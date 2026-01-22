#pragma once

#include <tuple>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class RigidBodyComponent {
    public:
        struct AABB {
            glm::vec3 min{0.0f};
            glm::vec3 max{0.0f};
        };

        inline static const float EPSILON = 0.0001f;
        inline static const glm::vec3 GRAVITY = glm::vec3(0.0f, 0.98f, 0.0f);

        RigidBodyComponent() = default;

        bool initialized = false;
        std::unordered_map<id_t, int> collisionMap{};
        AABB localAabb{};
        glm::vec3 velocity{0.0f};
        glm::vec3 omega{0.0f};
        glm::vec3 nativeMassCenter{0.0f};
        std::vector<std::tuple<glm::vec3, glm::vec3>> pendingImpulses{};
        glm::mat3 inertiaTensor{1.0f};
        glm::mat3 inverseInertiaTensor{1.0f};
        float totalMass = 1.0f;
        float inverseMass = 1.0f;
        float restitution = 0.7f;
        float friction = 0.5f;
        bool isKinematic = false;
        bool useGravity = false;
    };
}
