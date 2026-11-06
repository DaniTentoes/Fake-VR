#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fakevr {

using json = nlohmann::json;

// Standardized 3D Coordinate System:
// X = East / West (horizontal)
// Y = North / South (horizontal, facing 0 deg = North/+Y)
// Z = Altitude / Height (vertical, 0 = ground level, >0 = jumping/elevation)
struct Vector3 {
    float x = 0.0f; // East / West
    float y = 0.0f; // North / South
    float z = 0.0f; // Altitude / Height

    Vector3() = default;
    Vector3(float _x, float _y, float _z = 0.0f) : x(_x), y(_y), z(_z) {}

    float distance_to(const Vector3& o) const {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float distance_xy(const Vector3& o) const {
        float dx = x - o.x, dy = y - o.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Backwards-compatible distance alias for horizontal 2D plane
    float distance_xz(const Vector3& o) const {
        return distance_xy(o);
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float magnitude() const {
        return length();
    }

    float length_horizontal() const {
        return std::sqrt(x * x + y * y);
    }

    Vector3 normalized() const {
        float l = length();
        if (l < 1e-6f) return Vector3(0.0f, 1.0f, 0.0f);
        return Vector3(x / l, y / l, z / l);
    }
};

enum class SurfaceType {
    WOOD,
    STONE,
    CARPET,
    METAL,
    GRASS,
    TILE,
    GLASS,
    LEATHER_FABRIC
};

enum class ClothingType {
    REGULAR,
    NAKED,
    ARMOR
};

enum class AuraType {
    NONE,
    VINYL,
    CHIME,
    HUM,
    SILK
};

inline std::string aura_to_string(AuraType a) {
    switch (a) {
        case AuraType::NONE: return "none";
        case AuraType::VINYL: return "vinyl crackle";
        case AuraType::CHIME: return "crystal chime";
        case AuraType::HUM: return "sub-bass hum";
        case AuraType::SILK: return "soft silk";
        default: return "none";
    }
}

inline AuraType string_to_aura(const std::string& s) {
    if (s == "vinyl") return AuraType::VINYL;
    if (s == "chime") return AuraType::CHIME;
    if (s == "hum") return AuraType::HUM;
    if (s == "silk") return AuraType::SILK;
    return AuraType::NONE;
}

inline std::string clothing_to_string(ClothingType c) {
    switch (c) {
        case ClothingType::REGULAR: return "regular clothes";
        case ClothingType::NAKED: return "naked";
        case ClothingType::ARMOR: return "suit of armor";
        default: return "regular clothes";
    }
}

inline std::string surface_to_string(SurfaceType s) {
    switch (s) {
        case SurfaceType::WOOD: return "wood";
        case SurfaceType::STONE: return "stone";
        case SurfaceType::CARPET: return "carpet";
        case SurfaceType::METAL: return "metal";
        case SurfaceType::GRASS: return "grass";
        case SurfaceType::TILE: return "tile";
        case SurfaceType::GLASS: return "glass";
        case SurfaceType::LEATHER_FABRIC: return "fabric upholstery";
        default: return "wood";
    }
}

inline SurfaceType string_to_surface(const std::string& s) {
    if (s == "stone") return SurfaceType::STONE;
    if (s == "carpet") return SurfaceType::CARPET;
    if (s == "metal") return SurfaceType::METAL;
    if (s == "grass") return SurfaceType::GRASS;
    if (s == "tile") return SurfaceType::TILE;
    if (s == "glass") return SurfaceType::GLASS;
    if (s == "leather_fabric") return SurfaceType::LEATHER_FABRIC;
    return SurfaceType::WOOD;
}

// Physical Acoustic Properties for Materials
struct AcousticMaterialProps {
    float reflectivity;         // 0.0 to 1.0 (fraction of energy reflected)
    float highFreqDamping;      // absorption of high frequencies on reflection
    float resonancePitch;       // characteristic resonant frequency (Hz)
    float transmission;         // 0.0 to 1.0 (acoustic energy passing through when occluded)
    float transmissionMuffle;   // low-pass filter factor through the material (0.0 bright, 1.0 heavy muffle)
};

inline AcousticMaterialProps get_material_acoustics(SurfaceType mat) {
    switch (mat) {
        case SurfaceType::GLASS:
            return {0.96f, 0.05f, 3200.0f, 0.70f, 0.25f}; // Thin glass lets high audio pass
        case SurfaceType::METAL:
            return {0.92f, 0.08f, 2800.0f, 0.18f, 0.85f}; // Heavy metal blocks audio strongly
        case SurfaceType::TILE:
            return {0.90f, 0.12f, 2400.0f, 0.22f, 0.80f}; // Ceramic tile
        case SurfaceType::STONE:
            return {0.80f, 0.20f, 2100.0f, 0.12f, 0.90f}; // Dense stone wall = heavy occlusion & muffle
        case SurfaceType::WOOD:
            return {0.58f, 0.40f, 1600.0f, 0.48f, 0.55f}; // Wood walls let low/mid audio pass
        case SurfaceType::CARPET:
            return {0.18f, 0.85f, 900.0f, 0.55f, 0.60f};  // Heavy absorption
        case SurfaceType::LEATHER_FABRIC:
            return {0.22f, 0.80f, 1000.0f, 0.60f, 0.50f}; // Soft couch upholstery
        case SurfaceType::GRASS:
            return {0.15f, 0.90f, 800.0f, 0.80f, 0.30f};  // Open lawn / foliage
        default:
            return {0.60f, 0.35f, 1800.0f, 0.40f, 0.60f};
    }
}

// 3D Physical Object occupying space in the world (authoritative server sync supported)
struct WorldObject {
    int idIndex = 0;
    std::string id = "";
    std::string name;
    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f; // North/South horizontal bounds
    float min_z = 0.0f, max_z = 1.0f; // Vertical 3D altitude bounds
    float height = 1.0f;
    SurfaceType material = SurfaceType::WOOD;
    bool solidObstacle = true;
    bool destructible = true;
    bool regeneration = true;         // Fixed: only respawns if explicitly enabled
    int respawnTimeSeconds = 60;      // Fixed: configurable timer in seconds (not fixed to 1 min)
    int health = 100;
    int maxHealth = 100;
    bool isDestroyed = false;
    unsigned long long destroyedAtMs = 0;
    std::string soundAsset = "";
    float soundVolume = 0.8f;
    float soundRadius = 25.0f;        // 25 tiles hearing limit!

    bool contains(const Vector3& p) const {
        if (isDestroyed) return false;
        return (min_x <= p.x && p.x <= max_x &&
                min_y <= p.y && p.y <= max_y &&
                min_z <= p.z && p.z <= max_z);
    }

    Vector3 get_center() const {
        return Vector3((min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f, (min_z + max_z) * 0.5f);
    }

    void take_damage(int amount, unsigned long long nowMs = 0) {
        if (isDestroyed || !destructible) return;
        health -= amount;
        if (health <= 0) {
            health = 0;
            isDestroyed = true;
            solidObstacle = false;
            if (regeneration && respawnTimeSeconds > 0) {
                destroyedAtMs = nowMs;
            } else {
                destroyedAtMs = 0;
            }
        }
    }

    bool check_regeneration(unsigned long long nowMs) {
        if (!regeneration || respawnTimeSeconds <= 0 || destroyedAtMs == 0) return false;
        unsigned long long delayMs = (unsigned long long)respawnTimeSeconds * 1000ULL;
        if (isDestroyed && (nowMs - destroyedAtMs >= delayMs)) {
            isDestroyed = false;
            health = maxHealth;
            solidObstacle = true;
            destroyedAtMs = 0;
            return true;
        }
        return false;
    }
};

// Represents a 2D line segment obstacle in XY horizontal ground plane
struct WallSegment {
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    std::string name;
    SurfaceType material = SurfaceType::WOOD;
    float reflectivity = 0.60f;
    bool isDoorwayOpening = false;
    int linkedObjectId = -1; // -1 if static wall, >= 0 if linked to a WorldObject
};

struct SurfaceZone {
    std::string name;
    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f;
    SurfaceType surface = SurfaceType::WOOD;

    bool contains(const Vector3& p) const {
        return (min_x <= p.x && p.x <= max_x && min_y <= p.y && p.y <= max_y);
    }
};

struct RoomRegion {
    std::string id;
    std::string name;
    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f;
    float reverb_rt60 = 0.35f;
    bool isOutside = false;
    std::string ambienceCategory = "indoor";
    std::string ambienceAsset = "";
    float ambienceVolume = 0.5f;
};

// Travel Point: Moves avatar to child map (city, district, structure interior)
struct TravelPoint {
    std::string id;
    std::string name;
    float trigger_x = 0.0f;
    float trigger_y = 0.0f;
    float radius = 1.2f;
    std::string target_map = "default_world.json";
    Vector3 target_spawn{0.0f, 0.0f, 0.0f};

    bool is_triggered(const Vector3& p) const {
        float dx = p.x - trigger_x;
        float dy = p.y - trigger_y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }
};

// NPC Spawner definition for map instances
struct SpawnerConfig {
    std::string id;
    std::string entityType = "npc";
    Vector3 position;
    float interval = 10.0f;
    int maxCount = 1;
    float wanderRadius = 5.0f;
    std::string npcName = "Traveler";
    std::string soundAsset = "footsteps/wood_1.wav";
    std::string foleyAsset = "foley/regular/regular_1.wav";
    std::string voiceSound = "auras/hum.wav";
    std::string clothing = "regular";
    std::string dialogue = "Greetings, traveler.";
    std::string scriptName = "default_idle";
};

// Active Simulated NPC in the client
struct ActiveNPC {
    std::string id;
    std::string name;
    Vector3 position;
    float yaw = 0.0f;
    Vector3 homePosition;
    float wanderRadius = 5.0f;
    std::string soundAsset = "footsteps/wood_1.wav";
    std::string foleyAsset = "foley/regular/regular_1.wav";
    std::string voiceSound = "auras/hum.wav";
    std::string clothing = "regular";
    std::string dialogue = "Hello there.";
    bool isMoving = false;
    unsigned long long lastStepMs = 0;
};

// Remote Player representation as a physical 1-tile obstacle
struct RemotePlayerObstacle {
    std::string playerId;
    std::string username;
    Vector3 position;
    float tileSize = 1.0f; // Takes up 1 full tile!
    ClothingType clothing = ClothingType::REGULAR;
};

// Spatial detection descriptor for accessible radar & inspection queries
struct NearbyItemInfo {
    std::string id;
    std::string name;
    std::string category; // "object", "npc", "structure", "travel"
    float distance = 0.0f;
    std::string clockDirection; // e.g. "ahead (12 o'clock)", "3 o'clock", "behind (6 o'clock)"
    std::string details;
    Vector3 position;
};

inline std::string calculate_clock_direction(float playerYaw, const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    if (std::sqrt(dx * dx + dy * dy) < 0.2f) {
        return "at your feet";
    }
    float targetAngleDeg = std::atan2(dx, dy) * 180.0f / (float)M_PI;
    if (targetAngleDeg < 0.0f) targetAngleDeg += 360.0f;

    float relAngle = std::fmod((targetAngleDeg - playerYaw + 540.0f), 360.0f) - 180.0f;

    if (relAngle > -15.0f && relAngle <= 15.0f) return "ahead (12 o'clock)";
    if (relAngle > 15.0f && relAngle <= 45.0f) return "1 o'clock";
    if (relAngle > 45.0f && relAngle <= 75.0f) return "2 o'clock";
    if (relAngle > 75.0f && relAngle <= 105.0f) return "3 o'clock (right)";
    if (relAngle > 105.0f && relAngle <= 135.0f) return "4 o'clock";
    if (relAngle > 135.0f && relAngle <= 165.0f) return "5 o'clock";
    if (relAngle > 165.0f || relAngle <= -165.0f) return "behind (6 o'clock)";
    if (relAngle > -165.0f && relAngle <= -135.0f) return "7 o'clock";
    if (relAngle > -135.0f && relAngle <= -105.0f) return "8 o'clock";
    if (relAngle > -105.0f && relAngle <= -75.0f) return "9 o'clock (left)";
    if (relAngle > -75.0f && relAngle <= -45.0f) return "10 o'clock";
    return "11 o'clock";
}

// 3D Jump Controller with accessory multiplier hooks
struct JumpController {
    bool isJumping = false;
    unsigned long long jumpStartTime = 0;
    unsigned long long jumpDurationMs = 600; // 600ms natural parabolic jump
    float baseJumpHeight = 3.0f;             // Default 3 tiles vertical height!
    float accessoryMultiplier = 1.0f;        // Ready for boots, springs, gravity mods

    void startJump(unsigned long long nowMs) {
        if (isJumping) return;
        isJumping = true;
        jumpStartTime = nowMs;
    }

    float update(unsigned long long nowMs, bool& outLanded) {
        outLanded = false;
        if (!isJumping) return 0.0f;

        unsigned long long elapsed = nowMs - jumpStartTime;
        if (elapsed >= jumpDurationMs) {
            isJumping = false;
            outLanded = true;
            return 0.0f;
        }

        // Parabolic arc: 4 * h * t * (1 - t)
        float t = (float)elapsed / (float)jumpDurationMs;
        float h = (baseJumpHeight * accessoryMultiplier) * 4.0f * t * (1.0f - t);
        return (std::max)(0.0f, h);
    }
};

struct WorldMap {
    std::string title = "Lounge & Great Outdoor Field";
    Vector3 spawn_point{0.0f, -2.5f, 0.0f};
    std::vector<WallSegment> walls;
    std::vector<SurfaceZone> surface_zones;
    std::vector<WorldObject> objects;
    std::vector<RoomRegion> regions;
    std::vector<TravelPoint> travel_points;
    std::vector<SpawnerConfig> spawners;
    std::vector<ActiveNPC> active_npcs;

    SurfaceType get_surface_at(const Vector3& p) const {
        for (const auto& z : surface_zones) {
            if (z.contains(p)) return z.surface;
        }
        return SurfaceType::GRASS;
    }

    const RoomRegion* get_region_at(const Vector3& p) const {
        for (const auto& r : regions) {
            if (p.x >= r.min_x && p.x <= r.max_x && p.y >= r.min_y && p.y <= r.max_y) return &r;
        }
        return nullptr;
    }

    const TravelPoint* get_triggered_travel_point(const Vector3& p) const {
        for (const auto& tp : travel_points) {
            if (tp.is_triggered(p)) return &tp;
        }
        return nullptr;
    }

    const WorldObject* get_nearby_object(const Vector3& p, float threshold = 1.0f) const {
        for (const auto& obj : objects) {
            if (obj.isDestroyed) continue;
            float cx = (obj.min_x + obj.max_x) * 0.5f;
            float cy = (obj.min_y + obj.max_y) * 0.5f;
            float hx = (obj.max_x - obj.min_x) * 0.5f + threshold;
            float hy = (obj.max_y - obj.min_y) * 0.5f + threshold;
            if (std::abs(p.x - cx) <= hx && std::abs(p.y - cy) <= hy) {
                return &obj;
            }
        }
        return nullptr;
    }

    // Attacks the object directly in front of the avatar (Spacebar Attack)
    bool attack_object_in_front(
        const Vector3& origin,
        float yawDeg,
        int damage,
        std::string& out_id,
        std::string& out_name,
        SurfaceType& out_mat,
        bool& out_destroyed,
        int& out_health_left,
        Vector3& out_impact_pos,
        unsigned long long nowMs = 0
    ) {
        float rad = yawDeg * (float)M_PI / 180.0f;
        Vector3 dir(std::sin(rad), std::cos(rad), 0.0f); // X=sin(yaw), Y=cos(yaw)
        float reach = 1.7f; // 1.7m bare hands reach

        int hitIndex = -1;
        float bestDist = reach;

        for (int i = 0; i < (int)objects.size(); ++i) {
            if (objects[i].isDestroyed) continue;
            float x1 = objects[i].min_x, x2 = objects[i].max_x;
            float y1 = objects[i].min_y, y2 = objects[i].max_y;

            float segT;
            if (intersect_ray_segment(origin.x, origin.y, dir.x, dir.y, x1, y1, x2, y1, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.y, dir.x, dir.y, x2, y1, x2, y2, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.y, dir.x, dir.y, x2, y2, x1, y2, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.y, dir.x, dir.y, x1, y2, x1, y1, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
        }

        if (hitIndex >= 0) {
            WorldObject& obj = objects[hitIndex];
            obj.take_damage(damage, nowMs);
            out_id = obj.id;
            out_name = obj.name;
            out_mat = obj.material;
            out_destroyed = obj.isDestroyed;
            out_health_left = obj.health;
            out_impact_pos = Vector3(origin.x + dir.x * bestDist, origin.y + dir.y * bestDist, origin.z);
            return true;
        }

        return false;
    }

    std::vector<NearbyItemInfo> get_nearby_objects(const Vector3& origin, float yawDeg, float maxRange = 25.0f) const {
        std::vector<NearbyItemInfo> list;
        for (const auto& obj : objects) {
            Vector3 center = obj.get_center();
            float dist = origin.distance_xy(center);
            if (dist <= maxRange) {
                NearbyItemInfo item;
                item.id = obj.id;
                item.name = obj.name;
                item.category = "object";
                item.distance = dist;
                item.clockDirection = calculate_clock_direction(yawDeg, origin, center);
                item.position = center;
                if (obj.isDestroyed) {
                    item.details = surface_to_string(obj.material) + " (Destroyed)";
                } else {
                    item.details = surface_to_string(obj.material) + " (" + std::to_string(obj.health) + "/" + std::to_string(obj.maxHealth) + " HP)";
                }
                list.push_back(item);
            }
        }
        std::sort(list.begin(), list.end(), [](const NearbyItemInfo& a, const NearbyItemInfo& b) {
            return a.distance < b.distance;
        });
        return list;
    }

    std::vector<NearbyItemInfo> get_nearby_npcs(const Vector3& origin, float yawDeg, const std::vector<ActiveNPC>& npcs, float maxRange = 25.0f) const {
        std::vector<NearbyItemInfo> list;
        for (const auto& n : npcs) {
            float dist = origin.distance_xy(n.position);
            if (dist <= maxRange) {
                NearbyItemInfo item;
                item.id = n.id;
                item.name = n.name;
                item.category = "npc";
                item.distance = dist;
                item.clockDirection = calculate_clock_direction(yawDeg, origin, n.position);
                item.position = n.position;
                item.details = n.clothing + " clothing";
                if (!n.dialogue.empty()) {
                    std::string d = n.dialogue;
                    if (d.length() > 32) d = d.substr(0, 32) + "...";
                    item.details += " - \"" + d + "\"";
                }
                list.push_back(item);
            }
        }
        std::sort(list.begin(), list.end(), [](const NearbyItemInfo& a, const NearbyItemInfo& b) {
            return a.distance < b.distance;
        });
        return list;
    }

    std::vector<NearbyItemInfo> get_nearby_structures(const Vector3& origin, float yawDeg, float maxRange = 60.0f) const {
        std::vector<NearbyItemInfo> list;
        for (const auto& tp : travel_points) {
            Vector3 pos(tp.trigger_x, tp.trigger_y, 0.0f);
            float dist = origin.distance_xy(pos);
            if (dist <= maxRange) {
                NearbyItemInfo item;
                item.id = tp.id;
                item.name = tp.name;
                item.category = "travel";
                item.distance = dist;
                item.clockDirection = calculate_clock_direction(yawDeg, origin, pos);
                item.position = pos;
                item.details = "Gateway to " + tp.target_map;
                list.push_back(item);
            }
        }
        for (const auto& r : regions) {
            Vector3 center((r.min_x + r.max_x) * 0.5f, (r.min_y + r.max_y) * 0.5f, 0.0f);
            float dist = origin.distance_xy(center);
            if (dist <= maxRange) {
                NearbyItemInfo item;
                item.id = r.id;
                item.name = r.name;
                item.category = "structure";
                item.distance = dist;
                item.clockDirection = calculate_clock_direction(yawDeg, origin, center);
                item.position = center;
                item.details = r.isOutside ? "Outdoor Area" : "Indoor Structure";
                list.push_back(item);
            }
        }
        std::sort(list.begin(), list.end(), [](const NearbyItemInfo& a, const NearbyItemInfo& b) {
            return a.distance < b.distance;
        });
        return list;
    }

    bool update_regenerations(unsigned long long nowMs, std::vector<std::string>& out_respawned_names) {
        bool any = false;
        for (auto& obj : objects) {
            if (obj.check_regeneration(nowMs)) {
                out_respawned_names.push_back(obj.name);
                any = true;
            }
        }
        return any;
    }

    bool is_object_destroyed(int objId) const {
        if (objId < 0 || objId >= (int)objects.size()) return false;
        return objects[objId].isDestroyed;
    }

    // 2D Line segment intersection helper in horizontal XY plane
    static bool intersect_ray_segment(
        float ox, float oy, float dx, float dy,
        float x1, float y1, float x2, float y2,
        float& out_t
    ) {
        float wx = x2 - x1;
        float wy = y2 - y1;
        float denom = dx * wy - dy * wx;
        if (std::abs(denom) < 1e-6f) return false;

        float qx = x1 - ox;
        float qy = y1 - oy;
        float t = (qx * wy - qy * wx) / denom;
        float u = (qx * dy - qy * dx) / denom;

        if (t > 0.05f && u >= 0.0f && u <= 1.0f) {
            out_t = t;
            return true;
        }
        return false;
    }

    // Raycast against walls and furniture/field objects
    float cast_ray(
        const Vector3& origin,
        const Vector3& dir,
        float max_dist,
        SurfaceType& out_material,
        float& out_reflectivity,
        std::string& out_hit_name
    ) const {
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-5f) return max_dist;
        float dx = dir.x / len, dy = dir.y / len;
        float best_t = max_dist;

        for (const auto& w : walls) {
            if (w.isDoorwayOpening) continue;
            if (w.linkedObjectId >= 0 && is_object_destroyed(w.linkedObjectId)) continue;

            float t = 0.0f;
            if (intersect_ray_segment(origin.x, origin.y, dx, dy, w.x1, w.y1, w.x2, w.y2, t)) {
                if (t < best_t) {
                    best_t = t;
                    out_material = w.material;
                    out_reflectivity = w.reflectivity;
                    out_hit_name = w.name;
                }
            }
        }

        // Raycast against Active NPCs (acoustically solid, clothing-material reflection)
        for (const auto& npc : active_npcs) {
            float nx1 = npc.position.x - 0.5f;
            float nx2 = npc.position.x + 0.5f;
            float ny1 = npc.position.y - 0.5f;
            float ny2 = npc.position.y + 0.5f;

            float t = 0.0f;
            SurfaceType npcMat = SurfaceType::LEATHER_FABRIC;
            if (npc.clothing == "armor") npcMat = SurfaceType::METAL;
            float refl = (npcMat == SurfaceType::METAL) ? 0.90f : 0.45f;

            if (intersect_ray_segment(origin.x, origin.y, dx, dy, nx1, ny1, nx2, ny1, t) ||
                intersect_ray_segment(origin.x, origin.y, dx, dy, nx2, ny1, nx2, ny2, t) ||
                intersect_ray_segment(origin.x, origin.y, dx, dy, nx2, ny2, nx1, ny2, t) ||
                intersect_ray_segment(origin.x, origin.y, dx, dy, nx1, ny2, nx1, ny1, t)) {
                if (t < best_t) {
                    best_t = t;
                    out_material = npcMat;
                    out_reflectivity = refl;
                    out_hit_name = "NPC " + npc.name;
                }
            }
        }

        return best_t;
    }

    // Physical Movement check against walls, solid objects, AND other players (1-tile footprint)
    Vector3 try_move(
        const Vector3& from,
        const Vector3& to,
        bool& out_collided,
        std::string& out_obstacle_name,
        const std::vector<RemotePlayerObstacle>& otherPlayers = {}
    ) const {
        out_collided = false;
        out_obstacle_name = "";

        float move_dx = to.x - from.x;
        float move_dy = to.y - from.y;
        float move_dist = std::sqrt(move_dx * move_dx + move_dy * move_dy);
        if (move_dist < 1e-5f) return from;

        float dir_x = move_dx / move_dist;
        float dir_y = move_dy / move_dist;

        float best_t = move_dist + 1.0f;
        Vector3 best_pos = to;

        // 1. Collision with Walls and Static/Dynamic Map Objects
        for (const auto& w : walls) {
            if (w.isDoorwayOpening) continue;
            if (w.linkedObjectId >= 0 && is_object_destroyed(w.linkedObjectId)) continue;

            float t = 0.0f;
            if (intersect_ray_segment(from.x, from.y, dir_x, dir_y, w.x1, w.y1, w.x2, w.y2, t)) {
                if (t <= move_dist + 0.35f) { // 35cm avatar collision margin
                    if (t < best_t) {
                        best_t = t;
                        out_collided = true;
                        out_obstacle_name = w.name;
                        float safe_dist = (std::max)(0.0f, t - 0.35f);
                        best_pos = Vector3(from.x + dir_x * safe_dist, from.y + dir_y * safe_dist, to.z);
                    }
                }
            }
        }

        // 2. Physical Collision with other Connected Players (1 Tile = 1.0m Box Obstacle)
        for (const auto& rp : otherPlayers) {
            float px1 = rp.position.x - 0.5f;
            float px2 = rp.position.x + 0.5f;
            float py1 = rp.position.y - 0.5f;
            float py2 = rp.position.y + 0.5f;

            // Check if destination enters the other player's 1-tile body box
            if (to.x >= px1 && to.x <= px2 && to.y >= py1 && to.y <= py2) {
                out_collided = true;
                out_obstacle_name = "Player " + rp.username;
                return from; // Block completely at the boundary of their tile
            }

            // Ray segment check during movement sweep
            float segT = 0.0f;
            if ((intersect_ray_segment(from.x, from.y, dir_x, dir_y, px1, py1, px2, py1, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, px2, py1, px2, py2, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, px2, py2, px1, py2, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, px1, py2, px1, py1, segT)) && segT <= move_dist + 0.40f) {
                if (segT < best_t) {
                    best_t = segT;
                    out_collided = true;
                    out_obstacle_name = "Player " + rp.username;
                    float safe_dist = (std::max)(0.0f, segT - 0.40f);
                    best_pos = Vector3(from.x + dir_x * safe_dist, from.y + dir_y * safe_dist, to.z);
                }
            }
        }

        // 3. Physical Collision with Active NPCs (1 Tile = 1.0m Box Obstacle)
        for (const auto& npc : active_npcs) {
            float nx1 = npc.position.x - 0.5f;
            float nx2 = npc.position.x + 0.5f;
            float ny1 = npc.position.y - 0.5f;
            float ny2 = npc.position.y + 0.5f;

            if (to.x >= nx1 && to.x <= nx2 && to.y >= ny1 && to.y <= ny2) {
                out_collided = true;
                out_obstacle_name = "NPC " + npc.name;
                return from; // Block completely at the boundary of their tile
            }

            float segT = 0.0f;
            if ((intersect_ray_segment(from.x, from.y, dir_x, dir_y, nx1, ny1, nx2, ny1, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, nx2, ny1, nx2, ny2, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, nx2, ny2, nx1, ny2, segT) ||
                 intersect_ray_segment(from.x, from.y, dir_x, dir_y, nx1, ny2, nx1, ny1, segT)) && segT <= move_dist + 0.40f) {
                if (segT < best_t) {
                    best_t = segT;
                    out_collided = true;
                    out_obstacle_name = "NPC " + npc.name;
                    float safe_dist = (std::max)(0.0f, segT - 0.40f);
                    best_pos = Vector3(from.x + dir_x * safe_dist, from.y + dir_y * safe_dist, to.z);
                }
            }
        }

        return best_pos;
    }

    // Material-based Acoustic Occlusion Calculation between sound source and listener
    // Returns transmission factor (0.0=silent/blocked, 1.0=clear line-of-sight) and lowPassMuffle (0.0 to 1.0)
    void calculate_occlusion(
        const Vector3& listenerPos,
        const Vector3& sourcePos,
        float& out_transmission,
        float& out_lowPassMuffle
    ) const {
        out_transmission = 1.0f;
        out_lowPassMuffle = 0.0f;

        float dx = sourcePos.x - listenerPos.x;
        float dy = sourcePos.y - listenerPos.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.6f) return; // Very close: clear line-of-sight, skip raycasts
        if (dist >= 25.0f) {
            out_transmission = 0.0f;
            return;
        }

        float dir_x = dx / dist;
        float dir_y = dy / dist;

        float min_x = (std::min)(listenerPos.x, sourcePos.x) - 0.1f;
        float max_x = (std::max)(listenerPos.x, sourcePos.x) + 0.1f;
        float min_y = (std::min)(listenerPos.y, sourcePos.y) - 0.1f;
        float max_y = (std::max)(listenerPos.y, sourcePos.y) + 0.1f;

        // Trace ray between listener and source against all solid walls and objects
        for (const auto& w : walls) {
            if (w.isDoorwayOpening) continue;
            if (w.linkedObjectId >= 0 && is_object_destroyed(w.linkedObjectId)) continue;

            // Bounding box rejection: if wall is completely outside ray bounding box, skip!
            float w_min_x = (std::min)(w.x1, w.x2);
            float w_max_x = (std::max)(w.x1, w.x2);
            float w_min_y = (std::min)(w.y1, w.y2);
            float w_max_y = (std::max)(w.y1, w.y2);
            if (w_max_x < min_x || w_min_x > max_x || w_min_y > max_y || w_max_y < min_y) continue;

            float t = 0.0f;
            if (intersect_ray_segment(listenerPos.x, listenerPos.y, dir_x, dir_y, w.x1, w.y1, w.x2, w.y2, t)) {
                if (t > 0.1f && t < dist - 0.1f) {
                    auto props = get_material_acoustics(w.material);
                    out_transmission *= props.transmission;
                    out_lowPassMuffle = (std::max)(out_lowPassMuffle, props.transmissionMuffle);
                }
            }
        }

        // Clamp minimum transmission so total silence doesn't happen unnaturally
        out_transmission = (std::max)(0.04f, (std::min)(1.0f, out_transmission));
    }

    // Dynamic Map Loader from JSON (supports both modern X,Y,Z and legacy maps)
    bool load_from_json(const json& j) {
        try {
            title = j.value("title", "Fake-VR World");
            if (j.contains("spawn_point") && j["spawn_point"].is_object()) {
                spawn_point.x = j["spawn_point"].value("x", 0.0f);
                spawn_point.y = j["spawn_point"].value("y", j["spawn_point"].value("z", 0.0f));
                spawn_point.z = j["spawn_point"].value("z", 0.0f);
                if (j["spawn_point"].contains("y") && j["spawn_point"].contains("z")) {
                    spawn_point.y = j["spawn_point"].value("y", 0.0f);
                    spawn_point.z = j["spawn_point"].value("z", 0.0f);
                }
            }

            walls.clear();
            surface_zones.clear();
            objects.clear();
            regions.clear();
            travel_points.clear();
            spawners.clear();

            // Load regions
            if (j.contains("regions") && j["regions"].is_array()) {
                for (const auto& r : j["regions"]) {
                    RoomRegion reg;
                    reg.id = r.value("id", "region");
                    reg.name = r.value("name", "Area");
                    reg.min_x = r.value("min_x", -10.0f);
                    reg.max_x = r.value("max_x", 10.0f);
                    reg.min_y = r.value("min_y", r.value("min_z", -10.0f));
                    reg.max_y = r.value("max_y", r.value("max_z", 10.0f));
                    reg.reverb_rt60 = r.value("reverb_rt60", 0.35f);
                    reg.isOutside = r.value("is_outside", false);
                    reg.ambienceCategory = r.value("ambience_category", "indoor");
                    reg.ambienceAsset = r.value("ambience_asset", "");
                    reg.ambienceVolume = r.value("ambience_volume", 0.5f);
                    regions.push_back(reg);
                }
            }

            // Load surface zones
            if (j.contains("surface_zones") && j["surface_zones"].is_array()) {
                for (const auto& z : j["surface_zones"]) {
                    SurfaceZone sz;
                    sz.name = z.value("name", "Zone");
                    sz.min_x = z.value("min_x", 0.0f);
                    sz.max_x = z.value("max_x", 0.0f);
                    sz.min_y = z.value("min_y", z.value("min_z", 0.0f));
                    sz.max_y = z.value("max_y", z.value("max_z", 0.0f));
                    sz.surface = string_to_surface(z.value("surface", "wood"));
                    surface_zones.push_back(sz);
                }
            }

            int nextObjId = 0;
            auto add_box_object = [&](
                const std::string& rawId,
                const std::string& name,
                float x1, float x2, float y1, float y2, float h,
                SurfaceType mat, int hp, bool canDestroy, bool regen, int regenSec,
                const std::string& soundAsset = "", float soundVol = 0.8f, float sndRadius = 25.0f,
                const std::string& prefix = "obj",
                bool isDestroyed = false
            ) {
                WorldObject obj;
                obj.idIndex = nextObjId++;
                if (!rawId.empty()) {
                    obj.id = rawId;
                } else {
                    std::string cleanName;
                    for (char c : name) {
                        cleanName += (isalnum(c) ? (char)tolower(c) : '_');
                    }
                    obj.id = prefix + "_" + cleanName + "_" + std::to_string(int(x1)) + "_" + std::to_string(int(y1));
                }
                obj.name = name;
                obj.min_x = x1; obj.max_x = x2;
                obj.min_y = y1; obj.max_y = y2;
                obj.min_z = 0.0f; obj.max_z = h;
                obj.height = h;
                obj.material = mat;
                obj.health = hp;
                obj.maxHealth = (hp > 0 ? hp : 100);
                obj.destructible = canDestroy;
                obj.regeneration = regen;
                obj.respawnTimeSeconds = regenSec;
                obj.isDestroyed = isDestroyed || (hp <= 0);
                obj.solidObstacle = !obj.isDestroyed;
                obj.soundAsset = soundAsset;
                obj.soundVolume = soundVol;
                obj.soundRadius = sndRadius;
                objects.push_back(obj);

                auto props = get_material_acoustics(mat);
                walls.push_back({x1, y1, x2, y1, name, mat, props.reflectivity, false, obj.idIndex});
                walls.push_back({x2, y1, x2, y2, name, mat, props.reflectivity, false, obj.idIndex});
                walls.push_back({x2, y2, x1, y2, name, mat, props.reflectivity, false, obj.idIndex});
                walls.push_back({x1, y2, x1, y1, name, mat, props.reflectivity, false, obj.idIndex});
            };

            // Load travel points (gateways to child maps/structures)
            if (j.contains("travel_points") && j["travel_points"].is_array()) {
                for (const auto& tp : j["travel_points"]) {
                    TravelPoint t;
                    t.id = tp.value("id", "travel");
                    t.name = tp.value("name", "Travel Point");
                    t.trigger_x = tp.value("trigger_x", tp.value("x", 0.0f));
                    t.trigger_y = tp.value("trigger_y", tp.value("y", tp.value("z", 0.0f)));
                    t.radius = tp.value("radius", 1.2f);
                    t.target_map = tp.value("target_map", "default_world.json");
                    if (tp.contains("target_spawn") && tp["target_spawn"].is_object()) {
                        t.target_spawn.x = tp["target_spawn"].value("x", 0.0f);
                        t.target_spawn.y = tp["target_spawn"].value("y", tp["target_spawn"].value("z", 0.0f));
                        t.target_spawn.z = tp["target_spawn"].value("z", 0.0f);
                    }
                    travel_points.push_back(t);
                }
            }

            // Load spawners (NPCs and dynamic entities)
            if (j.contains("spawners") && j["spawners"].is_array()) {
                for (const auto& sp : j["spawners"]) {
                    SpawnerConfig sc;
                    sc.id = sp.value("id", "spawner");
                    sc.entityType = sp.value("entity_type", "npc");
                    sc.position.x = sp.value("x", 0.0f);
                    sc.position.y = sp.value("y", sp.value("z", 0.0f));
                    sc.position.z = sp.value("z", 0.0f);
                    sc.interval = sp.value("interval", 10.0f);
                    sc.maxCount = sp.value("max_count", 1);
                    sc.wanderRadius = sp.value("wander_radius", 5.0f);
                    if (sp.contains("npc_config") && sp["npc_config"].is_object()) {
                        const auto& cfg = sp["npc_config"];
                        sc.npcName = cfg.value("name", "Traveler");
                        sc.soundAsset = cfg.value("sound_asset", "footsteps/wood_1.wav");
                        sc.foleyAsset = cfg.value("foley_asset", "foley/regular/regular_1.wav");
                        sc.voiceSound = cfg.value("voice_sound", "auras/hum.wav");
                        sc.clothing = cfg.value("clothing", "regular");
                        sc.dialogue = cfg.value("dialogue", "Greetings, traveler.");
                        sc.scriptName = cfg.value("script_name", "default_idle");
                    }
                    spawners.push_back(sc);
                }

                // Materialize spawners into active local NPCs (offline/standalone fallback)
                active_npcs.clear();
                for (const auto& sp : spawners) {
                    if (sp.entityType == "npc" || sp.entityType == "creature") {
                        ActiveNPC npc;
                        npc.id = sp.id;
                        npc.name = sp.npcName.empty() ? "Traveler" : sp.npcName;
                        npc.position = sp.position;
                        npc.homePosition = sp.position;
                        npc.yaw = 0.0f;
                        npc.wanderRadius = sp.wanderRadius;
                        npc.soundAsset = sp.soundAsset;
                        npc.foleyAsset = sp.foleyAsset;
                        npc.voiceSound = sp.voiceSound;
                        npc.clothing = sp.clothing;
                        npc.dialogue = sp.dialogue;
                        active_npcs.push_back(npc);
                    }
                }
            }

            // Load nested structures (e.g. house or shop entities)
            if (j.contains("structures") && j["structures"].is_array()) {
                for (const auto& s : j["structures"]) {
                    if (s.contains("walls") && s["walls"].is_array()) {
                        for (const auto& w : s["walls"]) {
                            WallSegment ws;
                            ws.x1 = w.value("x1", 0.0f);
                            ws.y1 = w.value("y1", w.value("z1", 0.0f));
                            ws.x2 = w.value("x2", 0.0f);
                            ws.y2 = w.value("y2", w.value("z2", 0.0f));
                            ws.name = w.value("name", "Wall");
                            ws.material = string_to_surface(w.value("material", "wood"));
                            ws.reflectivity = w.value("reflectivity", 0.60f);
                            ws.isDoorwayOpening = w.value("is_doorway", false);
                            ws.linkedObjectId = -1;
                            walls.push_back(ws);
                        }
                    }

                    if (s.contains("interior_objects") && s["interior_objects"].is_array()) {
                        for (const auto& o : s["interior_objects"]) {
                            std::string oid = o.value("id", "");
                            std::string oname = o.value("name", "Object");
                            float x1 = o.value("min_x", 0.0f), x2 = o.value("max_x", 0.0f);
                            float y1 = o.value("min_y", o.value("min_z", 0.0f));
                            float y2 = o.value("max_y", o.value("max_z", 0.0f));
                            float h = o.value("height", 1.0f);
                            SurfaceType mat = string_to_surface(o.value("material", "wood"));
                            int hp = o.value("health", 100);
                            bool canDestroy = o.value("destructible", true);
                            bool regen = o.value("regeneration", true);
                            int regenSec = o.value("respawn_time_seconds", 60);
                            std::string snd = o.value("sound_asset", "");
                            float svol = o.value("sound_volume", 0.8f);
                            float srad = o.value("sound_radius", 25.0f);
                            bool isDestroyed = o.value("is_destroyed", false);
                            if (hp <= 0) isDestroyed = true;
                            add_box_object(oid, oname, x1, x2, y1, y2, h, mat, hp, canDestroy, regen, regenSec, snd, svol, srad, "struct", isDestroyed);
                        }
                    }
                }
            }

            // Load outdoor field objects
            if (j.contains("field_objects") && j["field_objects"].is_array()) {
                for (const auto& o : j["field_objects"]) {
                    std::string oid = o.value("id", "");
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f), x2 = o.value("max_x", 0.0f);
                    float y1 = o.value("min_y", o.value("min_z", 0.0f));
                    float y2 = o.value("max_y", o.value("max_z", 0.0f));
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    bool regen = o.value("regeneration", true);
                    int regenSec = o.value("respawn_time_seconds", 60);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    float srad = o.value("sound_radius", 25.0f);
                    bool isDestroyed = o.value("is_destroyed", false);
                    if (hp <= 0) isDestroyed = true;
                    add_box_object(oid, oname, x1, x2, y1, y2, h, mat, hp, canDestroy, regen, regenSec, snd, svol, srad, "field", isDestroyed);
                }
            }

            // Load direct/generic objects list
            if (j.contains("objects") && j["objects"].is_array()) {
                for (const auto& o : j["objects"]) {
                    std::string oid = o.value("id", "");
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f), x2 = o.value("max_x", 0.0f);
                    float y1 = o.value("min_y", o.value("min_z", 0.0f));
                    float y2 = o.value("max_y", o.value("max_z", 0.0f));
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    bool regen = o.value("regeneration", true);
                    int regenSec = o.value("respawn_time_seconds", 60);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    float srad = o.value("sound_radius", 25.0f);
                    bool isDestroyed = o.value("is_destroyed", false);
                    if (hp <= 0) isDestroyed = true;
                    add_box_object(oid, oname, x1, x2, y1, y2, h, mat, hp, canDestroy, regen, regenSec, snd, svol, srad, "obj", isDestroyed);
                }
            }

            // Load boundary walls
            if (j.contains("boundary_walls") && j["boundary_walls"].is_array()) {
                for (const auto& w : j["boundary_walls"]) {
                    WallSegment ws;
                    ws.x1 = w.value("x1", 0.0f);
                    ws.y1 = w.value("y1", w.value("z1", 0.0f));
                    ws.x2 = w.value("x2", 0.0f);
                    ws.y2 = w.value("y2", w.value("z2", 0.0f));
                    ws.name = w.value("name", "Boundary");
                    ws.material = string_to_surface(w.value("material", "wood"));
                    ws.reflectivity = w.value("reflectivity", 0.60f);
                    ws.isDoorwayOpening = w.value("is_doorway", false);
                    ws.linkedObjectId = -1;
                    walls.push_back(ws);
                }
            }

            return true;
        } catch (...) {
            return false;
        }
    }

    bool load_from_json_file(const std::string& jsonPath) {
        std::ifstream file(jsonPath);
        if (!file.is_open()) return false;
        try {
            json j;
            file >> j;
            return load_from_json(j);
        } catch (...) {
            return false;
        }
    }
};

inline WorldMap create_realistic_lounge() {
    WorldMap m;
    m.title = "Lounge & Great Outdoor Field";
    m.spawn_point = Vector3(0.0f, -2.5f, 0.0f);

    m.regions.push_back({"lounge", "Living Room Lounge", -3.5f, 3.5f, -4.0f, 4.0f, 0.40f, false});
    m.regions.push_back({"outside", "Great Outdoor Field", -50.0f, 50.0f, 4.0f, 104.0f, 0.15f, true});

    m.surface_zones.push_back({"Living Room Wood Floor", -3.5f, 3.5f, -4.0f, 4.0f, SurfaceType::WOOD});
    m.surface_zones.push_back({"Plush Rug Under Couch & Table", -2.0f, 2.0f, -1.8f, 1.8f, SurfaceType::CARPET});
    m.surface_zones.push_back({"Outdoor Grass Field", -50.0f, 50.0f, 4.0f, 104.0f, SurfaceType::GRASS});

    int nextObjId = 0;
    auto add_box_object = [&](const std::string& name, float x1, float x2, float y1, float y2, float h, SurfaceType mat, int hp = 100, bool canDestroy = true, int regenSec = 60) {
        WorldObject obj;
        obj.idIndex = nextObjId++;
        obj.id = "lounge_obj_" + std::to_string(obj.idIndex);
        obj.name = name;
        obj.min_x = x1; obj.max_x = x2;
        obj.min_y = y1; obj.max_y = y2;
        obj.min_z = 0.0f; obj.max_z = h;
        obj.height = h;
        obj.material = mat;
        obj.health = hp;
        obj.maxHealth = hp;
        obj.destructible = canDestroy;
        obj.regeneration = true;
        obj.respawnTimeSeconds = regenSec;
        m.objects.push_back(obj);

        auto props = get_material_acoustics(mat);
        m.walls.push_back({x1, y1, x2, y1, name, mat, props.reflectivity, false, obj.idIndex});
        m.walls.push_back({x2, y1, x2, y2, name, mat, props.reflectivity, false, obj.idIndex});
        m.walls.push_back({x2, y2, x1, y2, name, mat, props.reflectivity, false, obj.idIndex});
        m.walls.push_back({x1, y2, x1, y1, name, mat, props.reflectivity, false, obj.idIndex});
    };

    // Lounge furniture
    add_box_object("Fabric Sofa Couch", -3.2f, -1.4f, -1.0f, 1.0f, 0.85f, SurfaceType::LEATHER_FABRIC, 80, true, 45);
    add_box_object("Solid Oak Coffee Table", -1.1f, 0.1f, -0.5f, 0.5f, 0.45f, SurfaceType::WOOD, 100, true, 60);
    add_box_object("Metal Equipment Rack", 1.8f, 2.8f, -3.4f, -2.4f, 1.80f, SurfaceType::METAL, 250, true, 90);
    add_box_object("Wooden Bookcase", 2.5f, 3.2f, -1.5f, 1.5f, 2.10f, SurfaceType::WOOD, 120, true, 60);
    add_box_object("Glass Display Cabinet", 1.8f, 2.8f, 2.2f, 3.2f, 1.70f, SurfaceType::GLASS, 40, true, 30);

    // Walls
    m.walls.push_back({-3.5f, -4.0f, 3.5f, -4.0f, "South Plaster Wall", SurfaceType::WOOD, 0.60f, false, -1});
    m.walls.push_back({3.5f, -4.0f, 3.5f, 4.0f, "East Wall", SurfaceType::STONE, 0.75f, false, -1});
    m.walls.push_back({-3.5f, 4.0f, -3.5f, -4.0f, "West Wall", SurfaceType::STONE, 0.75f, false, -1});
    m.walls.push_back({-3.5f, 4.0f, -1.0f, 4.0f, "North Wall", SurfaceType::WOOD, 0.60f, false, -1});
    m.walls.push_back({-1.0f, 4.0f, 1.0f, 4.0f, "Doorway to Outside Field", SurfaceType::WOOD, 0.0f, true, -1});
    m.walls.push_back({1.0f, 4.0f, 2.5f, 4.0f, "Glass Bay Window", SurfaceType::GLASS, 0.96f, false, -1});
    m.walls.push_back({2.5f, 4.0f, 3.5f, 4.0f, "North Wall", SurfaceType::WOOD, 0.60f, false, -1});

    m.walls.push_back({-50.0f, 4.0f, -3.5f, 4.0f, "Lounge Exterior North Wall (West)", SurfaceType::STONE, 0.80f, false, -1});
    m.walls.push_back({3.5f, 4.0f, 50.0f, 4.0f, "Lounge Exterior North Wall (East)", SurfaceType::STONE, 0.80f, false, -1});
    m.walls.push_back({-50.0f, 4.0f, -50.0f, 104.0f, "West Field Wooden Fence", SurfaceType::WOOD, 0.50f, false, -1});
    m.walls.push_back({50.0f, 4.0f, 50.0f, 104.0f, "East Field Stone Wall", SurfaceType::STONE, 0.75f, false, -1});
    m.walls.push_back({-50.0f, 104.0f, 50.0f, 104.0f, "North Field Tree Line Boundary", SurfaceType::GRASS, 0.25f, false, -1});

    // Field objects
    add_box_object("Wooden Park Bench", -1.2f, 1.2f, 7.5f, 8.3f, 0.9f, SurfaceType::WOOD, 100, true, 60);
    add_box_object("Steel Tool Shed", 12.0f, 16.0f, 16.0f, 20.0f, 2.5f, SurfaceType::METAL, 400, true, 120);
    add_box_object("Granite Boulder", -14.0f, -10.0f, 13.0f, 17.0f, 1.8f, SurfaceType::STONE, 600, true, 180);

    return m;
}

} // namespace fakevr
