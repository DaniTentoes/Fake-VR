"""
Room and Player state management for Fake-VR Server.
Handles player session tracking, room isolation, and world snapshot generation.
"""

from __future__ import annotations
import uuid
import time
from typing import Dict, List, Optional
from shared.models import Vector3, SurfaceType, AuraType, PlayerState
from shared.world import WORLD_REGISTRY, Room


class ConnectedPlayer:
    """Represents a connected client session on the server."""
    def __init__(self, writer, username: str, initial_room: str = "resonance_lounge", initial_aura: AuraType = AuraType.NONE):
        self.writer = writer
        self.player_id = str(uuid.uuid4())[:8]
        self.username = username
        self.room_id = initial_room
        self.position = Vector3(0.0, 0.0, 0.0)
        self.yaw = 0.0
        self.pitch = 0.0
        self.surface = SurfaceType.WOOD
        self.aura = initial_aura
        self.is_moving = False
        self.last_active = time.time()

        # Set spawn point from room definition
        if initial_room in WORLD_REGISTRY:
            spawn = WORLD_REGISTRY[initial_room].spawn_point
            self.position = Vector3(spawn.x, spawn.y, spawn.z)
            self.surface = WORLD_REGISTRY[initial_room].get_surface_at(self.position)

    def to_player_state(self) -> PlayerState:
        return PlayerState(
            player_id=self.player_id,
            username=self.username,
            position=self.position,
            yaw=self.yaw,
            pitch=self.pitch,
            current_surface=self.surface,
            current_aura=self.aura,
            is_moving=self.is_moving,
            room_id=self.room_id
        )


class RoomManager:
    """Tracks connected players and partitions them into virtual rooms."""

    def __init__(self):
        self.players: Dict[str, ConnectedPlayer] = {}
        # Rooms mapping: room_id -> list of player_ids
        self.rooms: Dict[str, List[str]] = {rid: [] for rid in WORLD_REGISTRY}
        # Add primary 'lounge' room so clients connecting to 'lounge' aren't remapped
        if "lounge" not in self.rooms:
            self.rooms["lounge"] = []

    def add_player(self, writer, username: str, room_id: str, aura: AuraType) -> ConnectedPlayer:
        if room_id not in self.rooms:
            self.rooms[room_id] = []

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
        if new_room_id not in self.rooms or player_id not in self.players:
            return False

        player = self.players[player_id]
        old_room = player.room_id
        if old_room in self.rooms and player_id in self.rooms[old_room]:
            self.rooms[old_room].remove(player_id)

        player.room_id = new_room_id
        self.rooms[new_room_id].append(player_id)

        # Reset spawn point
        if new_room_id in WORLD_REGISTRY:
            spawn = WORLD_REGISTRY[new_room_id].spawn_point
            player.position = Vector3(spawn.x, spawn.y, spawn.z)
            player.surface = WORLD_REGISTRY[new_room_id].get_surface_at(player.position)

        return True

    def get_room_players(self, room_id: str) -> List[ConnectedPlayer]:
        ids = self.rooms.get(room_id, [])
        return [self.players[pid] for pid in ids if pid in self.players]

    def get_room_state_snapshot(self, room_id: str) -> List[dict]:
        players = self.get_room_players(room_id)
        return [p.to_player_state().to_dict() for p in players]
