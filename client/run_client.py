"""
Main client application entrypoint for Fake-VR.
Runs the client-side spatial audio engine, NVDA screen reader interface,
and real-time multiplayer network synchronizer.
"""

from __future__ import annotations
import os
import sys
import time
import argparse
from typing import Dict

# Ensure project root is in sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from shared.models import (
    Vector3, SurfaceType, AuraType, PlayerState,
    relative_azimuth, clock_direction_description
)
from shared.world import WORLD_REGISTRY, Room
from shared.protocol import MessageType
from client.screen_reader import get_screen_reader
from client.audio.audio_engine import AudioEngine
from client.network_client import NetworkClient
from client.ui_menu import Menu, MenuItem, MenuManager
from client.input_handler import InputHandler


class FakeVRClient:
    """Orchestrates audio, accessibility, input, and networking for a player."""

    def __init__(self, username: str, host: str = "127.0.0.1", port: int = 7777, buffer_size: int = 1024):
        self.username = username
        self.host = host
        self.port = port

        # Core Subsystems
        self.sr = get_screen_reader()
        self.audio = AudioEngine(frame_size=buffer_size, buffer_latency="high")
        self.network = NetworkClient(host=self.host, port=self.port)
        self.menu_manager = MenuManager()

        # Local Player State
        self.player_id: str = ""
        self.current_room_id: str = "resonance_lounge"
        self.active_room: Room = WORLD_REGISTRY["resonance_lounge"]
        self.position: Vector3 = Vector3(self.active_room.spawn_point.x, self.active_room.spawn_point.y, self.active_room.spawn_point.z)
        self.yaw: float = 0.0
        self.pitch: float = 0.0
        self.surface: SurfaceType = self.active_room.get_surface_at(self.position)
        self.aura: AuraType = AuraType.NONE

        # Other connected players in room
        self.other_players: Dict[str, PlayerState] = {}

        # Set up menu and input
        self._build_menu()
        self.input_handler = InputHandler(
            on_move=self._on_local_move,
            on_echolocation=self._on_local_echolocation,
            on_aura_change=self._on_local_aura_change,
            on_wall_bump=self._on_local_wall_bump,
            get_player_state=self._get_player_state,
            get_room_collider=lambda: self.active_room,
            menu_manager=self.menu_manager
        )
        # Override nearby players method
        self.input_handler._announce_nearby_players = self._announce_nearby_players

        # Register network handlers
        self._setup_network_handlers()

    def start(self) -> None:
        """Starts client audio and attempts server connection."""
        print(f"Starting Fake-VR Client for {self.username}...")
        self.sr.speak(f"Welcome to Fake-VR, {self.username}. Initializing 3D binaural audio with Steam Audio.")

        # 1. Start Audio Engine
        self.audio.start()
        self.audio.set_room(self.active_room)
        self.audio.set_listener(self.position, self.yaw)

        # 2. Connect to Server
        connected = self.network.connect(username=self.username, initial_aura=self.aura)
        if not connected:
            self.sr.speak("Could not reach multiplayer server. Starting in offline acoustic mode.")
            print("Note: Running in offline acoustic sandbox mode.")
        else:
            self.sr.speak(f"Connected to server at {self.host}:{self.port}.")

        # 3. Start Input Listener
        self.input_handler.start()

        # Announce current spawn location
        self._announce_current_environment()

    def stop(self) -> None:
        """Cleans up client and exits."""
        self.input_handler.stop()
        self.network.disconnect()
        self.audio.stop()
        self.sr.speak("Fake-VR exited cleanly.")

    # --- LOCAL INTERACTION CALLBACKS ---

    def _get_player_state(self) -> tuple[Vector3, float, SurfaceType, AuraType]:
        return self.position, self.yaw, self.surface, self.aura

    def _on_local_move(
        self,
        new_pos: Vector3,
        new_yaw: float,
        pitch: float,
        is_step: bool,
        new_surface: SurfaceType
    ) -> None:
        self.position = new_pos
        self.yaw = new_yaw
        self.surface = new_surface

        # Update Audio Engine listener coordinates
        self.audio.set_listener(self.position, self.yaw)

        # Render footstep client-side immediately to eliminate audio lag!
        if is_step:
            self.audio.trigger_footstep(self.position, self.surface, is_local=True)

        # Synchronize with multiplayer server
        self.network.send_move(self.position, self.yaw, 0.0, is_step, self.surface)

    def _on_local_echolocation(self, pos: Vector3, yaw: float) -> None:
        # Trigger client-side raytraced echolocation pulse with Steam Audio HRTF
        self.audio.trigger_echolocation_pulse(pos, yaw)
        # Notify other players
        self.network.send_echolocation(pos, yaw)

    def _on_local_aura_change(self, new_aura: AuraType) -> None:
        self.aura = new_aura
        self.audio.set_local_aura(new_aura)
        self.network.send_aura_change(new_aura)

    def _on_local_wall_bump(self, pos: Vector3) -> None:
        self.audio.trigger_wall_bump(pos)

    # --- NETWORK EVENT HANDLERS ---

    def _setup_network_handlers(self) -> None:
        self.network.register_handler(MessageType.S2C_WELCOME, self._handle_welcome)
        self.network.register_handler(MessageType.S2C_WORLD_TICK, self._handle_world_tick)
        self.network.register_handler(MessageType.S2C_PLAYER_JOINED, self._handle_player_joined)
        self.network.register_handler(MessageType.S2C_PLAYER_LEFT, self._handle_player_left)
        self.network.register_handler(MessageType.S2C_ECHOLOCATION_EVENT, self._handle_remote_echolocation)
        self.network.register_handler(MessageType.S2C_AURA_CHANGE, self._handle_remote_aura_change)
        self.network.register_handler(MessageType.S2C_CHAT, self._handle_chat)

    def _handle_welcome(self, payload: dict) -> None:
        self.player_id = payload.get("player_id", "")
        room_id = payload.get("room_id", "resonance_lounge")
        if room_id in WORLD_REGISTRY:
            self.active_room = WORLD_REGISTRY[room_id]
            self.audio.set_room(self.active_room)

        raw_players = payload.get("other_players", [])
        for p in raw_players:
            ps = PlayerState.from_dict(p)
            if ps.player_id != self.player_id:
                self.other_players[ps.player_id] = ps
                self.audio.update_remote_aura(ps.player_id, ps.current_aura, ps.position)

    def _handle_world_tick(self, payload: dict) -> None:
        raw_players = payload.get("players", [])
        for p_dict in raw_players:
            p_id = p_dict.get("player_id")
            if p_id == self.player_id:
                continue

            ps = PlayerState.from_dict(p_dict)
            old_state = self.other_players.get(p_id)

            # If remote player moved, render their footstep at their 3D location in Steam Audio
            if ps.is_moving and old_state and ps.position.distance_to(old_state.position) > 0.1:
                self.audio.trigger_footstep(ps.position, ps.current_surface, is_local=False)

            self.other_players[p_id] = ps
            self.audio.update_remote_aura(p_id, ps.current_aura, ps.position)

    def _handle_player_joined(self, payload: dict) -> None:
        p_dict = payload.get("player", {})
        ps = PlayerState.from_dict(p_dict)
        if ps.player_id != self.player_id:
            self.other_players[ps.player_id] = ps
            self.audio.play_ui_sound("player_join")
            self.sr.speak(f"{ps.username} joined {self.active_room.title}.")

    def _handle_player_left(self, payload: dict) -> None:
        p_id = payload.get("player_id")
        username = payload.get("username", "A player")
        if p_id in self.other_players:
            del self.other_players[p_id]
            self.audio.remove_remote_player(p_id)
            self.audio.play_ui_sound("player_leave")
            self.sr.speak(f"{username} left the room.")

    def _handle_remote_echolocation(self, payload: dict) -> None:
        pos_dict = payload.get("position", {})
        pos = Vector3.from_dict(pos_dict)
        # Render the friend's snap in 3D space
        self.audio.trigger_remote_echolocation(pos)

    def _handle_remote_aura_change(self, payload: dict) -> None:
        p_id = payload.get("player_id")
        aura_str = payload.get("aura", AuraType.NONE.value)
        aura = AuraType(aura_str)
        if p_id in self.other_players:
            self.other_players[p_id].current_aura = aura
            self.audio.update_remote_aura(p_id, aura, self.other_players[p_id].position)

    def _handle_chat(self, payload: dict) -> None:
        sender = payload.get("username", "Someone")
        text = payload.get("text", "")
        self.audio.play_ui_sound("chat_notification")
        self.sr.speak(f"{sender} says: {text}")

    # --- SCREEN READER / ACCESSIBLE NOTIFICATIONS ---

    def _announce_current_environment(self) -> None:
        in_mirror = False
        if self.active_room.resonance_zone:
            in_mirror = self.active_room.resonance_zone.contains(self.position)

        msg = (
            f"You are in {self.active_room.title}. "
            f"Standing on {self.surface.value} floor. "
            f"{'You are inside the Resonance Mirror chamber.' if in_mirror else ''} "
            f"Press H for keybindings help, or M for menu."
        )
        self.sr.speak(msg)

    def _announce_nearby_players(self) -> None:
        if not self.other_players:
            self.sr.speak("No other players currently in this room.", interrupt=True)
            return

        lines = [f"{len(self.other_players)} players in room:"]
        for p in self.other_players.values():
            dist = self.position.distance_to(p.position)
            azimuth = relative_azimuth(self.position, self.yaw, p.position)
            clock = clock_direction_description(azimuth)
            aura_desc = f", wearing {p.current_aura.value} aura" if p.current_aura != AuraType.NONE else ""
            lines.append(f"{p.username}: {dist:.1f} meters away, at {clock}{aura_desc}.")

        self.sr.speak(" ".join(lines), interrupt=True)

    # --- ACCESSIBLE MENU DEFINITIONS ---

    def _build_menu(self) -> None:
        root = Menu(title="Main")

        # World Travel Sub-Menu
        travel_menu = Menu(title="World Travel")
        for room_key, room in WORLD_REGISTRY.items():
            travel_menu.add_item(MenuItem(
                title=room.title,
                action=lambda rk=room_key: self._teleport_to_room(rk),
                description=room.description
            ))
        root.add_item(MenuItem("World Travel", sub_menu=travel_menu, description="Change rooms or hangout spaces."))

        # Acoustic Fashion & Aura Sub-Menu
        aura_menu = Menu(title="Acoustic Fashion & Auras")
        aura_menu.add_item(MenuItem("None (Silent)", action=lambda: self._set_aura_from_menu(AuraType.NONE)))
        aura_menu.add_item(MenuItem("Vinyl Warmth", action=lambda: self._set_aura_from_menu(AuraType.VINYL), description="Warm dust and gentle vinyl crackle."))
        aura_menu.add_item(MenuItem("Crystal Chime", action=lambda: self._set_aura_from_menu(AuraType.CHIME), description="Subtle harmonic glass chimes."))
        aura_menu.add_item(MenuItem("Deep Hum", action=lambda: self._set_aura_from_menu(AuraType.HUM), description="Resonant sub-harmonic drone."))
        aura_menu.add_item(MenuItem("Silk Rustle", action=lambda: self._set_aura_from_menu(AuraType.SILK), description="Whispering fabric texture."))
        root.add_item(MenuItem("Acoustic Fashion & Auras", sub_menu=aura_menu, description="Express your avatar identity through sound."))

        # Status & Coordinates
        root.add_item(MenuItem(
            "Announce Status & Coordinates",
            action=lambda: self.input_handler._announce_coordinates(),
            description="Speaks position, facing compass, and floor surface."
        ))

        # Nearby Players List
        root.add_item(MenuItem(
            "List Nearby Players",
            action=lambda: self._announce_nearby_players(),
            description="Reports distance and clock directions of friends."
        ))

        # Controls & Help
        root.add_item(MenuItem(
            "Keybinding Help",
            action=lambda: self.input_handler._announce_help(),
            description="List of all keyboard controls."
        ))

        # Exit
        root.add_item(MenuItem("Quit Application", action=lambda: sys.exit(0)))

        self.root_menu = root

    def _teleport_to_room(self, room_id: str) -> None:
        if room_id in WORLD_REGISTRY:
            self.active_room = WORLD_REGISTRY[room_id]
            self.position = Vector3(self.active_room.spawn_point.x, self.active_room.spawn_point.y, self.active_room.spawn_point.z)
            self.surface = self.active_room.get_surface_at(self.position)
            self.audio.set_room(self.active_room)
            self.audio.set_listener(self.position, self.yaw)
            self.audio.play_ui_sound("door_transition")
            self.network.send_room_change(room_id)
            self.menu_manager.close()
            self._announce_current_environment()

    def _set_aura_from_menu(self, aura: AuraType) -> None:
        self._on_local_aura_change(aura)
        self.menu_manager.close()
        self.sr.speak(f"Equipped {aura.value} acoustic aura.")


def main():
    parser = argparse.ArgumentParser(description="Fake-VR Client: Audio-First Social Metaverse for Blind Users")
    parser.add_argument("--username", "-u", default=None, help="Your player name")
    parser.add_argument("--server", "-s", default="127.0.0.1", help="Server IP address (default: 127.0.0.1)")
    parser.add_argument("--port", "-p", type=int, default=7777, help="Server port (default: 7777)")
    parser.add_argument("--buffer", "-b", type=int, default=1024, choices=[512, 1024, 2048, 4096], help="Audio buffer blocksize (default: 1024)")
    args = parser.parse_args()

    username = args.username
    if not username:
        try:
            username = input("Enter your username: ").strip()
        except EOFError:
            username = "Player"
    if not username:
        username = "Player"

    client = FakeVRClient(username=username, host=args.server, port=args.port, buffer_size=args.buffer)
    client.start()

    print("\nFake-VR Client is running!")
    print("Press 'M' for menu, 'H' for help, 'Space' for echolocation snap, 'WASD' to move, 'Q' to quit.\n")

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        client.stop()


if __name__ == "__main__":
    main()
