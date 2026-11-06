"""
High-performance asynchronous TCP server for Fake-VR.
Authoritative host orchestrating:
1. Synchronized object health, destruction & configurable respawn timers.
2. In-game building tool wall persistence and distribution.
3. TravelPoints connecting hierarchical maps (Country -> Cities -> Structures).
4. Simulated wandering NPCs with interactable dialogue.
5. 20Hz spatial snapshot synchronization.
"""

from __future__ import annotations
import asyncio
import socket
import os
import json
import time
import math
from typing import Dict, Optional, List

from shared.protocol import MessageType, encode_packet, decode_packet
from shared.models import Vector3, SurfaceType, AuraType
from server.room_manager import RoomManager, ConnectedPlayer, RoomState
from server.script_engine import ScriptEngine

TICK_RATE_HZ = 20  # 20 updates/second


class FakeVRServer:
    """Authoritative Asyncio TCP server for Fake-VR."""

    def __init__(self, host: str = "0.0.0.0", port: int = 7777):
        self.host = host
        self.port = port
        self.room_mgr = RoomManager()
        self.server: Optional[asyncio.Server] = None
        self.is_running = False
        self._writer_to_player: Dict[asyncio.StreamWriter, ConnectedPlayer] = {}
        base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.maps_dir = os.path.join(base_dir, "assets", "maps")
        self.saves_dir = os.path.join(base_dir, "assets", "saves")
        os.makedirs(self.saves_dir, exist_ok=True)
        self.scripts_dir = os.path.join(base_dir, "assets", "scripts")
        self.script_engine = ScriptEngine(self.scripts_dir)
        self.current_map_file = os.path.join(self.maps_dir, "default_world.json")
        self.current_map_data: dict = self._load_map_file(self.current_map_file)
        self.loaded_maps: Dict[str, dict] = {}

        # Preload all world maps, structures, and spawners at boot time so they exist as long as server is up
        self._preload_all_world_maps()

    def _get_room_save_path(self, room_id: str) -> str:
        clean = self.room_mgr.normalize_room_id(room_id)
        return os.path.join(self.saves_dir, f"{clean}_world_state.json")

    def _preload_all_world_maps(self) -> None:
        """Loads all map files, restores persistent states, and instantiates NPCs at boot."""
        if not os.path.isdir(self.maps_dir):
            return

        now = time.time()
        for fname in os.listdir(self.maps_dir):
            if fname.endswith(".json"):
                fpath = os.path.join(self.maps_dir, fname)
                mdata = self._load_map_file(fpath)
                if not mdata:
                    continue

                room_id = self.room_mgr.normalize_room_id(fname)
                self.loaded_maps[room_id] = mdata
                r_state = self.room_mgr.get_or_create_room(room_id)
                r_state.populate_from_map_dict(mdata)
                # Restore persistent saved state (destroyed items, built walls, NPCs)
                save_p = self._get_room_save_path(room_id)
                r_state.load_persistent_state(save_p, now=now)
                print(f"[Server]: Boot preloaded map '{fname}' into room '{room_id}' ({len(r_state.npcs)} NPCs, {len(r_state.objects)} objects)")

    def _load_map_file(self, path: str) -> dict:
        if os.path.exists(path):
            try:
                with open(path, "r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception as e:
                print(f"[Server]: Error loading map file '{path}': {e}")
        return {}

    async def start(self) -> None:
        self.is_running = True
        self.server = await asyncio.start_server(self._handle_client, self.host, self.port)
        print(f"[Server]: Fake-VR Server listening on {self.host}:{self.port}")
        asyncio.create_task(self._tick_loop())

    async def stop(self) -> None:
        self.is_running = False
        for rid, rst in list(self.room_mgr.room_states.items()):
            rst.save_persistent_state(self._get_room_save_path(rid))
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        print("[Server]: Server stopped.")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        addr = writer.get_extra_info("peername")
        print(f"[Server]: New connection established from {addr}")
        player: Optional[ConnectedPlayer] = None

        try:
            while self.is_running:
                line = await reader.readline()
                if not line:
                    break

                packet = decode_packet(line)
                if not packet:
                    continue

                msg_type = packet.get("type")
                payload = packet.get("payload", {})

                if msg_type == MessageType.C2S_JOIN.value:
                    username = payload.get("username", "Player")
                    room_id = payload.get("room_id", "lounge")
                    aura = AuraType(payload.get("aura", AuraType.NONE.value))

                    player = self.room_mgr.add_player(writer, username, room_id, aura)
                    self._writer_to_player[writer] = player
                    print(f"[Server]: Player joined -> '{username}' (ID: {player.player_id}) in room '{room_id}'")

                    room_st = self.room_mgr.get_or_create_room(player.room_id)

                    # Send welcome packet
                    welcome_payload = {
                        "player_id": player.player_id,
                        "room_id": player.room_id,
                        "position": player.position.to_dict(),
                        "other_players": [
                            p.to_player_state().to_dict()
                            for p in self.room_mgr.get_room_players(player.room_id)
                            if p.player_id != player.player_id
                        ]
                    }
                    await self._send_packet(writer, MessageType.S2C_WELCOME, welcome_payload)

                    # Send live authoritative map data with destroyed object states, walls, and spawners
                    live_map = room_st.get_live_map_dict()
                    await self._send_packet(
                        writer,
                        MessageType.S2C_MAP_DATA,
                        {"map": live_map}
                    )

                    # Send existing destroyed/damaged object states so client doesn't see broken objects
                    for obj in room_st.objects.values():
                        if obj.is_destroyed or obj.health < obj.max_health:
                            await self._send_packet(
                                writer,
                                MessageType.S2C_OBJECT_STATE,
                                obj.to_dict()
                            )

                    # Send current NPCs in the room
                    if room_st.npcs:
                        await self._send_packet(
                            writer,
                            MessageType.S2C_NPC_UPDATE,
                            {"npcs": [npc.to_dict() for npc in room_st.npcs.values()]}
                        )

                    # Announce player joined
                    await self._broadcast_to_room(
                        player.room_id,
                        MessageType.S2C_PLAYER_JOINED,
                        {"player": player.to_player_state().to_dict()},
                        exclude_player_id=player.player_id
                    )

                elif player is not None:
                    room_st = self.room_mgr.get_or_create_room(player.room_id)

                    if msg_type == MessageType.C2S_MOVE.value:
                        pos_dict = payload.get("position", {})
                        player.position = Vector3.from_dict(pos_dict)
                        player.yaw = float(payload.get("yaw", 0.0))
                        player.pitch = float(payload.get("pitch", 0.0))
                        player.is_moving = bool(payload.get("is_moving", False))
                        player.is_jumping = bool(payload.get("is_jumping", False))
                        player.surface = SurfaceType(payload.get("surface", SurfaceType.WOOD.value))
                        player.clothing = payload.get("clothing", "regular")

                        if player.is_moving:
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.C2S_MOVE,
                                {
                                    "player_id": player.player_id,
                                    "position": player.position.to_dict(),
                                    "yaw": player.yaw,
                                    "surface": player.surface.value,
                                    "clothing": player.clothing,
                                    "is_jumping": player.is_jumping
                                },
                                exclude_player_id=player.player_id
                            )

                    elif msg_type == MessageType.C2S_ATTACK.value:
                        target_id = payload.get("target_id", "")
                        target_name = payload.get("target_name", "")
                        mat_val = payload.get("material", "wood")
                        damage = int(payload.get("damage", 25))
                        impact_dict = payload.get("impact_pos", player.position.to_dict())

                        # Authoritative Server Object Damage Check (look up by ID first, fallback to name)
                        lookup_key = target_id if target_id else target_name
                        obj, was_destroyed = room_st.damage_object(lookup_key, damage, time.time())

                        broadcast_target_name = obj.name if obj else target_name
                        broadcast_target_id = obj.id if obj else target_id

                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_ATTACK_EVENT,
                            {
                                "player_id": player.player_id,
                                "username": player.username,
                                "target_id": broadcast_target_id,
                                "target_name": broadcast_target_name,
                                "material": mat_val,
                                "destroyed": was_destroyed,
                                "impact_pos": impact_dict
                            }
                        )

                        # Broadcast synchronized object health to ALL clients and persist to disk
                        if obj:
                            room_st.save_persistent_state(self._get_room_save_path(player.room_id))
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.S2C_OBJECT_STATE,
                                obj.to_dict()
                            )

                        # Trigger AcoustiScript attack and auditory awareness routines for nearby NPCs & creatures
                        impact_x = float(impact_dict.get("x", player.position.x))
                        impact_y = float(impact_dict.get("y", player.position.y))
                        npc_state_changed = False

                        for n in list(room_st.npcs.values()):
                            ndist = math.hypot(impact_x - n.position.x, impact_y - n.position.y)
                            is_direct_hit = (lookup_key and (lookup_key == n.id.lower() or lookup_key in n.name.lower())) or (ndist <= 1.5)
                            if is_direct_hit:
                                atk_cmds = self.script_engine.execute_event(n, "attack", {
                                    "damage": damage,
                                    "distance": ndist,
                                    "attacker": player.to_player_state().to_dict(),
                                    "player_position": player.position.to_dict()
                                }, room_state=room_st)
                                for acmd in atk_cmds:
                                    if acmd.get("type") == "say":
                                        await self._broadcast_to_room(
                                            player.room_id,
                                            MessageType.S2C_INTERACT_RESPONSE,
                                            {
                                                "speaker_name": n.name,
                                                "dialogue": acmd.get("text", n.dialogue),
                                                "sound_asset": n.sound_asset
                                            }
                                        )
                                    elif acmd.get("type") in ("flee", "step"):
                                        npc_state_changed = True
                            elif ndist <= 15.0:
                                stype = "object_destroy" if was_destroyed else "attack"
                                hear_cmds = self.script_engine.execute_event(n, "hear_sound", {
                                    "sound_type": stype,
                                    "distance": ndist,
                                    "attacker": player.to_player_state().to_dict(),
                                    "player_position": player.position.to_dict()
                                }, room_state=room_st)
                                for hcmd in hear_cmds:
                                    if hcmd.get("type") == "say":
                                        await self._broadcast_to_room(
                                            player.room_id,
                                            MessageType.S2C_INTERACT_RESPONSE,
                                            {
                                                "speaker_name": n.name,
                                                "dialogue": hcmd.get("text", n.dialogue),
                                                "sound_asset": n.sound_asset
                                            }
                                        )
                                    elif hcmd.get("type") in ("flee", "step"):
                                        npc_state_changed = True

                        if npc_state_changed:
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.S2C_NPC_UPDATE,
                                {"npcs": [n.to_dict() for n in room_st.npcs.values()]}
                            )

                    elif msg_type == MessageType.C2S_BUILD_WALL.value:
                        # Player erected a wall using the building tool!
                        x1 = float(payload.get("x1", 0.0))
                        y1 = float(payload.get("y1", 0.0))
                        x2 = float(payload.get("x2", 0.0))
                        y2 = float(payload.get("y2", 0.0))
                        mat = payload.get("material", "wood")
                        name = payload.get("name", "Player Built Wall")

                        new_wall = {
                            "x1": x1, "y1": y1,
                            "x2": x2, "y2": y2,
                            "material": mat,
                            "reflectivity": 0.60,
                            "name": name,
                            "is_doorway": False
                        }

                        room_st.walls.append(new_wall)
                        room_st.save_persistent_state(self._get_room_save_path(player.room_id))

                        if "boundary_walls" not in self.current_map_data:
                            self.current_map_data["boundary_walls"] = []
                        self.current_map_data["boundary_walls"].append(new_wall)

                        # Save updated map to disk
                        try:
                            with open(self.current_map_file, "w", encoding="utf-8") as f:
                                json.dump(self.current_map_data, f, indent=2)
                        except Exception as e:
                            print(f"[Server]: Failed saving map with player built wall: {e}")

                        # Broadcast live map update to all connected players
                        live_map = room_st.get_live_map_dict()
                        for rid in self.room_mgr.rooms:
                            await self._broadcast_to_room(
                                rid,
                                MessageType.S2C_MAP_UPDATED,
                                {
                                    "updated_by": player.username,
                                    "title": room_st.title,
                                    "map": live_map
                                }
                            )
                        print(f"[Server]: Wall erected by {player.username} from ({x1},{y1}) to ({x2},{y2})")

                    elif msg_type == MessageType.C2S_TRAVEL.value:
                        # Player walked into a TravelPoint to another map
                        target_map = payload.get("target_map", "default_world.json")
                        spawn_dict = payload.get("spawn", {})
                        target_room_id = target_map.replace(".json", "")

                        print(f"[Server]: Player {player.username} traveling to '{target_map}'")
                        child_map_data = self.loaded_maps.get(target_room_id)
                        if not child_map_data:
                            map_path = os.path.join(self.maps_dir, target_map)
                            child_map_data = self._load_map_file(map_path)
                            if not child_map_data:
                                child_map_data = self.current_map_data
                            self.loaded_maps[target_room_id] = child_map_data

                        old_room = player.room_id
                        self.room_mgr.change_room(player.player_id, target_room_id)
                        new_room_st = self.room_mgr.get_or_create_room(target_room_id)
                        if not new_room_st.objects and not new_room_st.walls:
                            new_room_st.populate_from_map_dict(child_map_data)

                        if spawn_dict:
                            player.position = Vector3.from_dict(spawn_dict)

                        # Notify old room
                        await self._broadcast_to_room(
                            old_room,
                            MessageType.S2C_PLAYER_LEFT,
                            {"player_id": player.player_id, "username": player.username}
                        )

                        # Welcome player to new room with live map data
                        live_child_map = new_room_st.get_live_map_dict()
                        await self._send_packet(
                            writer,
                            MessageType.S2C_MAP_DATA,
                            {"map": live_child_map}
                        )

                        # Send authoritative damaged/destroyed states in new room
                        for obj in new_room_st.objects.values():
                            if obj.is_destroyed or obj.health < obj.max_health:
                                await self._send_packet(
                                    writer,
                                    MessageType.S2C_OBJECT_STATE,
                                    obj.to_dict()
                                )

                        # Send current active NPCs in new room
                        if new_room_st.npcs:
                            await self._send_packet(
                                writer,
                                MessageType.S2C_NPC_UPDATE,
                                {"npcs": [npc.to_dict() for npc in new_room_st.npcs.values()]}
                            )

                        # Notify new room
                        await self._broadcast_to_room(
                            target_room_id,
                            MessageType.S2C_PLAYER_JOINED,
                            {"player": player.to_player_state().to_dict()},
                            exclude_player_id=player.player_id
                        )

                    elif msg_type == MessageType.C2S_INTERACT.value:
                        target_id = payload.get("target_id", "").strip().lower()
                        npc = room_st.npcs.get(target_id)
                        if not npc:
                            for n in room_st.npcs.values():
                                if n.id.lower() == target_id or n.name.lower() in target_id or target_id in n.name.lower():
                                    npc = n
                                    break
                        if not npc:
                            target_tokens = [t for t in target_id.replace("_", " ").split() if len(t) > 2 and t != "npc"]
                            for n in room_st.npcs.values():
                                n_id_lower = n.id.lower()
                                n_name_lower = n.name.lower()
                                if any(t in n_id_lower or t in n_name_lower for t in target_tokens):
                                    npc = n
                                    break

                        if npc:
                            dist = math.hypot(player.position.x - npc.position.x, player.position.y - npc.position.y)
                            script_cmds = self.script_engine.execute_event(npc, "interact", {
                                "player": player.to_player_state().to_dict(),
                                "player_position": player.position.to_dict(),
                                "distance": dist
                            }, room_state=room_st)

                            sound_to_play = getattr(npc, "voice_sound", "") or npc.sound_asset
                            for cmd in script_cmds:
                                if cmd.get("type") == "play_sound":
                                    sound_to_play = cmd.get("sound_asset", sound_to_play)
                                elif cmd.get("type") == "say":
                                    npc.dialogue = cmd.get("text", npc.dialogue)

                            await self._send_packet(
                                writer,
                                MessageType.S2C_INTERACT_RESPONSE,
                                {
                                    "speaker_name": npc.name,
                                    "dialogue": npc.dialogue,
                                    "sound_asset": sound_to_play,
                                    "position": npc.position.to_dict()
                                }
                            )

                            # Broadcast updated NPC state (e.g. face_player orientation, updated dialogue)
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.S2C_NPC_UPDATE,
                                {"npcs": [n.to_dict() for n in room_st.npcs.values()]}
                            )

                    elif msg_type == MessageType.C2S_SPAWN_NPC.value:
                        import uuid
                        from server.room_manager import NPCInstance

                        spawner_data = payload.get("spawner", payload)
                        npc_cfg = spawner_data.get("npc_config", {})
                        npc_name = npc_cfg.get("name", spawner_data.get("name", "NPC"))
                        sp_x = float(spawner_data.get("x", player.position.x))
                        sp_y = float(spawner_data.get("y", player.position.y))
                        sp_z = float(spawner_data.get("z", 0.0))
                        wander_rad = float(spawner_data.get("wander_radius", 5.0))
                        walk_interval = float(npc_cfg.get("walk_interval", 3.0))
                        sound_asset = npc_cfg.get("sound_asset", "footsteps/wood_1.wav")
                        foley_asset = npc_cfg.get("foley_asset", "foley/regular/regular_1.wav")
                        voice_sound = npc_cfg.get("voice_sound", "auras/hum.wav")
                        clothing = npc_cfg.get("clothing", "regular")
                        dialogue = npc_cfg.get("dialogue", "Greetings, traveler.")
                        script_name = npc_cfg.get("script_name", "default_idle")
                        entity_type = spawner_data.get("entity_type", "npc")

                        npc_id = spawner_data.get("id") or f"npc_{str(uuid.uuid4())[:6]}"

                        new_npc = NPCInstance(
                            npc_id=npc_id,
                            name=npc_name,
                            spawn_pos=Vector3(sp_x, sp_y, sp_z),
                            wander_radius=wander_rad,
                            walk_interval=walk_interval,
                            sound_asset=sound_asset,
                            foley_asset=foley_asset,
                            voice_sound=voice_sound,
                            clothing=clothing,
                            dialogue=dialogue,
                            script_name=script_name
                        )
                        room_st.npcs[npc_id] = new_npc

                        # Save to server map data so it persists
                        if "spawners" not in self.current_map_data:
                            self.current_map_data["spawners"] = []
                        existing = [s for s in self.current_map_data["spawners"] if s.get("id") == npc_id]
                        if not existing:
                            self.current_map_data["spawners"].append({
                                "id": npc_id,
                                "entity_type": entity_type,
                                "x": sp_x, "y": sp_y, "z": sp_z,
                                "wander_radius": wander_rad,
                                "interval": walk_interval,
                                "npc_config": {
                                    "name": npc_name,
                                    "sound_asset": sound_asset,
                                    "foley_asset": foley_asset,
                                    "voice_sound": voice_sound,
                                    "clothing": clothing,
                                    "dialogue": dialogue,
                                    "script_name": script_name,
                                    "walk_interval": walk_interval
                                }
                            })
                            try:
                                with open(self.current_map_file, "w", encoding="utf-8") as f:
                                    json.dump(self.current_map_data, f, indent=2)
                            except Exception as e:
                                print(f"[Server]: Failed saving spawned NPC to map: {e}")

                        # Persist room state including spawned NPCs
                        room_st.save_persistent_state(self._get_room_save_path(player.room_id))

                        # Immediately broadcast updated NPC list to all clients in the room!
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_NPC_UPDATE,
                            {"npcs": [n.to_dict() for n in room_st.npcs.values()]}
                        )
                        print(f"[Server]: Spawned NPC '{npc_name}' (ID: {npc_id}) at ({sp_x}, {sp_y}) into active room '{player.room_id}'")

                    elif msg_type == MessageType.C2S_ECHOLOCATION.value:
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_ECHOLOCATION_EVENT,
                            {
                                "player_id": player.player_id,
                                "position": player.position.to_dict(),
                                "yaw": player.yaw
                            },
                            exclude_player_id=player.player_id
                        )

                        # NPCs and creatures hear the echolocation pulse and call back
                        for n in list(room_st.npcs.values()):
                            ndist = math.hypot(player.position.x - n.position.x, player.position.y - n.position.y)
                            if ndist <= 15.0:
                                echo_cmds = self.script_engine.execute_event(n, "hear_sound", {
                                    "sound_type": "echolocation_snap",
                                    "distance": ndist,
                                    "player": player.to_player_state().to_dict(),
                                    "player_position": player.position.to_dict()
                                }, room_state=room_st)
                                for ecmd in echo_cmds:
                                    if ecmd.get("type") == "say":
                                        await self._broadcast_to_room(
                                            player.room_id,
                                            MessageType.S2C_INTERACT_RESPONSE,
                                            {
                                                "speaker_name": n.name,
                                                "dialogue": ecmd.get("text", n.dialogue),
                                                "sound_asset": n.sound_asset
                                            }
                                        )
                                    elif ecmd.get("type") == "play_sound":
                                        await self._broadcast_to_room(
                                            player.room_id,
                                            MessageType.S2C_INTERACT_RESPONSE,
                                            {
                                                "speaker_name": "",
                                                "dialogue": "",
                                                "sound_asset": ecmd.get("sound_asset", "")
                                            }
                                        )

                    elif msg_type == MessageType.C2S_AURA_CHANGE.value:
                        aura_val = payload.get("aura", AuraType.NONE.value)
                        player.aura = AuraType(aura_val)
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_AURA_CHANGE,
                            {"player_id": player.player_id, "aura": player.aura.value},
                            exclude_player_id=player.player_id
                        )

                    elif msg_type == MessageType.C2S_UPDATE_MAP.value:
                        new_map = payload.get("map")
                        if new_map and isinstance(new_map, dict):
                            self.current_map_data = new_map
                            map_title = new_map.get("title", "Updated World")
                            try:
                                with open(self.current_map_file, "w", encoding="utf-8") as f:
                                    json.dump(new_map, f, indent=2)
                            except Exception as e:
                                print(f"[Server]: Failed saving updated map: {e}")

                            # Update all room states
                            for rid in self.room_mgr.rooms:
                                r_st = self.room_mgr.get_or_create_room(rid)
                                r_st.populate_from_map_dict(new_map)
                                await self._broadcast_to_room(
                                    rid,
                                    MessageType.S2C_MAP_UPDATED,
                                    {
                                        "updated_by": player.username,
                                        "title": map_title,
                                        "map": self.current_map_data
                                    }
                                )

                    elif msg_type == MessageType.C2S_CHAT.value:
                        chat_text = payload.get("text", "")
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_CHAT,
                            {
                                "player_id": player.player_id,
                                "username": player.username,
                                "text": chat_text,
                                "position": player.position.to_dict()
                            }
                        )

        except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
            pass
        finally:
            if player:
                print(f"[Server]: Player disconnected -> '{player.username}' ({player.player_id})")
                r_st = self.room_mgr.get_or_create_room(player.room_id)
                r_st.save_persistent_state(self._get_room_save_path(player.room_id))
                self.room_mgr.remove_player(player.player_id)
                self._writer_to_player.pop(writer, None)
                await self._broadcast_to_room(
                    player.room_id,
                    MessageType.S2C_PLAYER_LEFT,
                    {"player_id": player.player_id, "username": player.username}
                )

            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    async def _send_packet(self, writer: asyncio.StreamWriter, msg_type: MessageType, payload: dict) -> None:
        try:
            data = encode_packet(msg_type, payload)
            writer.write(data)
            await writer.drain()
        except Exception:
            pass

    async def _broadcast_to_room(
        self,
        room_id: str,
        msg_type: MessageType,
        payload: dict,
        exclude_player_id: Optional[str] = None
    ) -> None:
        players = self.room_mgr.get_room_players(room_id)
        data = encode_packet(msg_type, payload)
        for p in players:
            if exclude_player_id and p.player_id == exclude_player_id:
                continue
            try:
                p.writer.write(data)
                await p.writer.drain()
            except Exception:
                pass

    async def _tick_loop(self) -> None:
        """Authoritative broadcast loop syncing players, NPC wander steps, and respawns."""
        interval = 1.0 / TICK_RATE_HZ
        npc_step_check_interval = 0.5
        last_npc_check = 0.0
        last_auto_save = time.time()
        last_hot_reload_check = time.time()

        while self.is_running:
            now = time.time()
            start_time = asyncio.get_event_loop().time()

            # Check script hot reloads every 2.5s without interrupting gameplay
            if now - last_hot_reload_check >= 2.5:
                last_hot_reload_check = now
                self.script_engine.check_hot_reloads()

            for room_id in list(self.room_mgr.room_states.keys()):
                players = self.room_mgr.get_room_players(room_id)
                room_st = self.room_mgr.get_or_create_room(room_id)

                # Check and broadcast object respawns (configurable timer!)
                respawned_objs = room_st.update_respawns(now)
                if respawned_objs:
                    room_st.save_persistent_state(self._get_room_save_path(room_id))
                    for robj in respawned_objs:
                        await self._broadcast_to_room(
                            room_id,
                            MessageType.S2C_OBJECT_RESPAWNED,
                            robj.to_dict()
                        )

                # Simulate NPC steps, wander movement, and AcoustiScript routines
                if now - last_npc_check >= npc_step_check_interval:
                    moved_npcs, script_cmds = room_st.update_npcs(now, script_engine=self.script_engine, players=players)
                    if moved_npcs:
                        await self._broadcast_to_room(
                            room_id,
                            MessageType.S2C_NPC_UPDATE,
                            {"npcs": [n.to_dict() for n in room_st.npcs.values()]}
                        )
                    # Broadcast any audio / speech commands generated by scripts
                    for scmd in script_cmds:
                        if scmd.get("type") == "say":
                            await self._broadcast_to_room(
                                room_id,
                                MessageType.S2C_INTERACT_RESPONSE,
                                {
                                    "speaker_name": scmd.get("speaker", "NPC"),
                                    "dialogue": scmd.get("text", "..."),
                                    "sound_asset": ""
                                }
                            )
                        elif scmd.get("type") == "play_sound":
                            await self._broadcast_to_room(
                                room_id,
                                MessageType.S2C_INTERACT_RESPONSE,
                                {
                                    "speaker_name": "",
                                    "dialogue": "",
                                    "sound_asset": scmd.get("sound_asset", "")
                                }
                            )

                if players:
                    snapshot = [p.to_player_state().to_dict() for p in players]
                    data = encode_packet(MessageType.S2C_WORLD_TICK, {"players": snapshot})
                    for p in players:
                        try:
                            p.writer.write(data)
                        except Exception:
                            pass

            if now - last_npc_check >= npc_step_check_interval:
                last_npc_check = now

            # Periodic state auto-save every 15 seconds
            if now - last_auto_save >= 15.0:
                last_auto_save = now
                for rid, rst in self.room_mgr.room_states.items():
                    rst.save_persistent_state(self._get_room_save_path(rid))

            for p in self.room_mgr.players.values():
                try:
                    await p.writer.drain()
                except Exception:
                    pass

            elapsed = asyncio.get_event_loop().time() - start_time
            sleep_time = max(0.001, interval - elapsed)
            await asyncio.sleep(sleep_time)
