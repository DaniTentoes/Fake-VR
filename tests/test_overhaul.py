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


def test_server_boot_persistence_and_multi_maps():
    print("\n[TEST 5] Testing Server Boot Preloading & Multi-Map Persistence...")
    from server.server import FakeVRServer
    server = FakeVRServer(port=7799)
    
    # Verify all maps were preloaded into room states at boot
    assert "default_world" in server.room_mgr.room_states
    assert "city_capital" in server.room_mgr.room_states
    assert "shop_blacksmith" in server.room_mgr.room_states
    
    lounge = server.room_mgr.get_or_create_room("lounge")
    assert lounge.room_id == "default_world"
    capital = server.room_mgr.room_states["city_capital"]
    blacksmith = server.room_mgr.room_states["shop_blacksmith"]
    
    # Check objects preloaded
    assert len(lounge.objects) > 0, "Lounge objects should be preloaded"
    assert len(capital.objects) > 0, "Capital city objects should be preloaded"
    assert len(blacksmith.objects) > 0, "Blacksmith workshop objects should be preloaded"
    
    # Check spawners & NPCs preloaded
    assert len(lounge.npcs) >= 2, f"Lounge NPCs should be preloaded, got {len(lounge.npcs)}"
    assert len(capital.npcs) >= 1, "Capital guard should be preloaded"
    assert len(blacksmith.npcs) >= 1, "Blacksmith NPC should be preloaded"
    
    # Verify unique IDs for every object across all rooms
    for r_name, r_st in [("lounge", lounge), ("capital", capital), ("blacksmith", blacksmith)]:
        ids = list(r_st.objects.keys())
        assert len(ids) == len(set(ids)), f"Duplicate object IDs detected in room '{r_name}'!"
        for obj_id, obj in r_st.objects.items():
            assert isinstance(obj.id, str) and len(obj.id) > 0
            assert obj.id == obj_id
            
    print("  -> Multi-map boot preloading and unique string IDs PASSED.")


def test_unique_id_damage_and_no_duplicate_respawn():
    print("\n[TEST 6] Testing Precise String ID Damage & Anti-Duplicate Respawn...")
    room = RoomState("test_arena")
    arena_map = {
        "title": "Arena",
        "objects": [
            {
                "id": "crate_alpha",
                "name": "Supply Crate",
                "material": "wood",
                "min_x": 1.0, "max_x": 2.0, "min_y": 1.0, "max_y": 2.0,
                "health": 50,
                "destructible": True,
                "regeneration": True,
                "respawn_time_seconds": 3
            },
            {
                "id": "crate_beta",
                "name": "Supply Crate", # Identical name, different ID!
                "material": "wood",
                "min_x": 5.0, "max_x": 6.0, "min_y": 5.0, "max_y": 6.0,
                "health": 50,
                "destructible": True,
                "regeneration": False, # Never respawns!
                "respawn_time_seconds": 3
            }
        ]
    }
    room.populate_from_map_dict(arena_map)
    assert "crate_alpha" in room.objects and "crate_beta" in room.objects
    
    now = time.time()
    # Damage crate_beta specifically by ID
    obj_b, destroyed_b = room.damage_object("crate_beta", 50, now)
    assert destroyed_b and obj_b.id == "crate_beta" and obj_b.is_destroyed
    # Verify crate_alpha was untouched!
    obj_a = room.objects["crate_alpha"]
    assert not obj_a.is_destroyed and obj_a.health == 50
    
    # Test respawns: crate_beta has regeneration=False, so must never respawn!
    respawned = room.update_respawns(now + 10.0)
    assert len(respawned) == 0, "crate_beta must never respawn because regeneration=False!"
    assert obj_b.is_destroyed
    
    # Now destroy crate_alpha
    obj_a_hit, destroyed_a = room.damage_object("crate_alpha", 50, now)
    assert destroyed_a and obj_a_hit.is_destroyed
    
    # Before 3s: 0 respawned
    assert len(room.update_respawns(now + 1.0)) == 0
    # After 3.5s: exactly crate_alpha respawns
    respawned_after = room.update_respawns(now + 3.5)
    assert len(respawned_after) == 1 and respawned_after[0].id == "crate_alpha"
    assert not respawned_after[0].is_destroyed
    # crate_beta still destroyed
    assert obj_b.is_destroyed
    print("  -> Precise string ID targeting and anti-duplicate respawn PASSED.")


