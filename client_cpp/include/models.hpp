#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include "nlohmann/json.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fakevr {

using json = nlohmann::json;

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vector3() = default;
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    float distance_to(const Vector3& o) const {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float distance_xz(const Vector3& o) const {
        float dx = x - o.x, dz = z - o.z;
        return std::sqrt(dx * dx + dz * dz);
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

// Physical Acoustic Properties for Materials
struct AcousticMaterialProps {
    float reflectivity;      // 0.0 to 1.0 (1.0 = total reflection)
    float highFreqDamping;   // absorption of high frequencies (0.0 = bright, 1.0 = heavily damped)
    float resonancePitch;    // characteristic pitch shift for impulse return
};

inline AcousticMaterialProps get_material_acoustics(SurfaceType mat) {
    switch (mat) {
        case SurfaceType::GLASS:
            return {0.96f, 0.05f, 3200.0f}; // Ultra crisp, high glass resonance
        case SurfaceType::METAL:
            return {0.92f, 0.08f, 2800.0f}; // Bright metallic ping
        case SurfaceType::TILE:
            return {0.90f, 0.12f, 2400.0f}; // Clean hard ceramic
        case SurfaceType::STONE:
            return {0.80f, 0.20f, 2100.0f}; // Dense solid reflection
        case SurfaceType::WOOD:
            return {0.58f, 0.40f, 1600.0f}; // Warm organic woody reflection
        case SurfaceType::CARPET:
            return {0.18f, 0.85f, 900.0f};  // Heavy absorption, muffled low-mid return
        case SurfaceType::LEATHER_FABRIC:
            return {0.22f, 0.80f, 1000.0f}; // Soft couch / armchair dampening
        case SurfaceType::GRASS:
            return {0.15f, 0.90f, 800.0f};  // Highly absorbed open air lawn
        default:
            return {0.60f, 0.35f, 1800.0f};
    }
}

// 3D Physical Object occupying space in the world (destructible for future gameplay)
struct WorldObject {
    int id = 0;
    std::string name;
    float min_x, max_x;
    float min_z, max_z;
    float height = 1.0f;
    SurfaceType material = SurfaceType::WOOD;
    bool solidObstacle = true;
    bool destructible = true;
    int health = 100;
    int maxHealth = 100;
    bool isDestroyed = false;
    unsigned long long destroyedAtMs = 0; // Timestamp when destroyed for 1-minute respawn
    std::string soundAsset = "";          // Object's own sound emission (e.g. "objects/generator_hum.wav")
    float soundVolume = 0.8f;             // Volume of sound emission
    float soundRadius = 15.0f;            // Maximum audible distance in meters

    bool contains(const Vector3& p) const {
        if (isDestroyed) return false;
        return (min_x <= p.x && p.x <= max_x && min_z <= p.z && p.z <= max_z);
    }

    Vector3 get_center() const {
        return Vector3((min_x + max_x) * 0.5f, height * 0.5f, (min_z + max_z) * 0.5f);
    }

    void take_damage(int amount, unsigned long long nowMs = 0) {
        if (isDestroyed) return;
        health -= amount;
        if (health <= 0) {
            health = 0;
            isDestroyed = true;
            solidObstacle = false;
            destroyedAtMs = nowMs;
        }
    }

    bool check_regeneration(unsigned long long nowMs, unsigned long long regenDelayMs = 60000) {
        if (isDestroyed && destroyedAtMs > 0 && (nowMs - destroyedAtMs >= regenDelayMs)) {
            isDestroyed = false;
            health = maxHealth;
            solidObstacle = true;
            destroyedAtMs = 0;
            return true;
        }
        return false;
    }
};

struct SurfaceZone {
    std::string name;
    float min_x, max_x;
    float min_z, max_z;
    SurfaceType surface;

    bool contains(const Vector3& p) const {
        return (min_x <= p.x && p.x <= max_x && min_z <= p.z && p.z <= max_z);
    }
};

// Represents a 2D line segment obstacle in XZ plane (walls, object boundaries)
struct WallSegment {
    float x1, z1;
    float x2, z2;
    std::string name;
    SurfaceType material = SurfaceType::WOOD;
    float reflectivity = 0.60f;
    bool isDoorwayOpening = false;
    int linkedObjectId = -1; // -1 if static wall, >= 0 if linked to a WorldObject
};

struct RoomRegion {
    std::string id;
    std::string name;
    float min_x, max_x;
    float min_z, max_z;
    float reverb_rt60 = 0.40f;
    bool isOutside = false;
    std::string ambienceCategory = "indoor"; // "indoor", "outdoor", "nature", "urban"
    std::string ambienceAsset = "";          // specific wav inside ambiances/ or environment/
    float ambienceVolume = 0.5f;             // Volume of ambient bed
};

struct WorldMap {
    std::string title = "Lounge & Great Outdoor Field";
    Vector3 spawn_point{0.0f, 0.0f, -2.5f};
    std::vector<WallSegment> walls;
    std::vector<SurfaceZone> surface_zones;
    std::vector<WorldObject> objects;
    std::vector<RoomRegion> regions;

    SurfaceType get_surface_at(const Vector3& p) const {
        for (const auto& z : surface_zones) {
            if (z.contains(p)) return z.surface;
        }
        // Default outside field is grass!
        return SurfaceType::GRASS;
    }

    const RoomRegion* get_region_at(const Vector3& p) const {
        for (const auto& r : regions) {
            if (p.x >= r.min_x && p.x <= r.max_x && p.z >= r.min_z && p.z <= r.max_z) return &r;
        }
        return nullptr;
    }

    const WorldObject* get_nearby_object(const Vector3& p, float threshold = 1.0f) const {
        for (const auto& obj : objects) {
            if (obj.isDestroyed) continue;
            float cx = (obj.min_x + obj.max_x) * 0.5f;
            float cz = (obj.min_z + obj.max_z) * 0.5f;
            float hx = (obj.max_x - obj.min_x) * 0.5f + threshold;
            float hz = (obj.max_z - obj.min_z) * 0.5f + threshold;
            if (std::abs(p.x - cx) <= hx && std::abs(p.z - cz) <= hz) {
                return &obj;
            }
        }
        return nullptr;
    }

    // Attacks the object in front of the player (Spacebar Attack)
    // Bare hands deal 25 damage per hit.
    // Returns true if an object was struck.
    bool attack_object_in_front(
        const Vector3& origin,
        float yawDeg,
        int damage,
        std::string& out_name,
        SurfaceType& out_mat,
        bool& out_destroyed,
        int& out_health_left,
        Vector3& out_impact_pos,
        unsigned long long nowMs = 0
    ) {
        float rad = yawDeg * (float)M_PI / 180.0f;
        Vector3 dir(std::sin(rad), 0.0f, std::cos(rad));
        float reach = 1.7f; // 1.7m bare hands reach

        int hitIndex = -1;
        float bestDist = reach;

        for (int i = 0; i < (int)objects.size(); ++i) {
            if (objects[i].isDestroyed) continue;
            float x1 = objects[i].min_x, x2 = objects[i].max_x;
            float z1 = objects[i].min_z, z2 = objects[i].max_z;

            float segT;
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x1, z1, x2, z1, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x2, z1, x2, z2, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x2, z2, x1, z2, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x1, z2, x1, z1, segT) && segT < bestDist) { bestDist = segT; hitIndex = i; }
        }

        if (hitIndex >= 0) {
            WorldObject& obj = objects[hitIndex];
            obj.take_damage(damage, nowMs);
            out_name = obj.name;
            out_mat = obj.material;
            out_destroyed = obj.isDestroyed;
            out_health_left = obj.health;
            out_impact_pos = Vector3(origin.x + dir.x * bestDist, origin.y, origin.z + dir.z * bestDist);
            return true;
        }

        return false;
    }

    bool update_regenerations(unsigned long long nowMs, std::vector<std::string>& out_respawned_names) {
        bool any = false;
        for (auto& obj : objects) {
            if (obj.check_regeneration(nowMs, 60000)) { // 1-minute (60,000ms) respawn
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

    // Line segment intersection helper
    static bool intersect_ray_segment(
        float ox, float oz, float dx, float dz,
        float x1, float z1, float x2, float z2,
        float& out_t
    ) {
        float wx = x2 - x1;
        float wz = z2 - z1;
        float denom = dx * wz - dz * wx;
        if (std::abs(denom) < 1e-6f) return false;

        float qx = x1 - ox;
        float qz = z1 - oz;
        float t = (qx * wz - qz * wx) / denom;
        float u = (qx * dz - qz * dx) / denom;

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
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (len < 1e-5f) return max_dist;
        float dx = dir.x / len, dz = dir.z / len;
        float best_t = max_dist;

        for (const auto& w : walls) {
            if (w.isDoorwayOpening) continue; // Acoustic rays shoot straight through open doors!
            if (w.linkedObjectId >= 0 && is_object_destroyed(w.linkedObjectId)) continue;

            float t = 0.0f;
            if (intersect_ray_segment(origin.x, origin.z, dx, dz, w.x1, w.z1, w.x2, w.z2, t)) {
                if (t < best_t) {
                    best_t = t;
                    out_material = w.material;
                    out_reflectivity = w.reflectivity;
                    out_hit_name = w.name;
                }
            }
        }
        return best_t;
    }

    // Player collision movement check against walls and solid objects
    Vector3 try_move(const Vector3& from, const Vector3& to, bool& out_collided, std::string& out_obstacle_name) const {
        out_collided = false;
        out_obstacle_name = "";

        float move_dx = to.x - from.x;
        float move_dz = to.z - from.z;
        float move_dist = std::sqrt(move_dx * move_dx + move_dz * move_dz);
        if (move_dist < 1e-5f) return from;

        float dir_x = move_dx / move_dist;
        float dir_z = move_dz / move_dist;

        float best_t = move_dist + 1.0f;
        Vector3 best_pos = to;

        for (const auto& w : walls) {
            if (w.isDoorwayOpening) continue; // Walk freely through doorway!
            if (w.linkedObjectId >= 0 && is_object_destroyed(w.linkedObjectId)) continue;

            float t = 0.0f;
            if (intersect_ray_segment(from.x, from.z, dir_x, dir_z, w.x1, w.z1, w.x2, w.z2, t)) {
                if (t <= move_dist + 0.35f) { // 35cm body radius
                    if (t < best_t) {
                        best_t = t;
                        out_collided = true;
                        out_obstacle_name = w.name;
                        float safe_dist = (std::max)(0.0f, t - 0.35f);
                        best_pos = Vector3(from.x + dir_x * safe_dist, from.y, from.z + dir_z * safe_dist);
                    }
                }
            }
        }
        return best_pos;
    }

    static SurfaceType string_to_surface(const std::string& s) {
        if (s == "stone") return SurfaceType::STONE;
        if (s == "carpet") return SurfaceType::CARPET;
        if (s == "metal") return SurfaceType::METAL;
        if (s == "grass") return SurfaceType::GRASS;
        if (s == "tile") return SurfaceType::TILE;
        if (s == "glass") return SurfaceType::GLASS;
        if (s == "leather_fabric") return SurfaceType::LEATHER_FABRIC;
        return SurfaceType::WOOD;
    }

    // Load dynamic map from JSON (e.g. assets/maps/default_world.json)
    bool load_from_json_file(const std::string& jsonPath) {
        std::ifstream file(jsonPath);
        if (!file.is_open()) return false;
        try {
            json j;
            file >> j;

            title = j.value("title", "Fake-VR World");
            if (j.contains("spawn_point") && j["spawn_point"].is_object()) {
                spawn_point.x = j["spawn_point"].value("x", 0.0f);
                spawn_point.y = j["spawn_point"].value("y", 0.0f);
                spawn_point.z = j["spawn_point"].value("z", 0.0f);
            }

            walls.clear();
            surface_zones.clear();
            objects.clear();
            regions.clear();

            // Load regions
            if (j.contains("regions") && j["regions"].is_array()) {
                for (const auto& r : j["regions"]) {
                    RoomRegion reg;
                    reg.id = r.value("id", "region");
                    reg.name = r.value("name", "Area");
                    reg.min_x = r.value("min_x", -10.0f);
                    reg.max_x = r.value("max_x", 10.0f);
                    reg.min_z = r.value("min_z", -10.0f);
                    reg.max_z = r.value("max_z", 10.0f);
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
                    sz.min_z = z.value("min_z", 0.0f);
                    sz.max_z = z.value("max_z", 0.0f);
                    sz.surface = string_to_surface(z.value("surface", "wood"));
                    surface_zones.push_back(sz);
                }
            }

            int nextObjId = 0;
            auto add_box_object = [&](const std::string& name, float x1, float x2, float z1, float z2, float h, SurfaceType mat, int hp, bool canDestroy, const std::string& soundAsset = "", float soundVol = 0.8f) {
                WorldObject obj;
                obj.id = nextObjId++;
                obj.name = name;
                obj.min_x = x1; obj.max_x = x2;
                obj.min_z = z1; obj.max_z = z2;
                obj.height = h;
                obj.material = mat;
                obj.health = hp;
                obj.maxHealth = hp;
                obj.destructible = canDestroy;
                obj.soundAsset = soundAsset;
                obj.soundVolume = soundVol;
                objects.push_back(obj);

                auto props = get_material_acoustics(mat);
                walls.push_back({x1, z1, x2, z1, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x2, z1, x2, z2, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x2, z2, x1, z2, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x1, z2, x1, z1, name, mat, props.reflectivity, false, obj.id});
            };

            // Load nested structures (e.g. house as a building entity on the map)
            if (j.contains("structures") && j["structures"].is_array()) {
                for (const auto& s : j["structures"]) {
                    // Load structure walls
                    if (s.contains("walls") && s["walls"].is_array()) {
                        for (const auto& w : s["walls"]) {
                            WallSegment ws;
                            ws.x1 = w.value("x1", 0.0f);
                            ws.z1 = w.value("z1", 0.0f);
                            ws.x2 = w.value("x2", 0.0f);
                            ws.z2 = w.value("z2", 0.0f);
                            ws.name = w.value("name", "Wall");
                            ws.material = string_to_surface(w.value("material", "wood"));
                            ws.reflectivity = w.value("reflectivity", 0.60f);
                            ws.isDoorwayOpening = w.value("is_doorway", false);
                            ws.linkedObjectId = -1;
                            walls.push_back(ws);
                        }
                    }

                    // Load interior objects inside the building
                    if (s.contains("interior_objects") && s["interior_objects"].is_array()) {
                        for (const auto& o : s["interior_objects"]) {
                            std::string oname = o.value("name", "Object");
                            float x1 = o.value("min_x", 0.0f);
                            float x2 = o.value("max_x", 0.0f);
                            float z1 = o.value("min_z", 0.0f);
                            float z2 = o.value("max_z", 0.0f);
                            float h = o.value("height", 1.0f);
                            SurfaceType mat = string_to_surface(o.value("material", "wood"));
                            int hp = o.value("health", 100);
                            bool canDestroy = o.value("destructible", true);
                            std::string snd = o.value("sound_asset", "");
                            float svol = o.value("sound_volume", 0.8f);
                            add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                        }
                    }
                }
            }

            // Load outdoor field objects
            if (j.contains("field_objects") && j["field_objects"].is_array()) {
                for (const auto& o : j["field_objects"]) {
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f);
                    float x2 = o.value("max_x", 0.0f);
                    float z1 = o.value("min_z", 0.0f);
                    float z2 = o.value("max_z", 0.0f);
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                }
            }

            // Load direct/generic objects list
            if (j.contains("objects") && j["objects"].is_array()) {
                for (const auto& o : j["objects"]) {
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f);
                    float x2 = o.value("max_x", 0.0f);
                    float z1 = o.value("min_z", 0.0f);
                    float z2 = o.value("max_z", 0.0f);
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                }
            }

            // Load outer boundary walls
            if (j.contains("boundary_walls") && j["boundary_walls"].is_array()) {
                for (const auto& w : j["boundary_walls"]) {
                    WallSegment ws;
                    ws.x1 = w.value("x1", 0.0f);
                    ws.z1 = w.value("z1", 0.0f);
                    ws.x2 = w.value("x2", 0.0f);
                    ws.z2 = w.value("z2", 0.0f);
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

    // Load dynamic map from JSON object directly (e.g. from server packet)
    bool load_from_json(const json& j) {
        try {
            title = j.value("title", "Fake-VR World");
            if (j.contains("spawn_point") && j["spawn_point"].is_object()) {
                spawn_point.x = j["spawn_point"].value("x", 0.0f);
                spawn_point.y = j["spawn_point"].value("y", 0.0f);
                spawn_point.z = j["spawn_point"].value("z", 0.0f);
            }

            walls.clear();
            surface_zones.clear();
            objects.clear();
            regions.clear();

            // Load regions
            if (j.contains("regions") && j["regions"].is_array()) {
                for (const auto& r : j["regions"]) {
                    RoomRegion reg;
                    reg.id = r.value("id", "region");
                    reg.name = r.value("name", "Area");
                    reg.min_x = r.value("min_x", -10.0f);
                    reg.max_x = r.value("max_x", 10.0f);
                    reg.min_z = r.value("min_z", -10.0f);
                    reg.max_z = r.value("max_z", 10.0f);
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
                    sz.min_z = z.value("min_z", 0.0f);
                    sz.max_z = z.value("max_z", 0.0f);
                    sz.surface = string_to_surface(z.value("surface", "wood"));
                    surface_zones.push_back(sz);
                }
            }

            int nextObjId = 0;
            auto add_box_object = [&](const std::string& name, float x1, float x2, float z1, float z2, float h, SurfaceType mat, int hp, bool canDestroy, const std::string& soundAsset = "", float soundVol = 0.8f) {
                WorldObject obj;
                obj.id = nextObjId++;
                obj.name = name;
                obj.min_x = x1; obj.max_x = x2;
                obj.min_z = z1; obj.max_z = z2;
                obj.height = h;
                obj.material = mat;
                obj.health = hp;
                obj.maxHealth = hp;
                obj.destructible = canDestroy;
                obj.soundAsset = soundAsset;
                obj.soundVolume = soundVol;
                objects.push_back(obj);

                auto props = get_material_acoustics(mat);
                walls.push_back({x1, z1, x2, z1, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x2, z1, x2, z2, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x2, z2, x1, z2, name, mat, props.reflectivity, false, obj.id});
                walls.push_back({x1, z2, x1, z1, name, mat, props.reflectivity, false, obj.id});
            };

            // Load nested structures (e.g. house as a building entity on the map)
            if (j.contains("structures") && j["structures"].is_array()) {
                for (const auto& s : j["structures"]) {
                    if (s.contains("walls") && s["walls"].is_array()) {
                        for (const auto& w : s["walls"]) {
                            WallSegment ws;
                            ws.x1 = w.value("x1", 0.0f);
                            ws.z1 = w.value("z1", 0.0f);
                            ws.x2 = w.value("x2", 0.0f);
                            ws.z2 = w.value("z2", 0.0f);
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
                            std::string oname = o.value("name", "Object");
                            float x1 = o.value("min_x", 0.0f);
                            float x2 = o.value("max_x", 0.0f);
                            float z1 = o.value("min_z", 0.0f);
                            float z2 = o.value("max_z", 0.0f);
                            float h = o.value("height", 1.0f);
                            SurfaceType mat = string_to_surface(o.value("material", "wood"));
                            int hp = o.value("health", 100);
                            bool canDestroy = o.value("destructible", true);
                            std::string snd = o.value("sound_asset", "");
                            float svol = o.value("sound_volume", 0.8f);
                            add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                        }
                    }
                }
            }

            // Load direct/outdoor objects
            if (j.contains("field_objects") && j["field_objects"].is_array()) {
                for (const auto& o : j["field_objects"]) {
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f);
                    float x2 = o.value("max_x", 0.0f);
                    float z1 = o.value("min_z", 0.0f);
                    float z2 = o.value("max_z", 0.0f);
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                }
            }

            if (j.contains("objects") && j["objects"].is_array()) {
                for (const auto& o : j["objects"]) {
                    std::string oname = o.value("name", "Object");
                    float x1 = o.value("min_x", 0.0f);
                    float x2 = o.value("max_x", 0.0f);
                    float z1 = o.value("min_z", 0.0f);
                    float z2 = o.value("max_z", 0.0f);
                    float h = o.value("height", 1.0f);
                    SurfaceType mat = string_to_surface(o.value("material", "wood"));
                    int hp = o.value("health", 100);
                    bool canDestroy = o.value("destructible", true);
                    std::string snd = o.value("sound_asset", "");
                    float svol = o.value("sound_volume", 0.8f);
                    add_box_object(oname, x1, x2, z1, z2, h, mat, hp, canDestroy, snd, svol);
                }
            }

            // Load outer boundary walls
            if (j.contains("boundary_walls") && j["boundary_walls"].is_array()) {
                for (const auto& w : j["boundary_walls"]) {
                    WallSegment ws;
                    ws.x1 = w.value("x1", 0.0f);
                    ws.z1 = w.value("z1", 0.0f);
                    ws.x2 = w.value("x2", 0.0f);
                    ws.z2 = w.value("z2", 0.0f);
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
};

// Builds a realistic lounge with a doorway opening that leads outside to a massive 100m x 100m grass field.
// Real physical objects are scattered across the field, with destructibility support for future gameplay.
inline WorldMap create_realistic_lounge() {
    WorldMap m;
    m.title = "Lounge & Great Outdoor Field";
    m.spawn_point = Vector3(0.0f, 0.0f, -2.5f);

    // 1. Lounge Region
    m.regions.push_back({"lounge", "Living Room Lounge", -3.5f, 3.5f, -4.0f, 4.0f, 0.40f, false});
    // 2. 100m x 100m Outside Grass Field Region (-50 to 50 X, 4 to 104 Z)
    m.regions.push_back({"outside", "Great Outdoor Field", -50.0f, 50.0f, 4.0f, 104.0f, 0.15f, true});

    // Floor surfaces
    m.surface_zones.push_back({"Living Room Wood Floor", -3.5f, 3.5f, -4.0f, 4.0f, SurfaceType::WOOD});
    m.surface_zones.push_back({"Plush Rug Under Couch & Table", -2.0f, 2.0f, -1.8f, 1.8f, SurfaceType::CARPET});
    // Massive 100x100 grass surface
    m.surface_zones.push_back({"Outdoor Grass Field", -50.0f, 50.0f, 4.0f, 104.0f, SurfaceType::GRASS});

    int nextObjId = 0;
    auto add_box_object = [&](const std::string& name, float x1, float x2, float z1, float z2, float h, SurfaceType mat, int hp = 100, bool canDestroy = true) {
        WorldObject obj;
        obj.id = nextObjId++;
        obj.name = name;
        obj.min_x = x1; obj.max_x = x2;
        obj.min_z = z1; obj.max_z = z2;
        obj.height = h;
        obj.material = mat;
        obj.health = hp;
        obj.destructible = canDestroy;
        m.objects.push_back(obj);

        auto props = get_material_acoustics(mat);
        m.walls.push_back({x1, z1, x2, z1, name, mat, props.reflectivity, false, obj.id});
        m.walls.push_back({x2, z1, x2, z2, name, mat, props.reflectivity, false, obj.id});
        m.walls.push_back({x2, z2, x1, z2, name, mat, props.reflectivity, false, obj.id});
        m.walls.push_back({x1, z2, x1, z1, name, mat, props.reflectivity, false, obj.id});
    };

    // --- LOUNGE FURNITURE (Arranged comfortably without blocking the door or passage) ---
    // 1. Large Upholstered Fabric Couch (Against West Wall, X: -3.2 to -1.4, Z: -1.0 to 1.0)
    add_box_object("Fabric Sofa Couch", -3.2f, -1.4f, -1.0f, 1.0f, 0.85f, SurfaceType::LEATHER_FABRIC, 80, true);

    // 2. Oak Wood Coffee Table (In front of the couch, X: -1.1 to 0.1, Z: -0.5 to 0.5)
    add_box_object("Solid Oak Coffee Table", -1.1f, 0.1f, -0.5f, 0.5f, 0.45f, SurfaceType::WOOD, 100, true);

    // 3. Tall Heavy Metal Equipment Rack (In Southeast Corner, X: 1.8 to 2.8, Z: -3.4 to -2.4)
    add_box_object("Metal Equipment Rack", 1.8f, 2.8f, -3.4f, -2.4f, 1.80f, SurfaceType::METAL, 250, true);

    // 4. Large Solid Wooden Bookcase (Along East Wall, X: 2.5 to 3.2, Z: -1.5 to 1.5)
    add_box_object("Wooden Bookcase", 2.5f, 3.2f, -1.5f, 1.5f, 2.10f, SurfaceType::WOOD, 120, true);

    // 5. Tall Glass Display Cabinet (Northeast Corner, X: 1.8 to 2.8, Z: 2.2 to 3.2)
    add_box_object("Glass Display Cabinet", 1.8f, 2.8f, 2.2f, 3.2f, 1.70f, SurfaceType::GLASS, 40, true);

    // --- LOUNGE WALLS (-3.5 to 3.5 X, -4.0 to 4.0 Z) ---
    // South Wall
    m.walls.push_back({-3.5f, -4.0f, 3.5f, -4.0f, "South Plaster Wall", SurfaceType::WOOD, 0.60f, false, -1});
    // East Wall
    m.walls.push_back({3.5f, -4.0f, 3.5f, 4.0f, "East Wall", SurfaceType::STONE, 0.75f, false, -1});
    // West Wall
    m.walls.push_back({-3.5f, 4.0f, -3.5f, -4.0f, "West Wall", SurfaceType::STONE, 0.75f, false, -1});

    // North Wall with Clear Wide Doorway to Outside:
    // Left section of north wall
    m.walls.push_back({-3.5f, 4.0f, -1.0f, 4.0f, "North Wall", SurfaceType::WOOD, 0.60f, false, -1});
    // CLEAR, UNBLOCKED DOORWAY OUTSIDE (-1.0m to 1.0m X at Z=4.0)
    m.walls.push_back({-1.0f, 4.0f, 1.0f, 4.0f, "Doorway to Outside Field", SurfaceType::WOOD, 0.0f, true, -1});
    // Right section of north wall (with glass window)
    m.walls.push_back({1.0f, 4.0f, 2.5f, 4.0f, "Glass Bay Window", SurfaceType::GLASS, 0.96f, false, -1});
    m.walls.push_back({2.5f, 4.0f, 3.5f, 4.0f, "North Wall", SurfaceType::WOOD, 0.60f, false, -1});

    // --- 100m x 100m OUTDOOR FIELD WITH REAL SCATTERED OBJECTS ---
    // Outside Exterior Facade walls beside the lounge
    m.walls.push_back({-50.0f, 4.0f, -3.5f, 4.0f, "Lounge Exterior North Wall (West)", SurfaceType::STONE, 0.80f, false, -1});
    m.walls.push_back({3.5f, 4.0f, 50.0f, 4.0f, "Lounge Exterior North Wall (East)", SurfaceType::STONE, 0.80f, false, -1});

    // Outer Boundaries of the 100x100 Field
    m.walls.push_back({-50.0f, 4.0f, -50.0f, 104.0f, "West Field Wooden Fence", SurfaceType::WOOD, 0.50f, false, -1});
    m.walls.push_back({50.0f, 4.0f, 50.0f, 104.0f, "East Field Stone Wall", SurfaceType::STONE, 0.75f, false, -1});
    m.walls.push_back({-50.0f, 104.0f, 50.0f, 104.0f, "North Field Tree Line Boundary", SurfaceType::GRASS, 0.25f, false, -1});

    // SCATTERED REAL PHYSICAL OBJECTS ACROSS THE GRASS FIELD:
    // 1. Solid Oak Wooden Bench (just outside the doorway at Z=8)
    add_box_object("Wooden Park Bench", -1.2f, 1.2f, 7.5f, 8.3f, 0.9f, SurfaceType::WOOD, 100, true);

    // 2. Heavy Steel Tool Shed (to the northeast at X=14, Z=18)
    add_box_object("Steel Tool Shed", 12.0f, 16.0f, 16.0f, 20.0f, 2.5f, SurfaceType::METAL, 400, true);

    // 3. Large Ancient Granite Boulder (to the northwest at X=-12, Z=15)
    add_box_object("Granite Boulder", -14.0f, -10.0f, 13.0f, 17.0f, 1.8f, SurfaceType::STONE, 600, true);

    // 4. Large Wooden Gazebo Pavilion (at X=0, Z=28)
    add_box_object("Wooden Gazebo Post (West)", -4.0f, -3.2f, 26.0f, 26.8f, 3.0f, SurfaceType::WOOD, 150, true);
    add_box_object("Wooden Gazebo Post (East)", 3.2f, 4.0f, 26.0f, 26.8f, 3.0f, SurfaceType::WOOD, 150, true);
    add_box_object("Wooden Gazebo Table", -1.5f, 1.5f, 27.5f, 29.5f, 0.9f, SurfaceType::WOOD, 120, true);

    // 5. Corrugated Metal Cargo Container (at X=-22, Z=35)
    add_box_object("Metal Cargo Container", -25.0f, -19.0f, 33.0f, 41.0f, 2.6f, SurfaceType::METAL, 500, true);

    // 6. Old Brick Well Structure (at X=18, Z=42)
    add_box_object("Stone Water Well", 16.5f, 19.5f, 40.5f, 43.5f, 1.2f, SurfaceType::STONE, 350, true);

    // 7. Large Wooden Lumber Stacks (at X=8, Z=55)
    add_box_object("Lumber Stack Pile", 6.0f, 11.0f, 53.0f, 57.0f, 1.6f, SurfaceType::WOOD, 150, true);

    // 8. Glass Greenhouse Nursery (at X=-18, Z=60)
    add_box_object("Glass Greenhouse Nursery", -22.0f, -14.0f, 57.0f, 65.0f, 2.8f, SurfaceType::GLASS, 80, true);

    // 9. Abandoned Steel Farm Tractor (at X=25, Z=72)
    add_box_object("Steel Farm Tractor", 23.0f, 27.0f, 70.0f, 74.0f, 2.0f, SurfaceType::METAL, 350, true);

    // 10. Massive Stone Monolith (at X=0, Z=85)
    add_box_object("Ancient Stone Monolith", -2.5f, 2.5f, 83.0f, 87.0f, 4.5f, SurfaceType::STONE, 800, true);

    // 11. Wooden Fruit Crates (at X=-8, Z=20)
    add_box_object("Wooden Fruit Crates", -9.5f, -7.0f, 19.0f, 21.5f, 0.8f, SurfaceType::WOOD, 60, true);

    return m;
}

} // namespace fakevr
