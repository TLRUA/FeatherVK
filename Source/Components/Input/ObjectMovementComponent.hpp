#pragma once

#include "InputKeyMappings.hpp"

namespace FeatherVK {
    class ObjectMovementComponent {
    public:
        ObjectMovementComponent() = default;

        InputKeyMappings keyMappings{};
        float moveSpeed = 0.003f;
    };
}
