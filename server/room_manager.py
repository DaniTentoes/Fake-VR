"""
Room, Player, NPC and World Object management for Fake-VR Server.
Handles authoritative object health, destruction, configurable respawns,
NPC wander simulation, travel points, and world snapshots.
"""

from __future__ import annotations
import uuid
import time
import math
import random
from typing import Dict, List, Optional, Tuple, Any
from shared.models import Vector3, SurfaceType, AuraType, PlayerState
from shared.world import ServerWorldObject, Spawner, TravelPoint, Room, WORLD_REGISTRY


class ConnectedPlayer:
    """Represents a connected client session on the server."""
    def __init__(self, writer, username: str, initial_room: str = "lounge", initial_aura: AuraType = AuraType.NONE):
        self.writer = writer
        self.player_id = str(uuid.uuid4())[:8]
        self.username = username
        self.room_id = initial_room
        self.position = Vector3(0.0, -2.5, 0.0)
        self.yaw = 0.0
        self.pitch = 0.0
        self.surface = SurfaceType.WOOD
        self.aura = initial_aura
        self.clothing = "regular"
        self.is_moving = False
        self.is_jumping = False
        self.last_active = time.time()

    def to_player_state(self) -> PlayerState:
        return PlayerState(
            player_id=self.player_id,
            username=self.username,
            position=self.position,
            yaw=self.yaw,
            pitch=self.pitch,
            current_surface=self.surface,
            current_aura=self.aura,
            clothing=self.clothing,
            is_moving=self.is_moving,
            is_jumping=self.is_jumping,
            room_id=self.room_id
        )


class NPCInstance:
    """Simulated active non-player character in a room."""
    def __init__(
        self,
        npc_id: str,
        name: str,
        spawn_pos: Vector3,
        wander_radius: float = 5.0,
        walk_interval: float = 2.5,
        sound_asset: str = "footsteps/wood_1.wav",
        foley_asset: str = "foley/regular/regular_1.wav",
        clothing: str = "regular",
        dialogue: str = "Greetings, traveler.",
        script_name: str = "default_idle"
    ):
        self.id = npc_id
        self.name = name
        self.position = Vector3(spawn_pos.x, spawn_pos.y, spawn_pos.z)
        self.home_position = Vector3(spawn_pos.x, spawn_pos.y, spawn_pos.z)
        self.yaw = 0.0
        self.wander_radius = wander_radius
        self.walk_interval = walk_interval
        self.last_step_time = time.time() + random.uniform(0.5, 2.0)
        self.sound_asset = sound_asset
        self.foley_asset = foley_asset
        self.clothing = clothing
        self.dialogue = dialogue
        self.script_name = script_name
        self.is_moving = False

    def update(self, now: float) -> bool:
        """Wanders around home position. Returns True if a step occurred."""
        self.is_moving = False
        if now - self.last_step_time >= self.walk_interval:
            self.last_step_time = now
            # 60% chance to take a step, 40% chance to pause/look around
            if random.random() < 0.65:
                angle_rad = random.uniform(0, 2 * math.pi)
                step_dist = 1.0  # 1 tile step!
                new_x = self.position.x + math.sin(angle_rad) * step_dist
                new_y = self.position.y + math.cos(angle_rad) * step_dist

                # Keep within wander radius
                dx = new_x - self.home_position.x
                dy = new_y - self.home_position.y
                if (dx * dx + dy * dy) <= (self.wander_radius * self.wander_radius):
                    self.position.x = round(new_x, 2)
                    self.position.y = round(new_y, 2)
                    self.yaw = round(math.degrees(angle_rad), 1)
                    self.is_moving = True
                    return True
                else:
                    # Head back towards home position
                    back_rad = math.atan2(self.home_position.x - self.position.x, self.home_position.y - self.position.y)
                    self.position.x += math.sin(back_rad) * step_dist
                    self.position.y += math.cos(back_rad) * step_dist
                    self.yaw = round(math.degrees(back_rad), 1)
                    self.is_moving = True
                    return True
        return False

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "position": self.position.to_dict(),
            "yaw": self.yaw,
            "is_moving": self.is_moving,
            "sound_asset": self.sound_asset,
            "foley_asset": self.foley_asset,
            "clothing": self.clothing,
            "dialogue": self.dialogue
        }


