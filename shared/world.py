"""
World and acoustic environment definitions.
Includes raytraced room boundaries, surface material zones, and the Resonance Lounge.
"""

from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from shared.models import Vector3, SurfaceType

SPEED_OF_SOUND = 343.0  # meters per second in air at ~20C


@dataclass
class SurfaceZone:
    """A rectangular area on the floor with a specific surface material."""
    name: str
    min_x: float
    max_x: float
    min_z: float
    max_z: float
    surface: SurfaceType

    def contains(self, pos: Vector3) -> bool:
        return (self.min_x <= pos.x <= self.max_x) and (self.min_z <= pos.z <= self.max_z)


@dataclass
class AcousticMirrorZone:
    """
    The Resonance Mirror Zone:
    In VRChat, players dwell around mirrors. In Fake-VR, this is an Acoustic Chamber
    with parabolic-like acoustic reflection where a player's own voice and clicks
    return with heightened clarity and spatial presence.
    """
    name: str
    center: Vector3
    radius: float
    reverberation_boost: float = 1.8
    reflection_clarity: float = 0.95

    def contains(self, pos: Vector3) -> bool:
        return pos.distance_xz(self.center) <= self.radius


@dataclass
class WallObstacle:
    """A flat wall segment with sound absorption and reflection properties."""
    start: Vector3
    end: Vector3
    height: float = 3.0
    material: SurfaceType = SurfaceType.STONE
    reflectivity: float = 0.85  # Fraction of acoustic energy reflected


@dataclass
class Room:
    """Represents a virtual hangout world."""
    room_id: str
    title: str
    description: str
    min_bounds: Vector3
    max_bounds: Vector3
    spawn_point: Vector3
    surface_zones: List[SurfaceZone] = field(default_factory=list)
    walls: List[WallObstacle] = field(default_factory=list)
    resonance_zone: Optional[AcousticMirrorZone] = None
    default_surface: SurfaceType = SurfaceType.WOOD
    reverb_decay_time: float = 1.2  # RT60 in seconds

    def get_surface_at(self, pos: Vector3) -> SurfaceType:
        """Determines the floor material at the given coordinate."""
        for zone in self.surface_zones:
            if zone.contains(pos):
                return zone.surface
        return self.default_surface

    def clamp_position(self, pos: Vector3, margin: float = 0.3) -> Tuple[Vector3, bool]:
        """
        Clamps position inside room boundaries.
        Returns (new_pos, did_collide_with_wall).
        """
        collided = False
        new_x = pos.x
        new_z = pos.z

        min_x = self.min_bounds.x + margin
        max_x = self.max_bounds.x - margin
        min_z = self.min_bounds.z + margin
        max_z = self.max_bounds.z - margin

        if new_x < min_x:
            new_x = min_x
            collided = True
        elif new_x > max_x:
            new_x = max_x
            collided = True

        if new_z < min_z:
            new_z = min_z
            collided = True
        elif new_z > max_z:
            new_z = max_z
            collided = True

        return Vector3(new_x, pos.y, new_z), collided

    def cast_echolocation_ray(self, origin: Vector3, direction: Vector3, max_dist: float = 30.0) -> Tuple[float, SurfaceType]:
        """
        Casts a ray to find the nearest wall collision for echolocation sound returns.
        Returns (distance_in_meters, surface_material).
        """
        d = direction.normalized()
        best_t = max_dist
        hit_surface = SurfaceType.STONE

        # Test against 4 bounding walls
        # X planes
        if abs(d.x) > 1e-5:
            # Min X wall
            t1 = (self.min_bounds.x - origin.x) / d.x
            if 0 < t1 < best_t:
                best_t = t1
                hit_surface = SurfaceType.WOOD
            # Max X wall
            t2 = (self.max_bounds.x - origin.x) / d.x
            if 0 < t2 < best_t:
                best_t = t2
                hit_surface = SurfaceType.WOOD

        # Z planes
        if abs(d.z) > 1e-5:
            # Min Z wall
            t3 = (self.min_bounds.z - origin.z) / d.z
            if 0 < t3 < best_t:
                best_t = t3
                hit_surface = SurfaceType.STONE
            # Max Z wall
            t4 = (self.max_bounds.z - origin.z) / d.z
            if 0 < t4 < best_t:
                best_t = t4
                hit_surface = SurfaceType.STONE

        return best_t, hit_surface


