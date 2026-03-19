#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "../GUI.hpp"
#include "../Components/LightComponent.hpp"

namespace Kaamoo {
    class LogicManager {
    public:
        LogicManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = resourceManager;
        };

        ~LogicManager() {};

        LogicManager(const LogicManager &) = delete;

        LogicManager &operator=(const LogicManager &) = delete;

        void UpdateComponents(FrameInfo &frameInfo) {
            UpdateUbo(frameInfo);
            ComponentUpdateInfo updateInfo{};
            auto &_renderer = m_resourceManager->GetRenderer();
            auto &_gameObjects = m_resourceManager->GetGameObjects();
            RendererInfo rendererInfo{_renderer.getAspectRatio(), _renderer.FOV_Y, _renderer.NEAR_CLIP, _renderer.FAR_CLIP};
            updateInfo.frameInfo = &frameInfo;
            updateInfo.rendererInfo = &rendererInfo;

            HandleSceneSelection(frameInfo);
            HandleSelectedObjectMovement(frameInfo);

            static bool firstFrame = true;
            if (firstFrame) {
                for (auto &pair: _gameObjects) {
                    if (!pair.second.IsActive()) continue;
                    updateInfo.gameObject = &pair.second;
                    pair.second.Start(updateInfo);
                }
                firstFrame = false;
            }

            for (auto &pair: _gameObjects) {
                auto &_gameObject = pair.second;
                updateInfo.gameObject = &_gameObject;
                if (_gameObject.IsOnDisabled()) {
                    _gameObject.OnDisable(updateInfo);
                }
                if (_gameObject.IsOnEnabled()) {
                    _gameObject.OnEnable(updateInfo);
                }
            }

            for (auto &pair: _gameObjects) {
                if (!pair.second.IsActive()) continue;
                updateInfo.gameObject = &pair.second;
                pair.second.Update(updateInfo);
            }

            for (auto &pair: _gameObjects) {
                if (!pair.second.IsActive()) continue;
                updateInfo.gameObject = &pair.second;
                pair.second.LateUpdate(updateInfo);
            }

            FixedUpdateComponents(frameInfo);
        }

        void FixedUpdateComponents(FrameInfo &frameInfo) {
            auto &_renderer = m_resourceManager->GetRenderer();
            auto &_gameObjects = m_resourceManager->GetGameObjects();
            static float reservedFrameTime = 0;
            float frameTime = frameInfo.frameTime + reservedFrameTime;

            while (frameTime >= FIXED_UPDATE_INTERVAL) {
                frameTime -= FIXED_UPDATE_INTERVAL;

                ComponentUpdateInfo updateInfo{};
                RendererInfo rendererInfo{_renderer.getAspectRatio()};
                updateInfo.frameInfo = &frameInfo;
                updateInfo.rendererInfo = &rendererInfo;
                for (auto &pair: _gameObjects) {
                    if (!pair.second.IsActive()) continue;
                    updateInfo.gameObject = &pair.second;
                    pair.second.FixedUpdate(updateInfo);
                }
                for (auto &pair: _gameObjects) {
                    if (!pair.second.IsActive()) continue;
                    updateInfo.gameObject = &pair.second;
                    pair.second.LateFixedUpdate(updateInfo);
                }
            }
            reservedFrameTime = frameTime;
        }

        //Todo: Light is not active but it still contributes lighting
        void UpdateUbo(FrameInfo &frameInfo) {
            frameInfo.globalUbo.lightNum = LightComponent::GetLightNum();
            frameInfo.globalUbo.curTime = frameInfo.totalTime;
        }

    private:
        static constexpr id_t InvalidGameObjectId = std::numeric_limits<id_t>::max();

        void SyncSelectedId(FrameInfo &frameInfo) {
            frameInfo.selectedGameObjectId = GUI::HasSelection() ? GUI::GetSelectedId() : InvalidGameObjectId;
        }

