#pragma once

#include "../StructureInfos.h"

namespace FeatherVK::EditorSceneUtils {
    inline void MarkRenderFrameDirty(FrameInfo &frameInfo) {
        frameInfo.sceneUpdated = true;
    }

    inline void MarkRenderSceneDirty(FrameInfo &frameInfo) {
        MarkRenderFrameDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkRenderSceneDirty();
        }
    }

    inline void MarkRenderCameraDirty(FrameInfo &frameInfo, id_t entityId) {
        MarkRenderFrameDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkRenderCameraDirty(entityId);
        }
    }

    inline void MarkRenderMeshDirty(FrameInfo &frameInfo, id_t entityId) {
        MarkRenderFrameDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkRenderMeshDirty(entityId);
        }
    }

    inline void MarkRenderLightDirty(FrameInfo &frameInfo, id_t entityId) {
        MarkRenderFrameDirty(frameInfo);
        if (frameInfo.renderInvalidationSink != nullptr) {
            frameInfo.renderInvalidationSink->MarkRenderLightDirty(entityId);
        }
    }

    inline void MarkTransformDirty(FrameInfo &frameInfo) {
        MarkRenderFrameDirty(frameInfo);
    }

    inline void MarkMeshRendererRenderResourcesDirty(FrameInfo &frameInfo, id_t entityId) {
        MarkRenderMeshDirty(frameInfo, entityId);
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
