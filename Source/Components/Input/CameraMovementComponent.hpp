#pragma once

#include <glm/vec3.hpp>

#include "InputKeyMappings.hpp"

namespace FeatherVK {
    class CameraMovementComponent {
    public:
        struct FocusState {
            bool active = false;
            float progress = 0.0f;
            glm::vec3 targetPosition{0.0f};
            glm::vec3 objectPosition{0.0f};
        };

        CameraMovementComponent() = default;

        InputKeyMappings keyMappings{};
        float focusMoveTime = 1.0f;
        float lookSpeed = 2.0f;
        float moveSpeed = 3.0f;
        FocusState focus{};
    };
}
