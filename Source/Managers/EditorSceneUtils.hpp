#pragma once

#include "../StructureInfos.h"

namespace FeatherVK::EditorSceneUtils {
    inline void MarkRenderSceneDirty(FrameInfo &frameInfo) {
        frameInfo.sceneUpdated = true;
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkRenderSceneDirty();
        }
    }

    inline void MarkMeshRendererRenderResourcesDirty(FrameInfo &frameInfo, id_t entityId) {
        MarkRenderSceneDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkMeshRendererRenderResourcesDirty(entityId);
        }
    }

    inline void MarkAllMeshRendererRenderResourcesDirty(FrameInfo &frameInfo) {
        MarkRenderSceneDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkAllMeshRendererRenderResourcesDirty();
        }
    }

    inline void MarkSceneDirty(FrameInfo &frameInfo, bool updateScene = true) {
        if (updateScene) {
            MarkRenderSceneDirty(frameInfo);
        }
        if (frameInfo.scenePersistence != nullptr) {
            frameInfo.scenePersistence->MarkSceneDirty();
        }
    }
}
