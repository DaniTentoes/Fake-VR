"""
World and acoustic environment definitions for Fake-VR.
Includes raytraced room boundaries, surface material zones, travel points, spawners, and the Resonance Lounge.
Standardized 3D coordinates:
  X = East/West
  Y = North/South
  Z = Altitude / Height
"""

from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple, Dict, Any
from shared.models import Vector3, SurfaceType

SPEED_OF_SOUND = 343.0  # meters per second in air at ~20C


@dataclass
class SurfaceZone:
    """A rectangular area on the floor with a specific surface material."""
    name: str
    min_x: float
    max_x: float
    min_y: float
    max_y: float
    surface: SurfaceType

    def contains(self, pos: Vector3) -> bool:
        return (self.min_x <= pos.x <= self.max_x) and (self.min_y <= pos.y <= self.max_y)


@dataclass
class TravelPoint:
    """A gateway/portal moving the player to another map or structure."""
    id: str
    name: str
    trigger_x: float
    trigger_y: float
    radius: float = 1.2
    target_map_id: str = "lounge"
    target_spawn: Vector3 = field(default_factory=Vector3)

    def is_triggered(self, pos: Vector3) -> bool:
        dx = pos.x - self.trigger_x
        dy = pos.y - self.trigger_y
        return (dx * dx + dy * dy) <= (self.radius * self.radius)


@dataclass
class Spawner:
    """Spawns NPCs or dynamic world objects with customizable behavior."""
    id: str
    entity_type: str = "npc"  # "npc" or "object"
    position: Vector3 = field(default_factory=Vector3)
    interval: float = 10.0
    max_count: int = 1
    wander_radius: float = 5.0
    npc_config: Dict[str, Any] = field(default_factory=dict)


@dataclass
class AcousticMirrorZone:
    """
    The Resonance Mirror Zone:
    Acoustic chamber with parabolic-like reflection where a player's
    clicks and sound returns are heightened with crisp spatial presence.
    """
    name: str
    center: Vector3
    radius: float
    reverberation_boost: float = 1.8
    reflection_clarity: float = 0.95

    def contains(self, pos: Vector3) -> bool:
        return pos.distance_xy(self.center) <= self.radius


@dataclass
class WallObstacle:
    """A flat wall segment with sound absorption, reflection, and occlusion properties."""
    start: Vector3
    end: Vector3
    height: float = 3.0
    material: SurfaceType = SurfaceType.STONE
    reflectivity: float = 0.85
    is_doorway: bool = False
    linked_object_id: int = -1


@dataclass
class ServerWorldObject:
    """Authoritative physical entity on the server."""
    id: Any
    name: str
    min_x: float
    max_x: float
    min_y: float
    max_y: float
    min_z: float = 0.0
    max_z: float = 1.0
    height: float = 1.0
    material: SurfaceType = SurfaceType.WOOD
    solid_obstacle: bool = True
    destructible: bool = True
    regeneration: bool = True
    respawn_time_seconds: int = 60
    health: int = 100
    max_health: int = 100
    is_destroyed: bool = False
    destroyed_timestamp: float = 0.0
    sound_asset: str = ""
    sound_volume: float = 0.8
    sound_radius: float = 25.0

    def contains(self, pos: Vector3) -> bool:
        if self.is_destroyed:
            return False
        return (self.min_x <= pos.x <= self.max_x and
                self.min_y <= pos.y <= self.max_y and
                self.min_z <= pos.z <= self.max_z)

    def take_damage(self, amount: int, now_time: float) -> bool:
        if self.is_destroyed or not self.destructible:
            return False
        self.health = max(0, self.health - amount)
        if self.health == 0:
            self.is_destroyed = True
            self.solid_obstacle = False
            # Only record destruction timestamp if regeneration is enabled and timer is positive
            if self.regeneration and self.respawn_time_seconds > 0:
                self.destroyed_timestamp = now_time
            else:
                self.destroyed_timestamp = 0.0
            return True
        return False

    def check_respawn(self, now_time: float) -> bool:
        # Never respawn if regeneration is unchecked or timer is not positive
        if not self.regeneration or self.respawn_time_seconds <= 0:
            return False
        if self.is_destroyed and self.destroyed_timestamp > 0:
            if (now_time - self.destroyed_timestamp) >= self.respawn_time_seconds:
                self.is_destroyed = False
                self.health = self.max_health
                self.solid_obstacle = True
                self.destroyed_timestamp = 0.0
                return True
        return False

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "min_x": self.min_x,
            "max_x": self.max_x,
            "min_y": self.min_y,
            "max_y": self.max_y,
            "height": self.height,
            "material": self.material.value if hasattr(self.material, 'value') else str(self.material),
            "health": self.health,
            "max_health": self.max_health,
            "destructible": self.destructible,
            "regeneration": self.regeneration,
            "respawn_time_seconds": self.respawn_time_seconds,
            "is_destroyed": self.is_destroyed,
            "sound_asset": self.sound_asset,
            "sound_volume": self.sound_volume,
            "sound_radius": self.sound_radius
        }


