#pragma once

#include "../StructureInfos.h"

namespace FeatherVK::EditorSceneUtils {
    inline void MarkSceneDirty(FrameInfo &frameInfo, bool updateScene = true) {
        if (updateScene) {
            frameInfo.sceneUpdated = true;
        }
        if (frameInfo.scenePersistence != nullptr) {
            frameInfo.scenePersistence->MarkSceneDirty();
        }
    }
}
