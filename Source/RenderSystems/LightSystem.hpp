#pragma once

#include <algorithm>

#include "../Components/LightComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
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

            if (frameInfo.renderScene != nullptr) {
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
                return;
            }

            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            int lightIndex = 0;
            for (const auto entityId: sceneRegistry.View<LightComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId) || lightIndex >= MAX_LIGHT_NUM) {
                    continue;
                }

                LightComponent *lightComponent = nullptr;
                TransformComponent *transformComponent = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, lightComponent) || lightComponent == nullptr ||
                    !sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr) {
                    continue;
                }

                Light light{};
                light.lightCategory = lightComponent->GetLightCategory();
                light.position = glm::vec4(transformComponent->GetTranslation(), 1.0f);

                const auto rotation = transformComponent->GetRotation();
                auto rotateMatrix = glm::rotate(glm::mat4(1.0f), rotation.y, {0, 1, 0});
                rotateMatrix = glm::rotate(rotateMatrix, rotation.x, {1, 0, 0});
                rotateMatrix = glm::rotate(rotateMatrix, rotation.z, {0, 0, 1});

                light.direction = rotateMatrix * glm::vec4(0, 0, 1, 0);
                light.color = glm::vec4(lightComponent->GetColor(), lightComponent->GetLightIntensity());
                frameInfo.globalUbo.lights[lightIndex++] = light;
            }

            frameInfo.globalUbo.lightNum = lightIndex;
        }
    };
}
