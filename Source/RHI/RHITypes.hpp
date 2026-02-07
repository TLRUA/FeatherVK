#pragma once

#include <cstdint>
#include <string>

namespace FeatherVK::RHI {
    enum class ShaderStage : uint32_t {
        None = 0,
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        TessellationControl = 1 << 2,
        TessellationEvaluation = 1 << 3,
        Geometry = 1 << 4,
        Compute = 1 << 5,
        RayGen = 1 << 6,
        RayClosestHit = 1 << 7,
        RayMiss = 1 << 8,
        RayAnyHit = 1 << 9,
        AllGraphics = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)
    };

    inline ShaderStage operator|(ShaderStage lhs, ShaderStage rhs) {
        return static_cast<ShaderStage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline bool HasStage(ShaderStage mask, ShaderStage flag) {
        return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0u;
    }

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

    enum class BindResourceType {
        UniformBuffer,
        StorageBuffer,
        CombinedImageSampler,
        StorageImage,
        AccelerationStructure
    };

    enum class SwapchainStatus {
        Success,
        Suboptimal,
        OutOfDate
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

    struct Viewport {
        float x{0.0f};
        float y{0.0f};
        float width{0.0f};
        float height{0.0f};
        float minDepth{0.0f};
        float maxDepth{1.0f};
    };

    struct ScissorRect {
        int32_t x{0};
        int32_t y{0};
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

    struct TextureViewDesc {
        TextureDimension dimension{TextureDimension::Texture2D};
        TextureAspect aspectMask{TextureAspect::Color};
        uint32_t baseMipLevel{0};
        uint32_t levelCount{1};
        uint32_t baseArrayLayer{0};
        uint32_t layerCount{1};
    };

    struct SamplerDesc {
        bool anisotropyEnabled{false};
    };

    struct PipelineDesc {
        PipelineType type{PipelineType::Graphics};
        std::string category{};
    };

    struct BindLayoutEntry {
        uint32_t binding{0};
        BindResourceType type{BindResourceType::UniformBuffer};
        ShaderStage stageMask{ShaderStage::None};
        uint32_t count{1};
    };
}