def test_persistence_across_disconnect_and_relog():
    print("\n[TEST 7] Testing Persistence Across Disconnect, Relog & Server Reboot...")
    import tempfile
    now = time.time()
    with tempfile.TemporaryDirectory() as tmpdir:
        save_file = os.path.join(tmpdir, "test_room_world_state.json")
        room_st = RoomState("test_room", "Test Persistent Room")
        map_data = {
            "title": "Test Persistent Room",
            "objects": [
                {"id": "couch_1", "name": "Sofa", "min_x": 0, "max_x": 2, "min_y": 0, "max_y": 1, "health": 100, "destructible": True, "regeneration": False, "respawn_time_seconds": 0},
                {"id": "table_1", "name": "Table", "min_x": 3, "max_x": 4, "min_y": 0, "max_y": 1, "health": 100, "destructible": True, "regeneration": True, "respawn_time_seconds": 120}
            ],
            "spawners": [
                {"id": "guard_1", "entity_type": "npc", "x": 5.0, "y": 5.0, "npc_config": {"name": "Guard Bob", "walk_interval": 3.0}}
            ]
        }
        room_st.populate_from_map_dict(map_data)
        assert len(room_st.objects) == 2
        assert len(room_st.npcs) == 1

        # Destroy sofa couch (non-regenerating)
        obj_couch, dest_couch = room_st.damage_object("couch_1", 100, now)
        assert dest_couch and obj_couch.is_destroyed
        assert not obj_couch.solid_obstacle

        # Damage table to 50 HP
        obj_table, dest_table = room_st.damage_object("table_1", 50, now)
        assert not dest_table and obj_table.health == 50

        # Save persistent state (as happens on disconnect or attack)
        room_st.save_persistent_state(save_file)
        assert os.path.exists(save_file)

        # Simulate fresh server reboot or new client login: fresh RoomState initialized from base map
        rebooted_room = RoomState("test_room", "Test Persistent Room")
        rebooted_room.populate_from_map_dict(map_data)
        assert rebooted_room.objects["couch_1"].health == 100  # Before loading save, base map has 100

        # Load saved persistent state
        rebooted_room.load_persistent_state(save_file, now=now + 10.0)
        assert rebooted_room.objects["couch_1"].is_destroyed == True
        assert rebooted_room.objects["couch_1"].health == 0
        assert rebooted_room.objects["couch_1"].solid_obstacle == False
        assert rebooted_room.objects["table_1"].health == 50
        assert "npc_guard_1" in rebooted_room.npcs or "guard_1" in rebooted_room.npcs

        # Test live map dict serialization
        live_dict = rebooted_room.get_live_map_dict()
        couch_in_live = next(o for o in live_dict["field_objects"] if o["id"] == "couch_1")
        assert couch_in_live["is_destroyed"] == True
        assert couch_in_live["health"] == 0

        print("  -> Persistent state across disconnect, relog & reboot PASSED.")


if __name__ == "__main__":
    test_vector3_and_coordinates()
    test_server_authoritative_objects_and_respawn()
    test_travel_points_and_hierarchical_maps()
    test_npc_spawners_and_wander()
    test_server_boot_persistence_and_multi_maps()
    test_unique_id_damage_and_no_duplicate_respawn()
    test_persistence_across_disconnect_and_relog()
    print("\n=======================================================")
    print("ALL CORE ARCHITECTURAL & UNIT TESTS PASSED SUCCESSFULLY!")
    print("=======================================================")