@dataclass
class Room:
    """Represents a virtual map / region instance."""
    room_id: str
    title: str
    description: str
    min_bounds: Vector3
    max_bounds: Vector3
    spawn_point: Vector3
    surface_zones: List[SurfaceZone] = field(default_factory=list)
    walls: List[WallObstacle] = field(default_factory=list)
    travel_points: List[TravelPoint] = field(default_factory=list)
    spawners: List[Spawner] = field(default_factory=list)
    objects: List[ServerWorldObject] = field(default_factory=list)
    resonance_zone: Optional[AcousticMirrorZone] = None
    default_surface: SurfaceType = SurfaceType.WOOD
    reverb_decay_time: float = 0.35

    def get_surface_at(self, pos: Vector3) -> SurfaceType:
        for zone in self.surface_zones:
            if zone.contains(pos):
                return zone.surface
        return self.default_surface

    def clamp_position(self, pos: Vector3, margin: float = 0.3) -> Tuple[Vector3, bool]:
        collided = False
        new_x = pos.x
        new_y = pos.y

        min_x = self.min_bounds.x + margin
        max_x = self.max_bounds.x - margin
        min_y = self.min_bounds.y + margin
        max_y = self.max_bounds.y - margin

        if new_x < min_x:
            new_x = min_x
            collided = True
        elif new_x > max_x:
            new_x = max_x
            collided = True

        if new_y < min_y:
            new_y = min_y
            collided = True
        elif new_y > max_y:
            new_y = max_y
            collided = True

        return Vector3(new_x, new_y, pos.z), collided


def create_default_lounge() -> Room:
    """Creates the standard initial lounge room."""
    return Room(
        room_id="lounge",
        title="Lounge & Great Outdoor Field",
        description="A residential lounge connected to a wide 100m x 100m outdoor terrain.",
        min_bounds=Vector3(-50.0, -4.0, 0.0),
        max_bounds=Vector3(50.0, 104.0, 10.0),
        spawn_point=Vector3(0.0, -2.5, 0.0),
        surface_zones=[
            SurfaceZone("Living Room Wood Floor", -3.5, 3.5, -4.0, 4.0, SurfaceType.WOOD),
            SurfaceZone("Plush Rug", -2.0, 2.0, -1.8, 1.8, SurfaceType.CARPET),
            SurfaceZone("Outdoor Grass Field", -50.0, 50.0, 4.0, 104.0, SurfaceType.GRASS),
        ],
        walls=[
            WallObstacle(Vector3(-3.5, -4.0, 0.0), Vector3(3.5, -4.0, 0.0), material=SurfaceType.WOOD, reflectivity=0.60),
            WallObstacle(Vector3(3.5, -4.0, 0.0), Vector3(3.5, 4.0, 0.0), material=SurfaceType.STONE, reflectivity=0.75),
            WallObstacle(Vector3(-3.5, 4.0, 0.0), Vector3(-3.5, -4.0, 0.0), material=SurfaceType.STONE, reflectivity=0.75),
            WallObstacle(Vector3(-3.5, 4.0, 0.0), Vector3(-1.0, 4.0, 0.0), material=SurfaceType.WOOD, reflectivity=0.60),
            WallObstacle(Vector3(-1.0, 4.0, 0.0), Vector3(1.0, 4.0, 0.0), material=SurfaceType.WOOD, reflectivity=0.0, is_doorway=True),
            WallObstacle(Vector3(1.0, 4.0, 0.0), Vector3(2.5, 4.0, 0.0), material=SurfaceType.GLASS, reflectivity=0.96),
            WallObstacle(Vector3(2.5, 4.0, 0.0), Vector3(3.5, 4.0, 0.0), material=SurfaceType.WOOD, reflectivity=0.60),
        ],
        default_surface=SurfaceType.GRASS,
        reverb_decay_time=0.35
    )


WORLD_REGISTRY: Dict[str, Room] = {
    "lounge": create_default_lounge(),
    "resonance_lounge": create_default_lounge()
}
