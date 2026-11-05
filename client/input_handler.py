"""
Keyboard input handler for Fake-VR.
Processes movement (WASD), orientation (Arrows), echolocation (Space),
aura hotkeys (1-4), and accessibility shortcuts (C, P, M, H).
"""

from __future__ import annotations
import math
import time
import msvcrt
import threading
from typing import Callable, Optional

from shared.models import Vector3, SurfaceType, AuraType
from client.screen_reader import get_screen_reader
from client.ui_menu import MenuManager


class InputHandler:
    """Listens to keyboard commands and drives player movement and menu interaction."""

    def __init__(
        self,
        on_move: Callable[[Vector3, float, float, bool, SurfaceType], None],
        on_echolocation: Callable[[Vector3, float], None],
        on_aura_change: Callable[[AuraType], None],
        on_wall_bump: Callable[[Vector3], None],
        get_player_state: Callable[[], tuple[Vector3, float, SurfaceType, AuraType]],
        get_room_collider: Callable[[], any],
        menu_manager: MenuManager
    ):
        self.on_move = on_move
        self.on_echolocation = on_echolocation
        self.on_aura_change = on_aura_change
        self.on_wall_bump = on_wall_bump
        self.get_player_state = get_player_state
        self.get_room_collider = get_room_collider
        self.menu_manager = menu_manager

        self.step_size = 0.6          # 60cm per step (natural walking pace)
        self.turn_step_deg = 30.0     # 30-degree turn per arrow press (12 clock divisions)
        self.is_running = False
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self.is_running = True
        self._thread = threading.Thread(target=self._input_loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self.is_running = False

    def _input_loop(self) -> None:
        """Reads console keypresses using Windows msvcrt without blocking screen readers."""
        while self.is_running:
            if msvcrt.kbhit():
                ch = msvcrt.getwch()
                # Handle Windows special keys (arrows, function keys)
                if ch in ('\x00', '\xe0'):
                    code = msvcrt.getwch()
                    self._handle_special_key(code)
                else:
                    self._handle_char_key(ch)
            else:
                time.sleep(0.02)

    def _handle_special_key(self, code: str) -> None:
        """Handles arrow keys ('H'=Up, 'P'=Down, 'K'=Left, 'M'=Right)."""
        sr = get_screen_reader()

        # If menu is open, arrow keys navigate the menu
        if self.menu_manager.is_active:
            if code == 'H':  # Up Arrow
                self.menu_manager.handle_up()
            elif code == 'P':  # Down Arrow
                self.menu_manager.handle_down()
            elif code == 'K':  # Left Arrow (Back)
                self.menu_manager.handle_back()
            elif code == 'M':  # Right Arrow (Select)
                self.menu_manager.handle_select()
            return

        # Movement mode: Left/Right arrows rotate facing orientation
        pos, yaw, surface, aura = self.get_player_state()
        if code == 'K':  # Left arrow -> turn counter-clockwise
            new_yaw = (yaw - self.turn_step_deg) % 360.0
            self._update_orientation(pos, new_yaw, surface)
        elif code == 'M':  # Right arrow -> turn clockwise
            new_yaw = (yaw + self.turn_step_deg) % 360.0
            self._update_orientation(pos, new_yaw, surface)
        elif code == 'H':  # Up arrow in free mode -> step forward
            self._step(1.0, 0.0)
        elif code == 'P':  # Down arrow in free mode -> step backward
            self._step(-1.0, 0.0)

    def _handle_char_key(self, ch: str) -> None:
        sr = get_screen_reader()
        lower = ch.lower()

        # Check menu navigation
        if self.menu_manager.is_active:
            if ch == '\r':  # Enter key
                self.menu_manager.handle_select()
                return
            elif ch == '\x1b':  # Escape key
                self.menu_manager.handle_back()
                return
            elif lower in ('m', 'q'):
                self.menu_manager.close()
                return

        # Free Movement mode
        if lower == 'w':
            self._step(1.0, 0.0)  # Forward
        elif lower == 's':
            self._step(-1.0, 0.0)  # Backward
        elif lower == 'a':
            self._step(0.0, -1.0)  # Strafe Left
        elif lower == 'd':
            self._step(0.0, 1.0)  # Strafe Right
        elif ch == ' ':  # Spacebar -> Active Echolocation Snap
            pos, yaw, _, _ = self.get_player_state()
            self.on_echolocation(pos, yaw)
        elif lower == 'm' or ch == '\t':  # Open Menu
            self.menu_manager.handle_select() if self.menu_manager.is_active else None
        elif lower == 'c':  # Announce Coordinates and Environment
            self._announce_coordinates()
        elif lower == 'p':  # Announce Nearby Players
            self._announce_nearby_players()
        elif lower == 'h':  # Announce Help
            self._announce_help()
        # Aura quick toggles (1-4)
        elif ch == '1':
            self.on_aura_change(AuraType.NONE)
            sr.speak("Aura removed.")
        elif ch == '2':
            self.on_aura_change(AuraType.VINYL)
            sr.speak("Acoustic aura set to Vinyl Warmth.")
        elif ch == '3':
            self.on_aura_change(AuraType.CHIME)
            sr.speak("Acoustic aura set to Crystal Chime.")
        elif ch == '4':
            self.on_aura_change(AuraType.HUM)
            sr.speak("Acoustic aura set to Deep Hum.")
        elif ch == '5':
            self.on_aura_change(AuraType.SILK)
            sr.speak("Acoustic aura set to Silk Rustle.")
        elif ch == '\x1b':  # Escape
            if not self.menu_manager.is_active:
                sr.speak("Press M for menu or Q to quit.")

    def _step(self, forward_mult: float, strafe_mult: float) -> None:
        """Moves player relative to current facing yaw."""
        pos, yaw, old_surface, aura = self.get_player_state()
        room = self.get_room_collider()

        yaw_rad = math.radians(yaw)
        cos_y = math.cos(yaw_rad)
        sin_y = math.sin(yaw_rad)

        # Forward/backward vector
        dx = (sin_y * forward_mult + cos_y * strafe_mult) * self.step_size
        dz = (cos_y * forward_mult - sin_y * strafe_mult) * self.step_size

        target_pos = Vector3(pos.x + dx, pos.y, pos.z + dz)

        # Collision detection with room boundaries
        clamped_pos, did_collide = room.clamp_position(target_pos)

        if did_collide:
            self.on_wall_bump(clamped_pos)

        # Determine surface material under new coordinates
        new_surface = room.get_surface_at(clamped_pos)

        # If stepped onto a new surface, announce it via NVDA
        if new_surface != old_surface:
            get_screen_reader().speak(f"Stepped onto {new_surface.value} floor.")

        # Check if entering Resonance Mirror zone
        if room.resonance_zone:
            was_in_mirror = room.resonance_zone.contains(pos)
            is_in_mirror = room.resonance_zone.contains(clamped_pos)
            if not was_in_mirror and is_in_mirror:
                get_screen_reader().speak("Entered the Resonance Mirror chamber. Voice and echo reflections heightened.")
            elif was_in_mirror and not is_in_mirror:
                get_screen_reader().speak("Stepped away from the Resonance Mirror.")

        self.on_move(clamped_pos, yaw, 0.0, True, new_surface)

    def _update_orientation(self, pos: Vector3, new_yaw: float, surface: SurfaceType) -> None:
        """Updates head direction and announces compass heading."""
        # Convert yaw to compass direction
        compass = self._compass_heading(new_yaw)
        get_screen_reader().speak(f"Facing {compass}, {int(new_yaw)} degrees.")
        self.on_move(pos, new_yaw, 0.0, False, surface)

    def _compass_heading(self, yaw_deg: float) -> str:
        """Maps yaw angle to 8-point compass."""
        headings = ["North", "North-East", "East", "South-East", "South", "South-West", "West", "North-West"]
        idx = int((yaw_deg + 22.5) / 45.0) % 8
        return headings[idx]

    def _announce_coordinates(self) -> None:
        pos, yaw, surface, aura = self.get_player_state()
        room = self.get_room_collider()
        compass = self._compass_heading(yaw)
        in_mirror = room.resonance_zone.contains(pos) if room and room.resonance_zone else False
        mirror_txt = "Inside Resonance Mirror zone." if in_mirror else ""
        msg = f"Location: X {pos.x:.1f}, Z {pos.z:.1f}. Facing {compass} ({int(yaw)} degrees). Floor: {surface.value}. Aura: {aura.value}. {mirror_txt}"
        get_screen_reader().speak(msg, interrupt=True)

    def _announce_nearby_players(self) -> None:
        # Handled by main client orchestrator
        pass

    def _announce_help(self) -> None:
        help_text = (
            "Controls: "
            "W, A, S, D: Move forward, left, backward, right. "
            "Left and Right Arrow: Turn body 30 degrees. "
            "Spacebar: Active Echolocation Snap. "
            "Keys 1 through 5: Select Acoustic Aura. "
            "C: Location and surface info. "
            "P: Nearby players list and clock direction. "
            "M: Open Accessible Menu. "
            "H: Repeat this help."
        )
        get_screen_reader().speak(help_text, interrupt=True)
