#pragma once

#include <cstdint>

namespace FeatherVK::RenderCore {
    enum class RenderResourceType : uint8_t {
        Unknown,
        Buffer,
        Texture,
        RenderTarget,
        Shader,
        Pipeline,
        Material,
        MaterialInstance,
        Mesh
    };

    struct RenderResourceHandle {
        RenderResourceType type{RenderResourceType::Unknown};
        uint32_t id{0};
        uint32_t generation{0};

        [[nodiscard]] bool IsValid() const {
            return type != RenderResourceType::Unknown && id != 0 && generation != 0;
        }

        friend bool operator==(const RenderResourceHandle &lhs, const RenderResourceHandle &rhs) {
            return lhs.type == rhs.type && lhs.id == rhs.id && lhs.generation == rhs.generation;
        }

        friend bool operator!=(const RenderResourceHandle &lhs, const RenderResourceHandle &rhs) {
            return !(lhs == rhs);
        }
    };

    inline constexpr RenderResourceHandle InvalidRenderResourceHandle{};
}
