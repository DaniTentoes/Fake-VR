"""
AcoustiScript Engine for Fake-VR.
Authoritative NPC & Creature scripting language interpreter for the multiplayer server.
Supports event-driven routines, spatial audio cues, physical locomotion,
and player proximity/interaction handling.
"""

from __future__ import annotations
import os
import re
import time
import math
import random
from typing import Dict, List, Optional, Any, Tuple, Union


def evaluate_condition(cond_str: str, context: dict) -> bool:
    """
    Safely evaluates simple AcoustiScript conditional expressions against a context dict.
    Supports equality, inequality, and numeric comparisons (==, !=, <=, >=, <, >).
    e.g. sound_type == "object_destroy" or distance <= 4.0
    """
    cond = cond_str.strip()
    if not cond:
        return True
    if cond.endswith(":"):
        cond = cond[:-1].strip()

    m = re.match(r"^([a-zA-Z0-9_]+)\s*(==|!=|<=|>=|<|>)\s*(.+)$", cond)
    if not m:
        # Check simple boolean flag in context
        val = context.get(cond.lower())
        return bool(val)

    var_name = m.group(1).lower()
    op = m.group(2)
    rhs_raw = m.group(3).strip()

    val = context.get(var_name)
    if val is None:
        return False

    # String comparison
    if (rhs_raw.startswith('"') and rhs_raw.endswith('"')) or (rhs_raw.startswith("'") and rhs_raw.endswith("'")):
        rhs = rhs_raw[1:-1]
        val_str = str(val)
        if op == "==":
            return val_str == rhs
        elif op == "!=":
            return val_str != rhs
        return False

    # Numeric comparison
    try:
        rhs_num = float(rhs_raw)
        val_num = float(val)
        if op == "==":
            return abs(val_num - rhs_num) < 1e-5
        elif op == "!=":
            return abs(val_num - rhs_num) >= 1e-5
        elif op == "<":
            return val_num < rhs_num
        elif op == "<=":
            return val_num <= rhs_num
        elif op == ">":
            return val_num > rhs_num
        elif op == ">=":
            return val_num >= rhs_num
    except (ValueError, TypeError):
        return False

    return False


class CommandNode:
    """Represents a single executable action statement."""
    def __init__(self, raw_line: str, indent: int):
        self.raw_line = raw_line.strip()
        self.indent = indent
        self.cmd, self.args = self._parse_command(self.raw_line)

    def _parse_command(self, line: str) -> Tuple[str, str]:
        if not line or line.startswith("#"):
            return "", ""
        parts = line.split(" ", 1)
        cmd = parts[0].strip().lower()
        args = parts[1].strip() if len(parts) > 1 else ""
        return cmd, args


class ConditionNode:
    """Represents an if / elif / else branching structure."""
    def __init__(self, condition: str):
        self.condition = condition
        self.true_nodes: List[Union[CommandNode, ConditionNode]] = []
        self.elif_branches: List[Tuple[str, List[Union[CommandNode, ConditionNode]]]] = []
        self.else_nodes: List[Union[CommandNode, ConditionNode]] = []


class ScriptEventBlock:
    """An event handler block, e.g. 'on interact(player):' or 'on player_approach(distance <= 4.0):'"""
    def __init__(self, event_name: str, condition_str: str = ""):
        self.event_name = event_name.lower().strip()
        self.condition_str = condition_str.strip()
        self.nodes: List[Union[CommandNode, ConditionNode]] = []
        self.condition_distance: Optional[float] = None
        self._parse_condition()

    def _parse_condition(self):
        dist_match = re.search(r"distance\s*<=\s*([0-9.]+)", self.condition_str)
        if dist_match:
            try:
                self.condition_distance = float(dist_match.group(1))
            except ValueError:
                self.condition_distance = None