        void HandleSceneSelection(FrameInfo &frameInfo) {
            auto &windowWrapper = m_resourceManager->GetWindow();
            GLFWwindow *window = windowWrapper.getGLFWwindow();
            if (window == nullptr) {
                return;
            }

            const bool leftMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool leftMouseJustPressed = leftMousePressed && !m_leftMousePressedLastFrame;
            m_leftMousePressedLastFrame = leftMousePressed;

            if (!leftMouseJustPressed) {
                return;
            }

            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
                return;
            }

            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                return;
            }

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                return;
            }

            const auto sceneExtent = windowWrapper.getCurrentSceneExtent();
            if (sceneExtent.width == 0 || sceneExtent.height == 0) {
                return;
            }

            const float sceneWidth = static_cast<float>(sceneExtent.width);
            const float sceneHeight = static_cast<float>(sceneExtent.height);
            const float sceneOriginX = static_cast<float>(framebufferWidth) - sceneWidth;
            const float sceneOriginY = 0.0f;

            double cursorWindowX = 0.0;
            double cursorWindowY = 0.0;
            glfwGetCursorPos(window, &cursorWindowX, &cursorWindowY);

            const float cursorFramebufferX = static_cast<float>(cursorWindowX) *
                                             static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
            const float cursorFramebufferY = static_cast<float>(cursorWindowY) *
                                             static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);

            if (cursorFramebufferX < sceneOriginX || cursorFramebufferX >= sceneOriginX + sceneWidth ||
                cursorFramebufferY < sceneOriginY || cursorFramebufferY >= sceneOriginY + sceneHeight) {
                return;
            }

            auto &renderer = m_resourceManager->GetRenderer();
            const auto pickingExtent = renderer.getPickingExtent();
            if (pickingExtent.width == 0 || pickingExtent.height == 0) {
                GUI::ClearSelection();
                SyncSelectedId(frameInfo);
                return;
            }

            const float localX = cursorFramebufferX - sceneOriginX;
            const float localY = cursorFramebufferY - sceneOriginY;

            float mappedX = localX * static_cast<float>(pickingExtent.width) / sceneWidth;
            float mappedY = localY * static_cast<float>(pickingExtent.height) / sceneHeight;
            mappedX = std::clamp(mappedX, 0.0f, static_cast<float>(pickingExtent.width - 1));
            mappedY = std::clamp(mappedY, 0.0f, static_cast<float>(pickingExtent.height - 1));

            const uint32_t pixelX = static_cast<uint32_t>(mappedX);
            const uint32_t pixelY = static_cast<uint32_t>(mappedY);

            const int32_t pickedObjectId = renderer.readPickingObjectId(pixelX, pixelY);
            if (pickedObjectId >= 0) {
                const id_t selectedId = static_cast<id_t>(pickedObjectId);
                const auto selectedIt = frameInfo.gameObjects.find(selectedId);
                if (selectedIt != frameInfo.gameObjects.end() && selectedIt->second.IsActive()) {
                    GUI::SetSelectedId(selectedId);
                } else {
                    GUI::ClearSelection();
                }
            } else {
                GUI::ClearSelection();
            }

            SyncSelectedId(frameInfo);
        }

        void HandleSelectedObjectMovement(FrameInfo &frameInfo) {
            auto &windowWrapper = m_resourceManager->GetWindow();
            GLFWwindow *window = windowWrapper.getGLFWwindow();
            if (window == nullptr) {
                return;
            }

            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard) {
                return;
            }

            if (!GUI::HasSelection()) {
                return;
            }

            const id_t selectedId = GUI::GetSelectedId();
            auto selectedIt = frameInfo.gameObjects.find(selectedId);
            if (selectedIt == frameInfo.gameObjects.end()) {
                return;
            }

            auto &selectedGameObject = selectedIt->second;
            if (!selectedGameObject.IsActive()) {
                return;
            }

            glm::vec3 delta{0.0f};
            if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
                delta.x -= m_selectedMoveSpeed;
            }
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
                delta.x += m_selectedMoveSpeed;
            }
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
                delta.z += m_selectedMoveSpeed;
            }
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
                delta.z -= m_selectedMoveSpeed;
            }

            if (glm::dot(delta, delta) > std::numeric_limits<float>::epsilon()) {
                selectedGameObject.transform->Translate(delta);
            }
        }

    private:
        std::shared_ptr<ResourceManager> m_resourceManager;
        bool m_leftMousePressedLastFrame = false;
        float m_selectedMoveSpeed = 0.003f;
    };
}

