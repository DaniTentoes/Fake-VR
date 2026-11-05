"""
Unit tests for AcoustiScript Engine, Creature Templates, and Event Handlers.
"""

import os
import sys
import time
import math

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)

from shared.models import Vector3
from server.room_manager import NPCInstance, RoomState
from server.script_engine import ScriptEngine, evaluate_condition


def test_condition_evaluation():
    print("[TEST 1] Testing AcoustiScript condition evaluation...")
    ctx = {"sound_type": "object_destroy", "distance": 4.5, "damage": 25}

    assert evaluate_condition('sound_type == "object_destroy"', ctx) is True
    assert evaluate_condition('sound_type == "echolocation_snap"', ctx) is False
    assert evaluate_condition('distance <= 5.0', ctx) is True
    assert evaluate_condition('distance <= 3.0', ctx) is False
    assert evaluate_condition('damage >= 20', ctx) is True
    assert evaluate_condition('damage < 10', ctx) is False
    print("  -> Condition evaluation PASSED.")


def test_script_loading():
    print("[TEST 2] Testing AcoustiScript file discovery & parsing...")
    scripts_dir = os.path.join(ROOT_DIR, "assets", "scripts")
    engine = ScriptEngine(scripts_dir)

    expected = ["creature_template", "wolf_companion", "guard_patrol", "merchant_idle", "default_idle"]
    for s in expected:
        assert s in engine.scripts, f"Missing expected script '{s}'"
        script = engine.scripts[s]
        assert len(script.events) > 0, f"Script '{s}' parsed 0 events"

    # Verify creature template properties
    creature_script = engine.scripts["creature_template"]
    assert creature_script.properties.get("clothing") == "naked"
    assert "interact" in creature_script.events
    assert "hear_sound" in creature_script.events
    assert "attack" in creature_script.events
    print("  -> Script parsing and creature template verified PASSED.")


def test_npc_patrol_and_approach():
    print("[TEST 3] Testing NPC waypoint patrol and player approach...")
    scripts_dir = os.path.join(ROOT_DIR, "assets", "scripts")
    engine = ScriptEngine(scripts_dir)

    guard = NPCInstance(
        npc_id="npc_cedric_test",
        name="Town Guard Cedric",
        spawn_pos=Vector3(-2.0, 3.5, 0.0),
        wander_radius=6.0,
        walk_interval=2.8,
        sound_asset="footsteps/wood_1.wav",
        clothing="armor",
        script_name="guard_patrol"
    )

    room = RoomState("test_patrol_room")
    room.npcs[guard.id] = guard

    # Context initialization executes 'on spawn'
    ctx = engine.get_or_create_context(guard)
    assert ctx is not None
    assert len(ctx.patrol_waypoints) == 4, f"Expected 4 waypoints, got {len(ctx.patrol_waypoints)}"

    # Test patrol stepping
    now = time.time()
    # Advance time to trigger step
    cmds = engine.on_tick_npc(guard, room, now + 10.0)
    assert any(c.get("type") == "step" for c in cmds)
    assert guard.is_moving is True

    # Test player approach hook
    class DummyPlayer:
        def __init__(self, pid, x, y):
            self.player_id = pid
            self.position = Vector3(x, y, 0.0)
        def to_player_state(self):
            return self
        def to_dict(self):
            return {"player_id": self.player_id, "position": self.position.to_dict()}

    player_near = DummyPlayer("p1", guard.position.x + 1.0, guard.position.y + 0.5)
    approach_cmds = engine.on_tick_npc(guard, room, now + 20.0, players=[player_near])
    say_cmds = [c for c in approach_cmds if c.get("type") == "say"]
    assert len(say_cmds) > 0, "Expected approach speech from guard"
    print(f"  -> Guard approach speech: '{say_cmds[0].get('text')}' PASSED.")


def test_creature_interaction_and_combat():
    print("[TEST 4] Testing Creature interaction and combat fleeing...")
    scripts_dir = os.path.join(ROOT_DIR, "assets", "scripts")
    engine = ScriptEngine(scripts_dir)

    wolf = NPCInstance(
        npc_id="npc_wolf_test",
        name="Forest Wolf",
        spawn_pos=Vector3(10.0, 10.0, 0.0),
        wander_radius=7.0,
        walk_interval=3.2,
        sound_asset="footsteps/carpet_1.wav",
        clothing="naked",
        script_name="wolf_companion"
    )

    room = RoomState("test_creature_room")
    room.npcs[wolf.id] = wolf

    # 1. Test Player Interact ('E')
    int_cmds = engine.execute_event(wolf, "interact", {
        "player_position": {"x": 10.5, "y": 10.0, "z": 0.0},
        "distance": 0.5
    })
    say_cmds = [c for c in int_cmds if c.get("type") == "say"]
    assert len(say_cmds) > 0
    assert "scratch" in say_cmds[0].get("text").lower() or "wolf" in say_cmds[0].get("text").lower()
    print(f"  -> Pet interaction response: '{say_cmds[0].get('text')}' PASSED.")

    # 2. Test Auditory Awareness (Echolocation snap pulse)
    echo_cmds = engine.execute_event(wolf, "hear_sound", {
        "sound_type": "echolocation_snap",
        "distance": 8.0,
        "player_position": {"x": 5.0, "y": 10.0, "z": 0.0}
    })
    echo_says = [c for c in echo_cmds if c.get("type") == "say"]
    assert len(echo_says) > 0
    assert "bark" in echo_says[0].get("text").lower()
    print(f"  -> Echolocation response: '{echo_says[0].get('text')}' PASSED.")

    # 3. Test Attack & Self-Defense
    atk_cmds = engine.execute_event(wolf, "attack", {
        "damage": 25,
        "attacker": {"position": {"x": 9.5, "y": 10.0, "z": 0.0}}
    })
    assert any(c.get("type") == "say" for c in atk_cmds)
    print("  -> Creature combat reaction PASSED.")


if __name__ == "__main__":
    test_condition_evaluation()
    test_script_loading()
    test_npc_patrol_and_approach()
    test_creature_interaction_and_combat()
    print("\n=======================================================")
    print("ALL ACOUSTISCRIPT & CREATURE TESTS PASSED SUCCESSFULLY!")
    print("=======================================================")
