"""
Domain models for Fake-VR (Audio-First Social Metaverse).
Defines spatial math, surface types, acoustic auras, player states, and acoustic zones.
"""

from __future__ import annotations
import math
from dataclasses import dataclass, field
from enum import Enum
from typing import List, Tuple, Optional


class SurfaceType(str, Enum):
    """Ground surface types that define footstep acoustics and tactile presence."""
    WOOD = "wood"
    STONE = "stone"
    CARPET = "carpet"
    METAL = "metal"
    GRASS = "grass"
    TILE = "tile"


class AuraType(str, Enum):
    """
    Acoustic Fashion & Auras:
    Avatar identity expressed through ambient psychoacoustic textures instead of visual shaders.
    """
    NONE = "none"
    VINYL = "vinyl"       # Warm vintage crackle & subtle dust warmth
    CHIME = "chime"       # Delicate crystal glass chime on movement
    HUM = "hum"           # Deep harmonic sub-bass drone / resonance
    SILK = "silk"         # Soft rustling fabric acoustic texture


@dataclass
class Vector3:
    """3D Vector with spatial calculation helpers for audio acoustics."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def distance_to(self, other: Vector3) -> float:
        dx = self.x - other.x
        dy = self.y - other.y
        dz = self.z - other.z
        return math.sqrt(dx * dx + dy * dy + dz * dz)

    def distance_xz(self, other: Vector3) -> float:
        """Horizontal 2D distance ignoring vertical height."""
        dx = self.x - other.x
        dz = self.z - other.z
        return math.sqrt(dx * dx + dz * dz)

    def length(self) -> float:
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def normalized(self) -> Vector3:
        l = self.length()
        if l < 1e-6:
            return Vector3(0.0, 0.0, 1.0)
        return Vector3(self.x / l, self.y / l, self.z / l)

    def dot(self, other: Vector3) -> float:
        return self.x * other.x + self.y * other.y + self.z * other.z

    def to_dict(self) -> dict:
        return {"x": round(self.x, 3), "y": round(self.y, 3), "z": round(self.z, 3)}

    @classmethod
    def from_dict(cls, d: dict) -> Vector3:
        return cls(x=float(d.get("x", 0.0)), y=float(d.get("y", 0.0)), z=float(d.get("z", 0.0)))


def relative_azimuth(listener_pos: Vector3, listener_yaw_deg: float, target_pos: Vector3) -> float:
    """
    Calculates azimuth angle in degrees (-180 to +180) of target relative to listener's facing direction.
    0 deg = directly ahead
    +90 deg = to the right
    -90 deg = to the left
    +/-180 deg = directly behind
    """
    dx = target_pos.x - listener_pos.x
    dz = target_pos.z - listener_pos.z
    target_angle_deg = math.degrees(math.atan2(dx, dz))
    rel = target_angle_deg - listener_yaw_deg
    while rel > 180.0:
        rel -= 360.0
    while rel < -180.0:
        rel += 360.0
    return rel


def clock_direction_description(rel_azimuth: float) -> str:
    """Converts relative azimuth degrees to a natural clock direction (e.g. 1 o'clock, 12 o'clock)."""
    # Shift so 0 deg is 12 o'clock
    hour = int(round(rel_azimuth / 30.0)) % 12
    if hour == 0:
        return "12 o'clock (directly ahead)"
    elif hour == 6:
        return "6 o'clock (directly behind)"
    elif hour > 0:
        return f"{hour} o'clock (to your right)"
    else:
        return f"{12 + hour} o'clock (to your left)"


@dataclass
class PlayerState:
    """Represents a connected player's avatar, position, and acoustic identity."""
    player_id: str
    username: str
    position: Vector3 = field(default_factory=Vector3)
    yaw: float = 0.0       # Facing angle in degrees (0 = North/+Z, 90 = East/+X)
    pitch: float = 0.0     # Looking up/down (-90 to +90)
    current_surface: SurfaceType = SurfaceType.WOOD
    current_aura: AuraType = AuraType.NONE
    is_moving: bool = False
    room_id: str = "resonance_lounge"

    def to_dict(self) -> dict:
        return {
            "player_id": self.player_id,
            "username": self.username,
            "position": self.position.to_dict(),
            "yaw": round(self.yaw, 1),
            "pitch": round(self.pitch, 1),
            "surface": self.current_surface.value,
            "aura": self.current_aura.value,
            "is_moving": self.is_moving,
            "room_id": self.room_id
        }

    @classmethod
    def from_dict(cls, d: dict) -> PlayerState:
        return cls(
            player_id=d["player_id"],
            username=d.get("username", "Anonymous"),
            position=Vector3.from_dict(d.get("position", {})),
            yaw=float(d.get("yaw", 0.0)),
            pitch=float(d.get("pitch", 0.0)),
            current_surface=SurfaceType(d.get("surface", SurfaceType.WOOD.value)),
            current_aura=AuraType(d.get("aura", AuraType.NONE.value)),
            is_moving=bool(d.get("is_moving", False)),
            room_id=d.get("room_id", "resonance_lounge")
        )
