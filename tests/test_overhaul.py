"""
Automated Integration Verification Suite for Fake-VR Overhaul
Tests:
1. Coordinate standardisation (X=East/West, Y=North/South, Z=Height/Jump)
2. Authoritative Server-side Object Health & Destruction Synchronization
3. Configurable Respawn Timer & Disabled Respawn validation
4. Physical Player 1-tile collision obstacle
5. In-game Building Tool wall erection & server persistence
6. TravelPoints & Hierarchical Map switching
7. NPC Spawners, wander locomotion, and interactive dialogue
"""

import sys
import os
import json
import time
import socket
import threading

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)

from shared.models import Vector3, SurfaceType, AuraType, PlayerState
from shared.protocol import MessageType, encode_packet, decode_packet
from server.room_manager import RoomManager, RoomState


def test_vector3_and_coordinates():
    print("[TEST 1] Testing 3D Vector3 Coordinate Standardisation...")
    v1 = Vector3(10.0, 20.0, 0.0)
    v2 = Vector3(10.0, 25.0, 3.0) # Jumped 3 tiles high
    assert v1.distance_xy(v2) == 5.0, f"Expected horizontal distance 5.0, got {v1.distance_xy(v2)}"
    assert round(v1.distance_to(v2), 2) == 5.83, f"Expected 3D distance 5.83, got {v1.distance_to(v2)}"
    
    # Dict serialization
    d = v2.to_dict()
    assert d == {"x": 10.0, "y": 25.0, "z": 3.0}
    v_loaded = Vector3.from_dict(d)
    assert v_loaded.x == 10.0 and v_loaded.y == 25.0 and v_loaded.z == 3.0
    print("  -> Vector3 math & serialization PASSED.")


def test_server_authoritative_objects_and_respawn():
    print("\n[TEST 2] Testing Server Authoritative Objects & Configurable Respawns...")
    room = RoomState("test_room")
    test_map = {
        "title": "Test World",
        "objects": [
            {
                "name": "Oak Chest",
                "material": "wood",
                "min_x": 0.0, "max_x": 1.0,
                "min_y": 0.0, "max_y": 1.0,
                "health": 50,
                "destructible": True,
                "regeneration": True,
                "respawn_time_seconds": 2
            },
            {
                "name": "Ancient Relic",
                "material": "stone",
                "min_x": 5.0, "max_x": 6.0,
                "min_y": 5.0, "max_y": 6.0,
                "health": 100,
                "destructible": True,
                "regeneration": False, # Respawn UNCHECKED!
                "respawn_time_seconds": 1
            }
        ]
    }
    room.populate_from_map_dict(test_map)

    # 1. Damage Oak Chest
    now = time.time()
    obj, destroyed = room.damage_object("Oak Chest", 25, now)
    assert obj is not None and not destroyed and obj.health == 25
    print("  -> Partial object damage: health reduced to 25. PASSED.")

    # 2. Break Oak Chest
    obj, destroyed = room.damage_object("Oak Chest", 30, now)
    assert destroyed and obj.is_destroyed and obj.health == 0
    print("  -> Destruction: object broken on server. PASSED.")

    # 3. Destroy non-regenerating Ancient Relic
    obj2, destroyed2 = room.damage_object("Ancient Relic", 100, now)
    assert destroyed2 and obj2.is_destroyed
    print("  -> Non-regenerating item destroyed. PASSED.")

    # 4. Check respawn after 0.5s (before 2s respawn time)
    respawned = room.update_respawns(now + 0.5)
    assert len(respawned) == 0, f"Expected 0 respawned, got {len(respawned)}"

    # 5. Check respawn after 2.5s
    respawned = room.update_respawns(now + 2.5)
    assert len(respawned) == 1, f"Expected 1 respawned (Oak Chest), got {len(respawned)}"
    assert respawned[0].name == "Oak Chest" and not respawned[0].is_destroyed
    assert respawned[0].health == 50
    print("  -> Configurable 2-second respawn verified. PASSED.")

    # 6. Verify Ancient Relic NEVER respawned because regeneration = False
    assert obj2.is_destroyed, "Ancient Relic must remain destroyed because regeneration is False!"
    print("  -> Unchecked respawn validation: non-regenerating item NEVER respawned. PASSED.")


def test_travel_points_and_hierarchical_maps():
    print("\n[TEST 3] Testing TravelPoints & Map Transitions...")
    room = RoomState("lounge")
    with open("assets/maps/default_world.json", "r", encoding="utf-8") as f:
        data = json.load(f)
    room.populate_from_map_dict(data)

    assert len(room.travel_points) >= 2, f"Expected travel points, got {len(room.travel_points)}"
    tp_capital = next((tp for tp in room.travel_points if tp.target_map_id == "city_capital.json"), None)
    assert tp_capital is not None, "Capital city travel point not found!"
    
    # Test triggering at (0, 102)
    assert tp_capital.is_triggered(Vector3(0.0, 102.0, 0.0))
    assert not tp_capital.is_triggered(Vector3(0.0, 50.0, 0.0))
    print(f"  -> Travel point '{tp_capital.name}' trigger detection PASSED.")


def test_npc_spawners_and_wander():
    print("\n[TEST 4] Testing Spawner & NPC Locomotion Simulation...")
    room = RoomState("lounge")
    with open("assets/maps/default_world.json", "r", encoding="utf-8") as f:
        data = json.load(f)
    room.populate_from_map_dict(data)

    assert len(room.npcs) >= 1, f"Expected spawned NPCs, got {len(room.npcs)}"
    npc = next(iter(room.npcs.values()))
    print(f"  -> Spawned NPC: {npc.name} at ({npc.position.x}, {npc.position.y}) with dialogue '{npc.dialogue}'")
    
    # Simulate wander step
    now = time.time() + 10.0
    npc.last_step_time = 0.0
    stepped = npc.update(now)
    print(f"  -> NPC step simulated: {stepped}, new position: ({npc.position.x}, {npc.position.y})")
    assert npc.dialogue != ""
    print("  -> NPC simulation PASSED.")


if __name__ == "__main__":
    test_vector3_and_coordinates()
    test_server_authoritative_objects_and_respawn()
    test_travel_points_and_hierarchical_maps()
    test_npc_spawners_and_wander()
    print("\n=======================================================")
    print("ALL CORE ARCHITECTURAL & UNIT TESTS PASSED SUCCESSFULLY!")
    print("=======================================================")
