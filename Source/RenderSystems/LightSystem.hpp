#pragma once

#include <algorithm>

#include "../RenderScene/RenderScene.hpp"
#include "../StructureInfos.h"

namespace FeatherVK {
    class LightSystem {
    public:
        void Collect(FrameInfo &frameInfo) const {
            for (auto &light: frameInfo.globalUbo.lights) {
                light = Light{};
                light.lightCategory = LightCategory::NONE;
            }
            frameInfo.globalUbo.lightNum = 0;

            if (frameInfo.renderScene == nullptr) {
                return;
            }

            int lightIndex = 0;
            for (const auto &lightInstance: frameInfo.renderScene->GetLightInstances()) {
                if (!lightInstance.active || lightIndex >= MAX_LIGHT_NUM) {
                    continue;
                }

                Light light{};
                light.lightCategory = lightInstance.lightCategory;
                light.position = glm::vec4(lightInstance.position, 1.0f);
                light.direction = glm::vec4(lightInstance.direction, 0.0f);
                light.color = glm::vec4(lightInstance.color, lightInstance.intensity);
                frameInfo.globalUbo.lights[lightIndex++] = light;
            }

            frameInfo.globalUbo.lightNum = lightIndex;
        }
    };
}