class RoomState:
    """Complete server-authoritative state for a map / room."""
    def __init__(self, room_id: str, title: str = "World"):
        self.room_id = room_id
        self.title = title
        self.objects: Dict[str, ServerWorldObject] = {}
        self.npcs: Dict[str, NPCInstance] = {}
        self.walls: List[dict] = []
        self.travel_points: List[TravelPoint] = []
        self.spawners: List[Spawner] = []

    def populate_from_map_dict(self, data: dict) -> None:
        self.title = data.get("title", self.title)
        self.objects.clear()
        self.walls.clear()
        self.travel_points.clear()
        self.spawners.clear()
        self.npcs.clear()

        # Load walls
        for w in data.get("boundary_walls", []):
            self.walls.append(dict(w))

        # Helper to load objects
        next_id = 0
        def add_obj_from_dict(d: dict, prefix: str = "obj"):
            nonlocal next_id
            name = d.get("name", "Object")
            mat_str = d.get("material", "wood")
            try:
                mat = SurfaceType(mat_str)
            except ValueError:
                mat = SurfaceType.WOOD

            # Support both y and legacy z
            min_y = float(d.get("min_y", d.get("min_z", 0.0)))
            max_y = float(d.get("max_y", d.get("max_z", 0.0)))

            # Persistent Unique ID
            raw_id = d.get("id")
            if raw_id:
                obj_id = str(raw_id)
            else:
                sanitized_name = "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")
                obj_id = f"{prefix}_{sanitized_name}_{int(d.get('min_x', 0))}_{int(min_y)}"

            obj = ServerWorldObject(
                id=obj_id,
                name=name,
                min_x=float(d.get("min_x", 0.0)),
                max_x=float(d.get("max_x", 0.0)),
                min_y=min_y,
                max_y=max_y,
                height=float(d.get("height", 1.0)),
                material=mat,
                destructible=bool(d.get("destructible", True)),
                regeneration=bool(d.get("regeneration", True)),
                respawn_time_seconds=int(d.get("respawn_time_seconds", 60)),
                health=int(d.get("health", 100)),
                max_health=int(d.get("health", 100)),
                sound_asset=d.get("sound_asset", ""),
                sound_volume=float(d.get("sound_volume", 0.8)),
                sound_radius=float(d.get("sound_radius", 25.0))
            )
            self.objects[obj_id] = obj
            next_id += 1

        for s in data.get("structures", []):
            for o in s.get("interior_objects", []):
                add_obj_from_dict(o, prefix="struct")
            for w in s.get("walls", []):
                self.walls.append(dict(w))
        for o in data.get("field_objects", []):
            add_obj_from_dict(o, prefix="field")
        for o in data.get("objects", []):
            add_obj_from_dict(o, prefix="obj")

        # Load travel points
        for tp in data.get("travel_points", []):
            self.travel_points.append(TravelPoint(
                id=tp.get("id", "travel"),
                name=tp.get("name", "Travel Point"),
                trigger_x=float(tp.get("trigger_x", tp.get("x", 0.0))),
                trigger_y=float(tp.get("trigger_y", tp.get("y", tp.get("z", 0.0)))),
                radius=float(tp.get("radius", 1.5)),
                target_map_id=tp.get("target_map", "lounge"),
                target_spawn=Vector3.from_dict(tp.get("target_spawn", {}))
            ))

        # Load spawners & spawn NPCs
        for sp in data.get("spawners", []):
            spawner_id = sp.get("id", str(uuid.uuid4())[:6])
            entity_type = sp.get("entity_type", "npc")
            sp_pos = Vector3(
                float(sp.get("x", 0.0)),
                float(sp.get("y", sp.get("z", 0.0))),
                float(sp.get("z", 0.0))
            )
            wander_rad = float(sp.get("wander_radius", 5.0))
            cfg = sp.get("npc_config", {})

            if entity_type == "npc":
                npc = NPCInstance(
                    npc_id=f"npc_{spawner_id}",
                    name=cfg.get("name", "Traveler"),
                    spawn_pos=sp_pos,
                    wander_radius=wander_rad,
                    walk_interval=float(cfg.get("walk_interval", 3.0)),
                    sound_asset=cfg.get("sound_asset", "footsteps/wood_1.wav"),
                    foley_asset=cfg.get("foley_asset", "foley/regular/regular_1.wav"),
                    clothing=cfg.get("clothing", "regular"),
                    dialogue=cfg.get("dialogue", "Greetings, traveler."),
                    script_name=cfg.get("script_name", "default_idle")
                )
                self.npcs[npc.id] = npc

    def damage_object(self, target_identifier: str, damage: int, now: float) -> Tuple[Optional[ServerWorldObject], bool]:
        """Server-authoritative damage calculation by unique ID or name."""
        if not target_identifier:
            return None, False
        target_key = str(target_identifier).strip().lower()

        # 1. Direct match by unique string ID
        obj = self.objects.get(target_identifier) or self.objects.get(target_key)
        if not obj:
            # 2. Match by exact name
            for o in self.objects.values():
                if o.name.lower() == target_key:
                    obj = o
                    break
        if not obj:
            # 3. Match by name substring
            for o in self.objects.values():
                if target_key in o.name.lower() or o.name.lower() in target_key:
                    obj = o
                    break

        if obj:
            was_destroyed = obj.take_damage(damage, now)
            return obj, was_destroyed
        return None, False

    def update_respawns(self, now: float) -> List[ServerWorldObject]:
        """Respawn check respecting configurable timer and regeneration checkbox."""
        respawned = []
        for obj in list(self.objects.values()):
            if obj.check_respawn(now):
                respawned.append(obj)
        return respawned

    def update_npcs(self, now: float) -> List[NPCInstance]:
        """Simulate NPC locomotion."""
        moved = []
        for npc in self.npcs.values():
            if npc.update(now):
                moved.append(npc)
        return moved


