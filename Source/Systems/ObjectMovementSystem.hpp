#pragma once

#include <limits>

#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Core/InputState.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Managers/EditorSceneUtils.hpp"
#include "../Managers/TransformService.hpp"
#include "../StructureInfos.h"

namespace FeatherVK {
    class ObjectMovementSystem {
    public:
        ObjectMovementSystem(InputState &inputState, TransformService &transformService)
            : m_inputState(inputState), m_transformService(transformService) {}

        void Update(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo) const {
            bool moved = false;
            for (const auto entityId: sceneRegistry.View<ObjectMovementComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId)) {
                    continue;
                }

                ObjectMovementComponent *movement = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, movement) ||
                    !sceneRegistry.TryGetComponent(entityId, transform) ||
                    movement == nullptr ||
                    transform == nullptr) {
                    continue;
                }

                glm::vec3 delta{0.0f};
                if (m_inputState.IsKeyDown(movement->keyMappings.lookLeft)) {
                    delta.x -= movement->moveSpeed;
                }
                if (m_inputState.IsKeyDown(movement->keyMappings.lookRight)) {
                    delta.x += movement->moveSpeed;
                }
                if (m_inputState.IsKeyDown(movement->keyMappings.lookUp)) {
                    delta.z += movement->moveSpeed;
                }
                if (m_inputState.IsKeyDown(movement->keyMappings.lookDown)) {
                    delta.z -= movement->moveSpeed;
                }

                if (glm::dot(delta, delta) > std::numeric_limits<float>::epsilon()) {
                    moved |= m_transformService.Translate(sceneRegistry, entityId, delta);
                }
            }
            if (moved) {
                EditorSceneUtils::MarkRenderSceneDirty(frameInfo);
            }
        }

    private:
        InputState &m_inputState;
        TransformService &m_transformService;
    };
}
