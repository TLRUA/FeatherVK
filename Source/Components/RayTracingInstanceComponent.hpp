#pragma once

#include <limits>

#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class RayTracingInstanceComponent {
    public:
        inline static constexpr id_t InvalidInstanceId = std::numeric_limits<id_t>::max();

        explicit RayTracingInstanceComponent(id_t instanceId = InvalidInstanceId)
            : instanceId(instanceId) {}

        bool IsValid() const { return instanceId != InvalidInstanceId; }

        id_t instanceId = InvalidInstanceId;
    };
}
