#pragma once

#include <utility>
#include <vector>

#include "RenderPipelineContext.hpp"

namespace FeatherVK {
    class RenderPipeline {
    public:
        virtual ~RenderPipeline() = default;

        [[nodiscard]] virtual const char *GetName() const = 0;

        virtual void Build(RenderPipelineContext &context, RenderGraph::RenderGraph &graph) = 0;
    };

    namespace RenderPipelineUtils {
        inline constexpr uint32_t ShadowMapResolution = 1024;

        inline RenderGraph::RenderGraphTextureDesc TextureDesc(
            VkExtent2D extent,
            uint32_t usageMask,
            RenderGraph::RenderGraphResourceState initialState = RenderGraph::RenderGraphResourceState::Unknown,
            RenderGraph::RenderGraphResourceState finalState = RenderGraph::RenderGraphResourceState::Unknown) {
            RenderGraph::RenderGraphTextureDesc desc{};
            desc.extent = extent;
            desc.usageMask = usageMask;
            desc.initialState = initialState;
            desc.finalState = finalState;
            return desc;
        }

        inline RenderGraph::RenderGraphBufferDesc BufferDesc(
            uint32_t usageMask,
            RenderGraph::RenderGraphResourceState initialState = RenderGraph::RenderGraphResourceState::Unknown,
            RenderGraph::RenderGraphResourceState finalState = RenderGraph::RenderGraphResourceState::Unknown) {
            RenderGraph::RenderGraphBufferDesc desc{};
            desc.usageMask = usageMask;
            desc.initialState = initialState;
            desc.finalState = finalState;
            return desc;
        }

        inline RenderGraph::RenderGraphGraphicsPassDesc GraphicsPassDesc(
            RenderGraph::GraphicsPassTargetKind target,
            VkExtent2D extent,
            RenderGraph::RenderGraphGraphicsPassSignature signature,
            std::vector<VkClearValue> clearValues,
            bool useSceneViewport = false,
            bool renderImGuiAtEnd = false,
            bool endFrame = false) {
            RenderGraph::RenderGraphGraphicsPassDesc desc{};
            desc.target = target;
            desc.extent = extent;
            desc.signature = std::move(signature);
            desc.clearValues = std::move(clearValues);
            desc.useSceneViewport = useSceneViewport;
            desc.renderImGuiAtEnd = renderImGuiAtEnd;
            desc.endFrame = endFrame;
            return desc;
        }

        inline VkClearValue ColorClear(float r, float g, float b, float a) {
            VkClearValue value{};
            value.color.float32[0] = r;
            value.color.float32[1] = g;
            value.color.float32[2] = b;
            value.color.float32[3] = a;
            return value;
        }

        inline VkClearValue DepthClear(float depth = 1.0f, uint32_t stencil = 0u) {
            VkClearValue value{};
            value.depthStencil.depth = depth;
            value.depthStencil.stencil = stencil;
            return value;
        }
    }
}