class RoomManager:
    """Tracks connected players and partitions them into virtual rooms."""
    def __init__(self):
        self.players: Dict[str, ConnectedPlayer] = {}
        self.rooms: Dict[str, List[str]] = {"lounge": []}
        self.room_states: Dict[str, RoomState] = {"lounge": RoomState("lounge")}

    def get_or_create_room(self, room_id: str) -> RoomState:
        if room_id not in self.room_states:
            self.room_states[room_id] = RoomState(room_id)
            self.rooms[room_id] = []
        return self.room_states[room_id]

    def add_player(self, writer, username: str, room_id: str, aura: AuraType) -> ConnectedPlayer:
        self.get_or_create_room(room_id)
        player = ConnectedPlayer(writer, username, initial_room=room_id, initial_aura=aura)
        self.players[player.player_id] = player
        self.rooms[room_id].append(player.player_id)
        return player

    def remove_player(self, player_id: str) -> Optional[ConnectedPlayer]:
        player = self.players.pop(player_id, None)
        if player and player.room_id in self.rooms:
            if player_id in self.rooms[player.room_id]:
                self.rooms[player.room_id].remove(player_id)
        return player

    def change_room(self, player_id: str, new_room_id: str) -> bool:
        if player_id not in self.players:
            return False

        self.get_or_create_room(new_room_id)
        player = self.players[player_id]
        old_room = player.room_id
        if old_room in self.rooms and player_id in self.rooms[old_room]:
            self.rooms[old_room].remove(player_id)

        player.room_id = new_room_id
        self.rooms[new_room_id].append(player_id)
        return True

    def get_room_players(self, room_id: str) -> List[ConnectedPlayer]:
        ids = self.rooms.get(room_id, [])
        return [self.players[pid] for pid in ids if pid in self.players]
