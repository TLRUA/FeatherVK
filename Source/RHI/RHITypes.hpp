#pragma once

#include <cstdint>
#include <string>

namespace FeatherVK::RHI {
    enum class BackendType {
        Vulkan
    };

    enum class PipelineType {
        Graphics,
        Compute,
        RayTracing
    };

    enum class TextureDimension {
        Texture2D,
        Cube
    };

    enum class TextureLayout {
        Undefined,
        TransferSrc,
        TransferDst,
        ShaderReadOnly,
        ColorAttachment,
        DepthStencilAttachment,
        General,
        Present
    };

    enum class TextureAspect : uint32_t {
        None = 0,
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2
    };

    inline TextureAspect operator|(TextureAspect lhs, TextureAspect rhs) {
        return static_cast<TextureAspect>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline bool HasAspect(TextureAspect mask, TextureAspect flag) {
        return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0u;
    }

    struct Extent2D {
        uint32_t width{0};
        uint32_t height{0};
    };

    struct TextureSubresourceRange {
        TextureAspect aspectMask{TextureAspect::Color};
        uint32_t baseMipLevel{0};
        uint32_t levelCount{1};
        uint32_t baseArrayLayer{0};
        uint32_t layerCount{1};
    };

    struct BufferDesc {
        uint64_t elementSize{0};
        uint32_t elementCount{0};
        uint64_t byteSize{0};
        bool hostVisible{false};
        bool deviceLocal{false};
        bool supportsDeviceAddress{false};
    };

    struct TextureDesc {
        Extent2D extent{};
        TextureDimension dimension{TextureDimension::Texture2D};
        uint32_t arrayLayers{1};
        bool sampled{false};
        bool storage{false};
        bool renderTarget{false};
        bool srgb{false};
    };

    struct SamplerDesc {
        bool anisotropyEnabled{false};
    };

    struct PipelineDesc {
        PipelineType type{PipelineType::Graphics};
        std::string category{};
    };
}
