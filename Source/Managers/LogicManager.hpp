#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "../Components/LightComponent.hpp"
#include "../GUI.hpp"
#include "ResourceManager.hpp"

namespace FeatherVK {
    class LogicManager {
    public:
        explicit LogicManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = std::move(resourceManager);
        }

        ~LogicManager() = default;

        LogicManager(const LogicManager &) = delete;
        LogicManager &operator=(const LogicManager &) = delete;

        void UpdateComponents(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            UpdateUbo(frameInfo);

            auto &renderer = m_resourceManager->GetRenderer();
            auto &sceneRegistry = *frameInfo.sceneRegistry;

            RendererInfo rendererInfo{renderer.getAspectRatio(), renderer.FOV_Y, renderer.NEAR_CLIP, renderer.FAR_CLIP};

            HandleSceneSelection(frameInfo);
            HandleSelectedObjectMovement(frameInfo);

            static bool firstFrame = true;
            if (firstFrame) {
                for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                    if (!sceneRegistry.IsEntityActive(entityId)) continue;
                    ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                    ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->Start(updateInfo); });
                }
                firstFrame = false;
            }

            for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                auto *meta = sceneRegistry.TryGetEntityMeta(entityId);
                if (meta == nullptr) {
                    continue;
                }

                ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                if (meta->onDisabled) {
                    meta->active = false;
                    meta->onDisabled = false;
                    ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->OnDisable(updateInfo); });
                }
                if (meta->onEnabled) {
                    meta->active = true;
                    meta->onEnabled = false;
                    ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->OnEnable(updateInfo); });
                }
            }

            for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                if (!sceneRegistry.IsEntityActive(entityId)) continue;
                ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->Update(updateInfo); });
            }

            for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                if (!sceneRegistry.IsEntityActive(entityId)) continue;
                ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->LateUpdate(updateInfo); });
            }

            FixedUpdateComponents(frameInfo);
        }

        void FixedUpdateComponents(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &renderer = m_resourceManager->GetRenderer();
            auto &sceneRegistry = *frameInfo.sceneRegistry;
            static float reservedFrameTime = 0;
            float frameTime = frameInfo.frameTime + reservedFrameTime;

            RendererInfo rendererInfo{renderer.getAspectRatio(), renderer.FOV_Y, renderer.NEAR_CLIP, renderer.FAR_CLIP};

            while (frameTime >= FIXED_UPDATE_INTERVAL) {
                frameTime -= FIXED_UPDATE_INTERVAL;

                for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                    if (!sceneRegistry.IsEntityActive(entityId)) continue;
                    ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                    ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->FixedUpdate(updateInfo); });
                }

                for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                    if (!sceneRegistry.IsEntityActive(entityId)) continue;
                    ComponentUpdateInfo updateInfo = BuildUpdateInfo(sceneRegistry, frameInfo, rendererInfo, entityId);
                    ForEachValidComponent(sceneRegistry, entityId, [&updateInfo](Component *component) { component->LateFixedUpdate(updateInfo); });
                }
            }
            reservedFrameTime = frameTime;
        }

        void UpdateUbo(FrameInfo &frameInfo) {
            frameInfo.globalUbo.lightNum = LightComponent::GetLightNum();
            frameInfo.globalUbo.curTime = frameInfo.totalTime;
        }

    private:
        template<typename Callback>
        static void ForEachValidComponent(ECS::SceneRegistry &sceneRegistry, id_t entityId, Callback &&callback) {
            for (auto *component: sceneRegistry.GetComponents(entityId)) {
                if (component != nullptr) {
                    callback(component);
                }
            }
        }

        static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        ComponentUpdateInfo BuildUpdateInfo(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo, RendererInfo &rendererInfo, id_t entityId) const {
            ComponentUpdateInfo updateInfo{};
            updateInfo.entityId = entityId;
            updateInfo.sceneRegistry = &sceneRegistry;
            updateInfo.frameInfo = &frameInfo;
            updateInfo.rendererInfo = &rendererInfo;
            sceneRegistry.TryGetComponent(entityId, updateInfo.transform);
            return updateInfo;
        }

        void SyncSelectedId(FrameInfo &frameInfo) {
            const id_t selectedId = GUI::HasSelection() ? GUI::GetSelectedId() : InvalidEntityId;
            frameInfo.selectedEntityId = selectedId;
            frameInfo.selectedGameObjectId = selectedId;
        }

        bool TryGetCursorFramebufferPosition(GLFWwindow *window, float &cursorFramebufferX, float &cursorFramebufferY) const {
            if (window == nullptr) {
                return false;
            }

            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                return false;
            }

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                return false;
            }

            double cursorWindowX = 0.0;
            double cursorWindowY = 0.0;
            glfwGetCursorPos(window, &cursorWindowX, &cursorWindowY);

            cursorFramebufferX = static_cast<float>(cursorWindowX) *
                                 static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
            cursorFramebufferY = static_cast<float>(cursorWindowY) *
                                 static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);
            return true;
        }

        bool IsCursorInEditorUi(MyWindow &windowWrapper, GLFWwindow *window) const {
            float cursorFramebufferX = 0.0f;
            float cursorFramebufferY = 0.0f;
            if (!TryGetCursorFramebufferPosition(window, cursorFramebufferX, cursorFramebufferY)) {
                return false;
            }

            const auto sceneExtent = windowWrapper.getCurrentSceneExtent();
            const auto windowExtent = windowWrapper.getCurrentExtent();
            const float sceneOriginX = static_cast<float>(windowExtent.width) - static_cast<float>(sceneExtent.width);
            return cursorFramebufferX < sceneOriginX;
        }

        void UpdateUiKeyboardFocusLatch(MyWindow &windowWrapper, GLFWwindow *window) {
            if (window == nullptr) {
                return;
            }

            const bool leftMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool leftMouseJustPressed = leftMousePressed && !m_leftMousePressedForUiFocusLastFrame;
            m_leftMousePressedForUiFocusLastFrame = leftMousePressed;
            if (!leftMouseJustPressed) {
                return;
            }

            m_sceneKeyboardBlockedByUiFocus = IsCursorInEditorUi(windowWrapper, window);
        }

        bool IsImGuiInputCaptured(MyWindow &windowWrapper, GLFWwindow *window) const {
            if (m_sceneKeyboardBlockedByUiFocus) {
                return true;
            }

            if (ImGui::GetCurrentContext() == nullptr) {
                return false;
            }

            const ImGuiIO &io = ImGui::GetIO();
            if (io.WantCaptureKeyboard || io.WantCaptureMouse || io.WantTextInput || io.NavActive) {
                return true;
            }

            if (IsCursorInEditorUi(windowWrapper, window)) {
                return true;
            }

            return ImGui::IsAnyItemActive() ||
                   ImGui::IsAnyItemFocused() ||
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
                   ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
        }

        void HandleSceneSelection(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

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

            if (IsCursorInEditorUi(windowWrapper, window)) {
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

            float cursorFramebufferX = 0.0f;
            float cursorFramebufferY = 0.0f;
            if (!TryGetCursorFramebufferPosition(window, cursorFramebufferX, cursorFramebufferY)) {
                return;
            }

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

            const int32_t pickedEntityId = renderer.readPickingObjectId(pixelX, pixelY);
            if (pickedEntityId >= 0) {
                const id_t selectedId = static_cast<id_t>(pickedEntityId);
                if (frameInfo.sceneRegistry->IsAlive(selectedId) && frameInfo.sceneRegistry->IsEntityActive(selectedId)) {
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
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &windowWrapper = m_resourceManager->GetWindow();
            GLFWwindow *window = windowWrapper.getGLFWwindow();
            if (window == nullptr) {
                return;
            }

            UpdateUiKeyboardFocusLatch(windowWrapper, window);
            if (IsImGuiInputCaptured(windowWrapper, window)) {
                return;
            }

            if (!GUI::HasSelection()) {
                return;
            }

            const id_t selectedId = GUI::GetSelectedId();
            if (!frameInfo.sceneRegistry->IsAlive(selectedId) || !frameInfo.sceneRegistry->IsEntityActive(selectedId)) {
                return;
            }

            TransformComponent *selectedTransform = nullptr;
            if (!frameInfo.sceneRegistry->TryGetComponent(selectedId, selectedTransform) || selectedTransform == nullptr) {
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
                selectedTransform->Translate(delta);
            }
        }

    private:
        std::shared_ptr<ResourceManager> m_resourceManager;
        bool m_leftMousePressedLastFrame = false;
        bool m_leftMousePressedForUiFocusLastFrame = false;
        bool m_sceneKeyboardBlockedByUiFocus = false;
        float m_selectedMoveSpeed = 0.003f;
    };
}









