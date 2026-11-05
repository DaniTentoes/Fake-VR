    // Attacks the object in front of the player (spacebar attack)
    // Bare hands deal 25 damage per strike.
    // Returns true if hit an object, with object name, material, destroyed status, and remaining health.
    bool attack_object_in_front(
        const Vector3& origin,
        float yawDeg,
        int damage,
        std::string& out_name,
        SurfaceType& out_mat,
        bool& out_destroyed,
        int& out_health_left,
        Vector3& out_impact_pos
    ) {
        float rad = yawDeg * (float)M_PI / 180.0f;
        Vector3 dir(std::sin(rad), 0.0f, std::cos(rad));
        float reach = 1.6f; // 1.6m attack range with bare hands

        int hitIndex = -1;
        float bestDist = reach;

        for (int i = 0; i < (int)objects.size(); ++i) {
            if (objects[i].isDestroyed) continue;
            // Check ray against the 4 bounding segments of this object
            float x1 = objects[i].min_x, x2 = objects[i].max_x;
            float z1 = objects[i].min_z, z2 = objects[i].max_z;

            float t = 0.0f;
            bool hit = false;
            float segT;
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x1, z1, x2, z1, segT) && segT < bestDist) { bestDist = segT; hit = true; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x2, z1, x2, z2, segT) && segT < bestDist) { bestDist = segT; hit = true; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x2, z2, x1, z2, segT) && segT < bestDist) { bestDist = segT; hit = true; }
            if (intersect_ray_segment(origin.x, origin.z, dir.x, dir.z, x1, z2, x1, z1, segT) && segT < bestDist) { bestDist = segT; hit = true; }

            if (hit) {
                hitIndex = i;
            }
        }

        if (hitIndex >= 0) {
            WorldObject& obj = objects[hitIndex];
            obj.take_damage(damage);
            out_name = obj.name;
            out_mat = obj.material;
            out_destroyed = obj.isDestroyed;
            out_health_left = obj.health;
            out_impact_pos = Vector3(origin.x + dir.x * bestDist, origin.y, origin.z + dir.z * bestDist);
            return true;
        }

        return false;
    }
