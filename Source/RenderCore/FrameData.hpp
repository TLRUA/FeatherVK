#pragma once

#include "../StructureInfos.h"

namespace FeatherVK::RenderCore {
    struct RenderView {
        ViewportRect panelRect{};
        ViewportRect viewportRect{};
        VkExtent2D renderExtent{};
        float aspectRatio{1.0f};
    };

    struct FrameContext {
        int frameIndex{};
        float frameTime{};
        float totalTime{};
        VkExtent2D windowExtent{};
        RenderView view{};
    };

    inline FrameContext BuildFrameContext(const FrameInfo &frameInfo) {
        const float aspectRatio = frameInfo.sceneRenderExtent.height == 0
                                      ? 1.0f
                                      : static_cast<float>(frameInfo.sceneRenderExtent.width) /
                                            static_cast<float>(frameInfo.sceneRenderExtent.height);
        return {
            frameInfo.frameIndex,
            frameInfo.frameTime,
            frameInfo.totalTime,
            frameInfo.extent,
            {
                frameInfo.scenePanelRect,
                frameInfo.sceneViewportRect,
                frameInfo.sceneRenderExtent,
                aspectRatio,
            },
        };
    }
}
