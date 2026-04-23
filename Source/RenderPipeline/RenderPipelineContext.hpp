#pragma once

#include <functional>

#include "../RenderGraph/RenderGraph.hpp"
#include "../RenderGraph/RenderGraphResourceCache.hpp"
#include "../Renderer.h"
#include "../StructureInfos.h"

namespace FeatherVK {
    enum class RenderPipelineBuildMode : uint8_t {
        Main,
        LayoutInteraction
    };

    struct RenderPipelineCallbacks {
        std::function<void(RenderGraph::RenderGraphPassContext &)> SyncRayTracingScene{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordRasterScene{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordRayTracing{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordRayTracingDenoise{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordPicking{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordPost{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordLayoutInteractionPost{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordGizmos{};
        std::function<void(RenderGraph::RenderGraphPassContext &)> RecordShadow{};
    };

    struct RenderPipelineContext {
        FrameInfo &frameInfo;
        Renderer &renderer;
        RenderGraph::RenderGraphResourceCache &resourceCache;
        RenderPipelineCallbacks callbacks{};
        RenderPipelineBuildMode buildMode{RenderPipelineBuildMode::Main};
    };
}
