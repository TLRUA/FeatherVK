#pragma once

#include <glm/mat4x4.hpp>

namespace FeatherVK {
    class CameraComponent {
    public:
        constexpr static glm::mat4 CorrectionMatrix = glm::mat4{
            1, 0, 0, 0,
            0, -1, 0, 0,
            0, 0, 0.5f, 0,
            0, 0, 0.5f, 1
        };

        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 inverseViewMatrix{1.0f};
    };
}
