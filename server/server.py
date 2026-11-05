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
from typing import Dict, Optional, List

from shared.protocol import MessageType, encode_packet, decode_packet
from shared.models import Vector3, SurfaceType, AuraType
from server.room_manager import RoomManager, ConnectedPlayer, RoomState

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
        self.maps_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "maps")
        self.current_map_file = os.path.join(self.maps_dir, "default_world.json")
        self.current_map_data: dict = self._load_map_file(self.current_map_file)

        # Initialize primary room with map data
        lounge_room = self.room_mgr.get_or_create_room("lounge")
        lounge_room.populate_from_map_dict(self.current_map_data)

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

                    # Send authoritative map data
                    if self.current_map_data:
                        await self._send_packet(
                            writer,
                            MessageType.S2C_MAP_DATA,
                            {"map": self.current_map_data}
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
                        target_name = payload.get("target_name", "")
                        mat_val = payload.get("material", "wood")
                        damage = int(payload.get("damage", 25))
                        impact_dict = payload.get("impact_pos", player.position.to_dict())

                        # Authoritative Server Object Damage Check
                        obj, was_destroyed = room_st.damage_object(target_name, damage, time.time())

                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_ATTACK_EVENT,
                            {
                                "player_id": player.player_id,
                                "username": player.username,
                                "target_name": target_name,
                                "material": mat_val,
                                "destroyed": was_destroyed,
                                "impact_pos": impact_dict
                            }
                        )

                        # Broadcast synchronized object health to ALL clients
                        if obj:
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.S2C_OBJECT_STATE,
                                obj.to_dict()
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
                        for rid in self.room_mgr.rooms:
                            await self._broadcast_to_room(
                                rid,
                                MessageType.S2C_MAP_UPDATED,
                                {
                                    "updated_by": player.username,
                                    "title": self.current_map_data.get("title", "World"),
                                    "map": self.current_map_data
                                }
                            )
                        print(f"[Server]: Wall erected by {player.username} from ({x1},{y1}) to ({x2},{y2})")

                    elif msg_type == MessageType.C2S_TRAVEL.value:
                        # Player walked into a TravelPoint to another map
                        target_map = payload.get("target_map", "default_world.json")
                        spawn_dict = payload.get("spawn", {})
                        target_room_id = target_map.replace(".json", "")

                        print(f"[Server]: Player {player.username} traveling to '{target_map}'")
                        map_path = os.path.join(self.maps_dir, target_map)
                        child_map_data = self._load_map_file(map_path)
                        if not child_map_data:
                            child_map_data = self.current_map_data

                        old_room = player.room_id
                        self.room_mgr.change_room(player.player_id, target_room_id)
                        new_room_st = self.room_mgr.get_or_create_room(target_room_id)
                        new_room_st.populate_from_map_dict(child_map_data)

                        if spawn_dict:
                            player.position = Vector3.from_dict(spawn_dict)

                        # Notify old room
                        await self._broadcast_to_room(
                            old_room,
                            MessageType.S2C_PLAYER_LEFT,
                            {"player_id": player.player_id, "username": player.username}
                        )

                        # Welcome player to new room with map data
                        await self._send_packet(
                            writer,
                            MessageType.S2C_MAP_DATA,
                            {"map": child_map_data}
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

                        if npc:
                            await self._send_packet(
                                writer,
                                MessageType.S2C_INTERACT_RESPONSE,
                                {
                                    "speaker_name": npc.name,
                                    "dialogue": npc.dialogue,
                                    "sound_asset": npc.sound_asset
                                }
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
                        clothing = npc_cfg.get("clothing", "regular")
                        dialogue = npc_cfg.get("dialogue", "Greetings, traveler.")
                        script_name = npc_cfg.get("script_name", "default_idle")

                        npc_id = spawner_data.get("id") or f"npc_{str(uuid.uuid4())[:6]}"

                        new_npc = NPCInstance(
                            npc_id=npc_id,
                            name=npc_name,
                            spawn_pos=Vector3(sp_x, sp_y, sp_z),
                            wander_radius=wander_rad,
                            walk_interval=walk_interval,
                            sound_asset=sound_asset,
                            foley_asset=foley_asset,
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
                                "entity_type": "npc",
                                "x": sp_x, "y": sp_y, "z": sp_z,
                                "wander_radius": wander_rad,
                                "interval": walk_interval,
                                "npc_config": {
                                    "name": npc_name,
                                    "sound_asset": sound_asset,
                                    "foley_asset": foley_asset,
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

        while self.is_running:
            now = time.time()
            start_time = asyncio.get_event_loop().time()

            for room_id in list(self.room_mgr.rooms.keys()):
                players = self.room_mgr.get_room_players(room_id)
                room_st = self.room_mgr.get_or_create_room(room_id)

                # Check and broadcast object respawns (configurable timer!)
                respawned_objs = room_st.update_respawns(now)
                for robj in respawned_objs:
                    await self._broadcast_to_room(
                        room_id,
                        MessageType.S2C_OBJECT_RESPAWNED,
                        robj.to_dict()
                    )

                # Simulate NPC steps and wander movement
                if now - last_npc_check >= npc_step_check_interval:
                    moved_npcs = room_st.update_npcs(now)
                    if moved_npcs:
                        await self._broadcast_to_room(
                            room_id,
                            MessageType.S2C_NPC_UPDATE,
                            {"npcs": [n.to_dict() for n in room_st.npcs.values()]}
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

            for p in self.room_mgr.players.values():
                try:
                    await p.writer.drain()
                except Exception:
                    pass

            elapsed = asyncio.get_event_loop().time() - start_time
            sleep_time = max(0.001, interval - elapsed)
            await asyncio.sleep(sleep_time)
