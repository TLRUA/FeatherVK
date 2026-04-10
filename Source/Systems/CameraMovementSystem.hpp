#pragma once

#include <algorithm>
#include <limits>

#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Core/InputState.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Managers/EditorSceneUtils.hpp"
#include "../Managers/TransformService.hpp"
#include "../StructureInfos.h"

namespace FeatherVK {
    class CameraMovementSystem {
    public:
        CameraMovementSystem(InputState &inputState, TransformService &transformService)
            : m_inputState(inputState), m_transformService(transformService) {}

        void Update(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo, const RendererInfo &rendererInfo) const {
            for (const auto entityId: sceneRegistry.View<CameraMovementComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId)) {
                    continue;
                }

                CameraMovementComponent *cameraMovement = nullptr;
                TransformComponent *cameraTransform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, cameraMovement) ||
                    !sceneRegistry.TryGetComponent(entityId, cameraTransform) ||
                    cameraMovement == nullptr ||
                    cameraTransform == nullptr) {
                    continue;
                }

                const bool freeLookChanged =
                    UpdateFreeLook(sceneRegistry, entityId, *cameraMovement, *cameraTransform, frameInfo.frameTime);
                const bool focusChanged =
                    UpdateFocus(sceneRegistry, entityId, *cameraMovement, *cameraTransform, frameInfo, rendererInfo);
                if (freeLookChanged || focusChanged) {
                    EditorSceneUtils::MarkRenderSceneDirty(frameInfo);
                }
            }
        }

    private:
        static constexpr float FocusReachThreshold = 0.01f;

        bool UpdateFreeLook(ECS::SceneRegistry &sceneRegistry,
                            id_t entityId,
                            CameraMovementComponent &cameraMovement,
                            TransformComponent &cameraTransform,
                            float frameTime) const {
            bool changed = false;
            glm::vec3 rotation{0.0f};

            if (m_inputState.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
                const glm::vec2 deltaPos = m_inputState.GetCursorDeltaNormalized();
                rotation.x -= deltaPos.y;
                rotation.y += deltaPos.x;
            }

            if (glm::dot(rotation, rotation) > std::numeric_limits<float>::epsilon()) {
                changed |= m_transformService.SetRotation(
                    sceneRegistry,
                    entityId,
                    rotation * cameraMovement.lookSpeed + cameraTransform.GetRotation());
            }

            const auto transformRotation = cameraTransform.GetRotation();
            auto forwardDirMatrix = Utils::GetRotateDirectionMatrix(transformRotation);
            const glm::vec3 forwardDir = forwardDirMatrix * glm::vec4{0, 0, 1, 1};
            forwardDirMatrix = Utils::GetRotateDirectionMatrix({transformRotation.x + 1, transformRotation.y, transformRotation.z});
            const glm::vec3 forwardDirWithOffset = forwardDirMatrix * glm::vec4{0, 0, 1, 1};
            const glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, forwardDirWithOffset));
            const glm::mat4 rotationMatrix = glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), rightDir);
            const glm::vec3 upDir = rotationMatrix * glm::vec4{forwardDir, 1};

            glm::vec3 moveDir{0.0f};
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveUp)) moveDir += upDir;
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveDown)) moveDir -= upDir;
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveLeft)) moveDir -= rightDir;
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveRight)) moveDir += rightDir;
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveForward)) moveDir += forwardDir;
            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.moveBack)) moveDir -= forwardDir;

            if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
                changed |= m_transformService.SetTranslation(
                    sceneRegistry,
                    entityId,
                    glm::normalize(moveDir) * cameraMovement.moveSpeed * frameTime + cameraTransform.GetTranslation());
            }
            return changed;
        }

        bool UpdateFocus(ECS::SceneRegistry &sceneRegistry,
                         id_t entityId,
                         CameraMovementComponent &cameraMovement,
                         TransformComponent &cameraTransform,
                         FrameInfo &frameInfo,
                         const RendererInfo &rendererInfo) const {
            auto &focus = cameraMovement.focus;

            if (m_inputState.IsKeyDown(cameraMovement.keyMappings.focusSelected) && !focus.active) {
                const id_t selectedEntityId = frameInfo.selectedEntityId;
                if (!sceneRegistry.IsAlive(selectedEntityId) || !sceneRegistry.IsEntityActive(selectedEntityId)) {
                    return false;
                }

                TransformComponent *selectedTransform = nullptr;
                if (!sceneRegistry.TryGetComponent(selectedEntityId, selectedTransform) || selectedTransform == nullptr) {
                    return false;
                }

                focus.objectPosition = selectedTransform->GetTranslation();
                const glm::vec3 moveTarget = focus.objectPosition - cameraTransform.GetTranslation();
                if (glm::length(moveTarget) < std::numeric_limits<float>::epsilon()) {
                    return false;
                }

                const float distance = glm::length(moveTarget);
                const glm::vec3 moveDirection = glm::normalize(moveTarget);

                float maxRadius = 1.0f;
                MeshRendererComponent *selectedMeshRenderer = nullptr;
                if (sceneRegistry.TryGetComponent(selectedEntityId, selectedMeshRenderer) &&
                    selectedMeshRenderer != nullptr &&
                    selectedMeshRenderer->GetModelPtr() != nullptr) {
                    const auto selectedScale = selectedTransform->GetScale();
                    maxRadius = selectedMeshRenderer->GetModelPtr()->GetMaxRadius() *
                                glm::max(selectedScale.x, glm::max(selectedScale.y, selectedScale.z));
                }

                const float keptDistance = maxRadius / glm::tan(glm::radians(rendererInfo.fovY) / 2.0f);
                focus.targetPosition = cameraTransform.GetTranslation() + moveDirection * (distance - keptDistance);
                focus.progress = 0.0f;
                focus.active = true;
            }

            if (!focus.active) {
                return false;
            }

            bool changed = false;
            const float duration = std::max(cameraMovement.focusMoveTime, std::numeric_limits<float>::epsilon());
            focus.progress = std::min(focus.progress + frameInfo.frameTime / duration, 1.0f);

            const glm::vec3 nextPosition =
                (1.0f - focus.progress) * cameraTransform.GetTranslation() + focus.progress * focus.targetPosition;
            changed |= m_transformService.SetTranslation(sceneRegistry, entityId, nextPosition);

            const glm::vec3 focusDirection = focus.objectPosition - cameraTransform.GetTranslation();
            if (glm::length(focusDirection) > std::numeric_limits<float>::epsilon()) {
                changed |= m_transformService.SetRotation(sceneRegistry, entityId, Utils::VectorToRotation(glm::normalize(focusDirection)));
            }

            if (glm::length(nextPosition - focus.targetPosition) < FocusReachThreshold) {
                focus.active = false;
                focus.progress = 0.0f;
            }
            return changed;
        }

        InputState &m_inputState;
        TransformService &m_transformService;
    };
}