class AcoustiScript:
    """Parsed AcoustiScript instance holding properties and event blocks."""
    def __init__(self, script_name: str, file_path: str):
        self.name = script_name
        self.file_path = file_path
        self.last_modified = 0.0
        self.properties: Dict[str, Any] = {
            "entity_type": "npc",
            "clothing": "regular",
            "sound_asset": "footsteps/wood_1.wav",
            "foley_asset": "foley/regular/regular_1.wav",
            "voice_sound": "auras/hum.wav",
            "dialogue": "...",
            "wander_radius": 5.0,
            "walk_interval": 3.0,
            "flee_distance": 5.0,
            "patrol_interval": 3.2
        }
        self.patrol_waypoints: List[Tuple[float, float]] = []
        self.events: Dict[str, List[ScriptEventBlock]] = {}

    def parse(self, text: str):
        self.events.clear()
        lines = text.splitlines()

        # Step 1: Separate into blocks and header properties
        blocks_raw: List[Tuple[str, str, List[Tuple[int, str]]]] = []
        current_event_header: Optional[Tuple[str, str]] = None
        current_block_lines: List[Tuple[int, str]] = []

        for raw_line in lines:
            line_stripped = raw_line.strip()
            if not line_stripped or line_stripped.startswith("#"):
                continue

            on_match = re.match(r"^on\s+([a-zA-Z0-9_]+)(?:\((.*?)\))?\s*:", line_stripped)
            if on_match:
                if current_event_header is not None:
                    blocks_raw.append((current_event_header[0], current_event_header[1], list(current_block_lines)))
                    current_block_lines.clear()

                current_event_header = (on_match.group(1).lower(), on_match.group(2) or "")
                continue

            if current_event_header is None:
                prop_line = line_stripped
                if "#" in prop_line:
                    prop_line = prop_line.split("#", 1)[0].strip()
                if ":" in prop_line:
                    key, val = prop_line.split(":", 1)
                    k = key.strip().lower()
                    v = val.strip().strip('"').strip("'")
                    try:
                        if "." in v:
                            self.properties[k] = float(v)
                        else:
                            self.properties[k] = int(v)
                    except ValueError:
                        self.properties[k] = v
            else:
                indent = len(raw_line) - len(raw_line.lstrip())
                current_block_lines.append((indent, line_stripped))

        if current_event_header is not None and current_block_lines:
            blocks_raw.append((current_event_header[0], current_event_header[1], list(current_block_lines)))

        # Step 2: Build AST for each block
        for ev_name, cond_str, blk_lines in blocks_raw:
            blk = ScriptEventBlock(ev_name, cond_str)
            blk.nodes = self._parse_lines_to_nodes(blk_lines)
            if ev_name not in self.events:
                self.events[ev_name] = []
            self.events[ev_name].append(blk)

    def _parse_lines_to_nodes(self, lines: List[Tuple[int, str]]) -> List[Union[CommandNode, ConditionNode]]:
        nodes: List[Union[CommandNode, ConditionNode]] = []
        i = 0
        n = len(lines)

        while i < n:
            indent, text = lines[i]

            if text.startswith("if "):
                cond_str = text[3:].rstrip(":").strip()
                cond_node = ConditionNode(cond_str)
                i += 1

                # Gather 'if' body lines
                true_lines = []
                while i < n and lines[i][0] > indent and not (lines[i][1].startswith("elif ") or lines[i][1].startswith("else:")):
                    true_lines.append(lines[i])
                    i += 1
                cond_node.true_nodes = self._parse_lines_to_nodes(true_lines)

                # Gather 'elif' branches
                while i < n and lines[i][0] == indent and lines[i][1].startswith("elif "):
                    elif_cond = lines[i][1][5:].rstrip(":").strip()
                    i += 1
                    elif_lines = []
                    while i < n and lines[i][0] > indent and not (lines[i][1].startswith("elif ") or lines[i][1].startswith("else:")):
                        elif_lines.append(lines[i])
                        i += 1
                    cond_node.elif_branches.append((elif_cond, self._parse_lines_to_nodes(elif_lines)))

                # Gather 'else' branch
                if i < n and lines[i][0] == indent and lines[i][1].startswith("else:"):
                    i += 1
                    else_lines = []
                    while i < n and lines[i][0] > indent:
                        else_lines.append(lines[i])
                        i += 1
                    cond_node.else_nodes = self._parse_lines_to_nodes(else_lines)

                nodes.append(cond_node)
            else:
                nodes.append(CommandNode(text, indent))
                i += 1

        return nodes


