#pragma once

#include <limits>

#include "../../ECS/SceneRegistry.hpp"
#include "../MeshRendererComponent.hpp"
#include "../TransformComponent.hpp"
#include "InputControllerComponent.hpp"

namespace FeatherVK {
    class CameraMovementComponent : public InputControllerComponent {
    public:
        CameraMovementComponent(GLFWwindow *window) : InputControllerComponent(window) {
            name = "CameraMovementComponent";
        }

        void Update(const ComponentUpdateInfo &updateInfo) override {
            MoveCamera(updateInfo);
            FocusOnObject(updateInfo);
        }

    private:
        float m_focusMoveTime = 1.f;
        float m_lookSpeed{2.f};
        float moveSpeed{3.f};

        void MoveCamera(const ComponentUpdateInfo &updateInfo) {
            TransformComponent *cameraTransform = updateInfo.transform;
            if (cameraTransform == nullptr || updateInfo.frameInfo == nullptr) {
                return;
            }

            glm::vec3 rotation{0.f};

            static glm::vec2 lastFrameCursorPos = glm::vec2{std::numeric_limits<float>::max()};
            if (rightMousePressed && glm::length(lastFrameCursorPos - curPos) > std::numeric_limits<float>::epsilon()) {
                const glm::vec2 deltaPos = curPos - lastPos;
                lastFrameCursorPos = curPos;
                rotation.x -= deltaPos.y;
                rotation.y += deltaPos.x;
            }

            if (glm::dot(rotation, rotation) > std::numeric_limits<float>::epsilon()) {
                cameraTransform->SetRotation(rotation * m_lookSpeed + cameraTransform->GetRotation());
            }

            const auto transformRotation = cameraTransform->GetRotation();
            auto forwardDirMatrix = Utils::GetRotateDirectionMatrix(transformRotation);
            const glm::vec3 forwardDir = forwardDirMatrix * glm::vec4{0, 0, 1, 1};
            forwardDirMatrix = Utils::GetRotateDirectionMatrix({transformRotation.x + 1, transformRotation.y, transformRotation.z});
            const glm::vec3 forwardDirWithOffset = forwardDirMatrix * glm::vec4{0, 0, 1, 1};
            const glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, forwardDirWithOffset));
            const glm::mat4 rotationMatrix = glm::rotate(glm::mat4{1.f}, glm::radians(90.f), rightDir);
            const glm::vec3 upDir = rotationMatrix * glm::vec4{forwardDir, 1};

            glm::vec3 moveDir{0.f};
            if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
            if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;
            if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
            if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
            if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir += forwardDir;
            if (glfwGetKey(window, keys.moveBack) == GLFW_PRESS) moveDir -= forwardDir;
            if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
                cameraTransform->SetTranslation(glm::normalize(moveDir) * moveSpeed * updateInfo.frameInfo->frameTime + cameraTransform->GetTranslation());
            }
        }

        void FocusOnObject(const ComponentUpdateInfo &updateInfo) {
            TransformComponent *cameraTransform = updateInfo.transform;
            if (cameraTransform == nullptr || updateInfo.frameInfo == nullptr || updateInfo.rendererInfo == nullptr) {
                return;
            }

            FrameInfo &frameInfo = *updateInfo.frameInfo;
            ECS::SceneRegistry *sceneRegistry = frameInfo.sceneRegistry != nullptr ? frameInfo.sceneRegistry : updateInfo.sceneRegistry;
            if (sceneRegistry == nullptr) {
                return;
            }

            static bool isFocusing = false;
            static glm::vec3 targetPosition{};
            static glm::vec3 focusObjectPosition{};

            if (glfwGetKey(window, keys.KEY_F) == GLFW_PRESS && !isFocusing) {
                const id_t selectedEntityId = frameInfo.selectedEntityId;
                if (!sceneRegistry->IsAlive(selectedEntityId) || !sceneRegistry->IsEntityActive(selectedEntityId)) {
                    return;
                }

                TransformComponent *selectedTransform = nullptr;
                if (!sceneRegistry->TryGetComponent(selectedEntityId, selectedTransform) || selectedTransform == nullptr) {
                    return;
                }

                focusObjectPosition = selectedTransform->GetTranslation();
                const glm::vec3 moveTarget = focusObjectPosition - cameraTransform->GetTranslation();
                if (glm::length(moveTarget) < std::numeric_limits<float>::epsilon()) {
                    return;
                }

                const float distance = glm::length(moveTarget);
                const glm::vec3 moveDirection = glm::normalize(moveTarget);

                float maxRadius = 1.f;
                MeshRendererComponent *selectedMeshRenderer = nullptr;
                if (sceneRegistry->TryGetComponent(selectedEntityId, selectedMeshRenderer) &&
                    selectedMeshRenderer != nullptr &&
                    selectedMeshRenderer->GetModelPtr() != nullptr) {
                    const auto selectedScale = selectedTransform->GetScale();
                    maxRadius = selectedMeshRenderer->GetModelPtr()->GetMaxRadius() *
                                glm::max(selectedScale.x, glm::max(selectedScale.y, selectedScale.z));
                }

                const float keptDistance = maxRadius / glm::tan(glm::radians(updateInfo.rendererInfo->fovY) / 2.f);
                const float moveDistance = distance - keptDistance;
                targetPosition = cameraTransform->GetTranslation() + moveDirection * moveDistance;
                isFocusing = true;
            }

            if (isFocusing) {
                static float t = 0.f;
                t += frameInfo.frameTime / m_focusMoveTime;
                const glm::vec3 stepTranslateTarget = (1 - t) * cameraTransform->GetTranslation() + t * targetPosition;
                cameraTransform->SetTranslation(stepTranslateTarget);

                const glm::vec3 translateDirection = glm::normalize(focusObjectPosition - cameraTransform->GetTranslation());
                const glm::vec3 targetRotation = Utils::VectorToRotation(translateDirection);
                cameraTransform->SetRotation(targetRotation);

                if (glm::length(stepTranslateTarget - targetPosition) < 0.01f) {
                    isFocusing = false;
                    t = 0.f;
                }
            }
        }
    };
}