# --- PRECONFIGURED WORLDS ---

def create_resonance_lounge() -> Room:
    """
    The Resonance Lounge:
    The flagship hangout room. 14m x 10m x 4m.
    Features:
    - Center polished oak floor
    - Plush carpet conversational lounge in the west wing
    - Marble entryway in the east
    - The Resonance Mirror (Acoustic Chamber) at the north wall with parabolic sound reflections
    """
    return Room(
        room_id="resonance_lounge",
        title="The Resonance Lounge",
        description="A warm, acoustic hangout space with rich reflections and a central Resonance Mirror.",
        min_bounds=Vector3(-7.0, 0.0, -5.0),
        max_bounds=Vector3(7.0, 4.0, 5.0),
        spawn_point=Vector3(0.0, 0.0, -3.0),
        default_surface=SurfaceType.WOOD,
        reverb_decay_time=1.4,
        resonance_zone=AcousticMirrorZone(
            name="Resonance Mirror",
            center=Vector3(0.0, 0.0, 3.5),
            radius=2.5,
            reverberation_boost=2.2,
            reflection_clarity=1.0
        ),
        surface_zones=[
            # West lounge area: plush carpet
            SurfaceZone(
                name="Carpet Lounge",
                min_x=-6.5, max_x=-2.0,
                min_z=-4.5, max_z=4.5,
                surface=SurfaceType.CARPET
            ),
            # East entrance: polished stone/tile
            SurfaceZone(
                name="Stone Entryway",
                min_x=2.5, max_x=6.5,
                min_z=-4.5, max_z=4.5,
                surface=SurfaceType.STONE
            ),
            # North mirror zone: smooth resonance floor (wood/tile mix)
            SurfaceZone(
                name="Mirror Stage",
                min_x=-2.0, max_x=2.0,
                min_z=2.0, max_z=4.8,
                surface=SurfaceType.WOOD
            ),
        ]
    )


def create_acoustic_courtyard() -> Room:
    """
    The Acoustic Courtyard:
    An expansive open-air park (40m x 40m).
    Features gravel paths, soft grass lawns, and an acoustic water beacon in the center.
    """
    return Room(
        room_id="acoustic_courtyard",
        title="The Acoustic Courtyard",
        description="An expansive outdoor courtyard with grass lawns, stone pathways, and gentle breeze.",
        min_bounds=Vector3(-20.0, 0.0, -20.0),
        max_bounds=Vector3(20.0, 10.0, 20.0),
        spawn_point=Vector3(0.0, 0.0, -12.0),
        default_surface=SurfaceType.GRASS,
        reverb_decay_time=0.6,
        surface_zones=[
            # Center stone plaza
            SurfaceZone(
                name="Central Plaza",
                min_x=-5.0, max_x=5.0,
                min_z=-5.0, max_z=5.0,
                surface=SurfaceType.STONE
            ),
            # Stone walkway connecting North to South
            SurfaceZone(
                name="South Path",
                min_x=-1.5, max_x=1.5,
                min_z=-19.0, max_z=-5.0,
                surface=SurfaceType.STONE
            ),
            # Stone walkway connecting East to West
            SurfaceZone(
                name="East-West Path",
                min_x=-19.0, max_x=19.0,
                min_z=-1.5, max_z=1.5,
                surface=SurfaceType.STONE
            )
        ]
    )


WORLD_REGISTRY = {
    "resonance_lounge": create_resonance_lounge(),
    "acoustic_courtyard": create_acoustic_courtyard()
}