class ScriptExecutionContext:
    """Runtime execution state per active NPC instance."""
    def __init__(self, script: AcoustiScript, npc_id: str):
        self.script = script
        self.npc_id = npc_id
        self.current_waypoint_idx = 0
        self.last_approach_time: Dict[str, float] = {}
        self.patrol_waypoints: List[Tuple[float, float]] = list(script.patrol_waypoints)
        self.fleeing_until: float = 0.0
        self.last_script_step_time: float = 0.0


class ScriptEngine:
    """Authoritative singleton managing and executing all NPC & creature scripts."""
    def __init__(self, scripts_dir: str):
        self.scripts_dir = scripts_dir
        self.scripts: Dict[str, AcoustiScript] = {}
        self.npc_contexts: Dict[str, ScriptExecutionContext] = {}
        self.load_all_scripts()

    def load_all_scripts(self) -> None:
        """Discovers and compiles all .fake scripts in the scripts directory."""
        if not os.path.isdir(self.scripts_dir):
            return

        for root, _, files in os.walk(self.scripts_dir):
            for fname in files:
                if fname.endswith(".fake") or fname.endswith(".txt"):
                    fpath = os.path.join(root, fname)
                    sname = fname.rsplit(".", 1)[0].lower()
                    self._load_script_file(sname, fpath)

    def _load_script_file(self, script_name: str, fpath: str) -> bool:
        try:
            mtime = os.path.getmtime(fpath)
            with open(fpath, "r", encoding="utf-8") as f:
                content = f.read()

            script = AcoustiScript(script_name, fpath)
            script.last_modified = mtime
            script.parse(content)
            self.scripts[script_name] = script
            print(f"[ScriptEngine]: Loaded AcoustiScript '{script_name}' from '{os.path.basename(fpath)}'")
            return True
        except Exception as e:
            print(f"[ScriptEngine]: Error parsing script '{script_name}': {e}")
            return False

    def check_hot_reloads(self) -> None:
        """Hot-reloads any scripts on disk if modified without requiring a server reboot."""
        if not os.path.isdir(self.scripts_dir):
            return
        for root, _, files in os.walk(self.scripts_dir):
            for fname in files:
                if fname.endswith(".fake"):
                    fpath = os.path.join(root, fname)
                    sname = fname.rsplit(".", 1)[0].lower()
                    try:
                        mtime = os.path.getmtime(fpath)
                        existing = self.scripts.get(sname)
                        if not existing or mtime > existing.last_modified:
                            self._load_script_file(sname, fpath)
                    except OSError:
                        pass

    def get_or_create_context(self, npc: Any) -> Optional[ScriptExecutionContext]:
        script_key = str(getattr(npc, "script_name", "default_idle")).lower().strip()
        script = self.scripts.get(script_key)
        if not script:
            script = self.scripts.get("default_idle")
            if not script:
                return None

        if npc.id not in self.npc_contexts or self.npc_contexts[npc.id].script.name != script.name:
            ctx = ScriptExecutionContext(script, npc.id)
            self.npc_contexts[npc.id] = ctx
            # Run 'on spawn' trigger once
            self.execute_event(npc, "spawn", {})
        return self.npc_contexts[npc.id]

    def execute_event(self, npc: Any, event_name: str, event_data: dict, room_state: Any = None) -> List[dict]:
        """
        Executes an event block for an NPC.
        Returns a list of side-effect commands (e.g. broadcasts, sounds, speech) for the server.
        """
        ctx = self.get_or_create_context(npc)
        if not ctx:
            return []

        blocks = ctx.script.events.get(event_name.lower(), [])
        if not blocks:
            return []

        out_commands: List[dict] = []
        for blk in blocks:
            if blk.condition_distance is not None:
                dist = float(event_data.get("distance", 999.0))
                if dist > blk.condition_distance:
                    continue

            self._execute_node_list(blk.nodes, npc, ctx, event_data, out_commands)

        return out_commands

    def _execute_node_list(
        self,
        nodes: List[Union[CommandNode, ConditionNode]],
        npc: Any,
        ctx: ScriptExecutionContext,
        event_data: dict,
        out_commands: List[dict]
    ) -> None:
        for node in nodes:
            if isinstance(node, CommandNode):
                self._execute_single_command(node, npc, ctx, event_data, out_commands)
            elif isinstance(node, ConditionNode):
                matched = False
                if evaluate_condition(node.condition, event_data):
                    matched = True
                    self._execute_node_list(node.true_nodes, npc, ctx, event_data, out_commands)
                if not matched:
                    for elif_cond, elif_nodes in node.elif_branches:
                        if evaluate_condition(elif_cond, event_data):
                            matched = True
                            self._execute_node_list(elif_nodes, npc, ctx, event_data, out_commands)
                            break
                if not matched and node.else_nodes:
                    self._execute_node_list(node.else_nodes, npc, ctx, event_data, out_commands)

    def _execute_single_command(
        self,
        act: CommandNode,
        npc: Any,
        ctx: ScriptExecutionContext,
        event_data: dict,
        out_commands: List[dict]
    ) -> None:
        cmd = act.cmd
        args = act.args
        if not cmd:
            return

        if cmd in ("say", "speak"):
            text = args.strip('"').strip("'")
            npc.dialogue = text
            out_commands.append({
                "type": "say",
                "speaker": npc.name,
                "text": text,
                "npc_id": npc.id,
                "position": npc.position.to_dict()
            })

        elif cmd == "play_sound":
            parts = [p.strip() for p in args.split(",")]
            snd_asset = parts[0].strip('"').strip("'")
            vol = 0.8
            if len(parts) > 1 and "volume=" in parts[1]:
                try:
                    vol = float(parts[1].split("=")[1])
                except ValueError:
                    vol = 0.8
            out_commands.append({
                "type": "play_sound",
                "sound_asset": snd_asset,
                "volume": vol,
                "position": npc.position.to_dict()
            })

        elif cmd == "set_dialogue":
            npc.dialogue = args.strip('"').strip("'")

        elif cmd == "change_clothing":
            npc.clothing = args.strip('"').strip("'")

        elif cmd in ("walk_to", "step_towards"):
            coords = re.findall(r"[-+]?[0-9]*\.?[0-9]+", args)
            if len(coords) >= 2:
                target_x = float(coords[0])
                target_y = float(coords[1])
                dx = target_x - npc.position.x
                dy = target_y - npc.position.y
                dist = math.hypot(dx, dy)
                if dist > 0.1:
                    step_dist = min(1.0, dist)
                    npc.position.x = round(npc.position.x + (dx / dist) * step_dist, 2)
                    npc.position.y = round(npc.position.y + (dy / dist) * step_dist, 2)
                    npc.yaw = round(math.degrees(math.atan2(dx, dy)), 1)
                    npc.is_moving = True
                    out_commands.append({
                        "type": "step",
                        "npc_id": npc.id,
                        "position": npc.position.to_dict(),
                        "yaw": npc.yaw
                    })

        elif cmd == "flee_from":
            # e.g. flee_from attacker, flee_distance OR flee_from 5.0, 10.0
            attacker = event_data.get("attacker") or event_data.get("player_position")
            fx, fy = None, None
            if "attacker" in args and attacker:
                if isinstance(attacker, dict):
                    pos = attacker.get("position", attacker)
                    fx = float(pos.get("x", npc.position.x))
                    fy = float(pos.get("y", npc.position.y))
            else:
                coords = re.findall(r"[-+]?[0-9]*\.?[0-9]+", args)
                if len(coords) >= 2:
                    fx = float(coords[0])
                    fy = float(coords[1])

            if fx is not None and fy is not None:
                dx = npc.position.x - fx
                dy = npc.position.y - fy
                dist = math.hypot(dx, dy)
                if dist < 0.1:
                    dx, dy = 1.0, 0.0
                    dist = 1.0

                flee_dist = float(ctx.script.properties.get("flee_distance", 5.0))
                step_dist = min(flee_dist, 3.0)
                npc.position.x = round(npc.position.x + (dx / dist) * step_dist, 2)
                npc.position.y = round(npc.position.y + (dy / dist) * step_dist, 2)
                npc.yaw = round(math.degrees(math.atan2(dx, dy)), 1)
                npc.is_moving = True
                ctx.fleeing_until = time.time() + 3.0
                out_commands.append({
                    "type": "flee",
                    "npc_id": npc.id,
                    "position": npc.position.to_dict(),
                    "yaw": npc.yaw
                })

        elif cmd == "face_player":
            p_pos = event_data.get("player_position")
            if not p_pos and "player" in event_data:
                p_pos = event_data["player"].get("position")
            if p_pos:
                px = float(p_pos.get("x", npc.position.x))
                py = float(p_pos.get("y", npc.position.y))
                dx = px - npc.position.x
                dy = py - npc.position.y
                if abs(dx) > 0.05 or abs(dy) > 0.05:
                    npc.yaw = round(math.degrees(math.atan2(dx, dy)), 1)

        elif cmd == "face":
            coords = re.findall(r"[-+]?[0-9]*\.?[0-9]+", args)
            if len(coords) >= 2:
                dx = float(coords[0]) - npc.position.x
                dy = float(coords[1]) - npc.position.y
                npc.yaw = round(math.degrees(math.atan2(dx, dy)), 1)

        elif cmd == "patrol_waypoints":
            pairs = re.findall(r"\(\s*([-+]?[0-9.]+)\s*,\s*([-+]?[0-9.]+)\s*\)", args)
            ctx.patrol_waypoints = [(float(x), float(y)) for x, y in pairs]

    def on_tick_npc(self, npc: Any, room_state: Any, now: float, players: Optional[List[Any]] = None) -> List[dict]:
        """Called on server tick to handle patrol waypoints, approach checks, and AI routines."""
        ctx = self.get_or_create_context(npc)
        if not ctx:
            return []

        out_commands: List[dict] = []

        # 1. Patrol waypoints handling
        if ctx.patrol_waypoints and (now - ctx.last_script_step_time >= float(ctx.script.properties.get("patrol_interval", 3.2))):
            ctx.last_script_step_time = now
            wp = ctx.patrol_waypoints[ctx.current_waypoint_idx % len(ctx.patrol_waypoints)]
            dx = wp[0] - npc.position.x
            dy = wp[1] - npc.position.y
            dist = math.hypot(dx, dy)

            if dist <= 0.8:
                ctx.current_waypoint_idx = (ctx.current_waypoint_idx + 1) % len(ctx.patrol_waypoints)
                wp = ctx.patrol_waypoints[ctx.current_waypoint_idx % len(ctx.patrol_waypoints)]
                dx = wp[0] - npc.position.x
                dy = wp[1] - npc.position.y
                dist = math.hypot(dx, dy)

            if dist > 0.1:
                step = min(1.0, dist)
                npc.position.x = round(npc.position.x + (dx / dist) * step, 2)
                npc.position.y = round(npc.position.y + (dy / dist) * step, 2)
                npc.yaw = round(math.degrees(math.atan2(dx, dy)), 1)
                npc.is_moving = True
                out_commands.append({
                    "type": "step",
                    "npc_id": npc.id,
                    "position": npc.position.to_dict(),
                    "yaw": npc.yaw
                })

        # 2. Player Proximity / Approach Detection
        if players:
            for p in players:
                dx = p.position.x - npc.position.x
                dy = p.position.y - npc.position.y
                dist = math.hypot(dx, dy)
                if dist <= 3.5:
                    last_t = ctx.last_approach_time.get(p.player_id, 0.0)
                    if now - last_t >= 10.0:  # 10s cooldown per player
                        ctx.last_approach_time[p.player_id] = now
                        approach_cmds = self.execute_event(npc, "player_approach", {
                            "distance": dist,
                            "player": p.to_player_state().to_dict() if hasattr(p, "to_player_state") else p.to_dict(),
                            "player_position": p.position.to_dict()
                        }, room_state=room_state)
                        out_commands.extend(approach_cmds)

        return out_commands
