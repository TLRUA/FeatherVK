#pragma once

#include "RayTracingRenderPipeline.hpp"

namespace FeatherVK {
    class HybridRenderPipeline final : public RayTracingRenderPipeline {
    public:
        [[nodiscard]] const char *GetName() const override {
            return "Hybrid";
        }
    };
}
