#pragma once

#include <algorithm>
#include <limits>

#include "../Components/TransformComponent.hpp"
#include "../Core/InputState.hpp"
#include "../MyWindow.hpp"
#include "../Renderer.h"
#include "EditorSelectionService.hpp"
#include "TransformService.hpp"

namespace FeatherVK {
    class EditorInteractionSystem {
    public:
        EditorInteractionSystem(MyWindow &window,
                                Renderer &renderer,
                                InputState &inputState,
                                EditorSelectionService &selectionService,
                                TransformService &transformService)
            : m_window(window),
              m_renderer(renderer),
              m_inputState(inputState),
              m_selectionService(selectionService),
              m_transformService(transformService) {}

        void Update(FrameInfo &frameInfo) {
            HandleSceneSelection(frameInfo);
            HandleSelectedObjectMovement(frameInfo);
        }

    private:
        static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        void SyncSelectedId(FrameInfo &frameInfo) const {
            frameInfo.selectedEntityId = m_selectionService.HasSelection() ? m_selectionService.GetSelectedId() : InvalidEntityId;
        }

        bool IsCursorInEditorUi() const {
            float cursorFramebufferX = 0.0f;
            float cursorFramebufferY = 0.0f;
            if (!m_inputState.TryGetCursorFramebufferPosition(cursorFramebufferX, cursorFramebufferY)) {
                return false;
            }

            const auto &scenePanelRect = m_renderer.getScenePanelRect();
            return !scenePanelRect.Contains(cursorFramebufferX, cursorFramebufferY);
        }

        void UpdateUiKeyboardFocusLatch() {
            const bool leftMousePressed = m_inputState.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftMouseJustPressed = leftMousePressed && !m_leftMousePressedForUiFocusLastFrame;
            m_leftMousePressedForUiFocusLastFrame = leftMousePressed;
            if (!leftMouseJustPressed) {
                return;
            }

            m_sceneKeyboardBlockedByUiFocus = IsCursorInEditorUi();
        }

        bool IsImGuiInputCaptured() const {
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

            if (IsCursorInEditorUi()) {
                return true;
            }

            return ImGui::IsAnyItemActive() ||
                   ImGui::IsAnyItemFocused() ||
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
                   ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
        }

        void HandleSceneSelection(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr || m_inputState.GetWindow() == nullptr) {
                return;
            }

            const bool leftMousePressed = m_inputState.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftMouseJustPressed = leftMousePressed && !m_leftMousePressedLastFrame;
            m_leftMousePressedLastFrame = leftMousePressed;

            if (!leftMouseJustPressed) {
                return;
            }

            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
                return;
            }

            if (IsCursorInEditorUi()) {
                return;
            }

            const auto &sceneViewportRect = m_renderer.getSceneViewportRect();
            if (sceneViewportRect.width <= 0.0f || sceneViewportRect.height <= 0.0f) {
                return;
            }

            float cursorFramebufferX = 0.0f;
            float cursorFramebufferY = 0.0f;
            if (!m_inputState.TryGetCursorFramebufferPosition(cursorFramebufferX, cursorFramebufferY)) {
                return;
            }

            if (!sceneViewportRect.Contains(cursorFramebufferX, cursorFramebufferY)) {
                return;
            }

            const auto pickingExtent = m_renderer.getPickingExtent();
            if (pickingExtent.width == 0 || pickingExtent.height == 0) {
                m_selectionService.ClearSelection();
                SyncSelectedId(frameInfo);
                return;
            }

            const float localX = cursorFramebufferX - sceneViewportRect.x;
            const float localY = cursorFramebufferY - sceneViewportRect.y;

            float mappedX = localX * static_cast<float>(pickingExtent.width) / sceneViewportRect.width;
            float mappedY = localY * static_cast<float>(pickingExtent.height) / sceneViewportRect.height;
            mappedX = std::clamp(mappedX, 0.0f, static_cast<float>(pickingExtent.width - 1));
            mappedY = std::clamp(mappedY, 0.0f, static_cast<float>(pickingExtent.height - 1));

            const int32_t pickedEntityId = m_renderer.readPickingObjectId(
                static_cast<uint32_t>(mappedX),
                static_cast<uint32_t>(mappedY));

            if (pickedEntityId >= 0) {
                const id_t selectedId = static_cast<id_t>(pickedEntityId);
                if (frameInfo.sceneRegistry->IsAlive(selectedId) && frameInfo.sceneRegistry->IsEntityActive(selectedId)) {
                    m_selectionService.Select(selectedId);
                } else {
                    m_selectionService.ClearSelection();
                }
            } else {
                m_selectionService.ClearSelection();
            }

            SyncSelectedId(frameInfo);
        }

        void HandleSelectedObjectMovement(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr || m_inputState.GetWindow() == nullptr) {
                return;
            }

            UpdateUiKeyboardFocusLatch();
            if (IsImGuiInputCaptured()) {
                return;
            }

            if (!m_selectionService.HasSelection()) {
                return;
            }

            const id_t selectedId = m_selectionService.GetSelectedId();
            if (!frameInfo.sceneRegistry->IsAlive(selectedId) || !frameInfo.sceneRegistry->IsEntityActive(selectedId)) {
                return;
            }

            TransformComponent *selectedTransform = nullptr;
            if (!frameInfo.sceneRegistry->TryGetComponent(selectedId, selectedTransform) || selectedTransform == nullptr) {
                return;
            }

            glm::vec3 delta{0.0f};
            if (m_inputState.IsKeyDown(GLFW_KEY_LEFT)) {
                delta.x -= m_selectedMoveSpeed;
            }
            if (m_inputState.IsKeyDown(GLFW_KEY_RIGHT)) {
                delta.x += m_selectedMoveSpeed;
            }
            if (m_inputState.IsKeyDown(GLFW_KEY_UP)) {
                delta.z += m_selectedMoveSpeed;
            }
            if (m_inputState.IsKeyDown(GLFW_KEY_DOWN)) {
                delta.z -= m_selectedMoveSpeed;
            }

            if (glm::dot(delta, delta) > std::numeric_limits<float>::epsilon()) {
                m_transformService.Translate(*frameInfo.sceneRegistry, selectedId, delta);
            }
        }

        MyWindow &m_window;
        Renderer &m_renderer;
        InputState &m_inputState;
        EditorSelectionService &m_selectionService;
        TransformService &m_transformService;
        bool m_leftMousePressedLastFrame = false;
        bool m_leftMousePressedForUiFocusLastFrame = false;
        bool m_sceneKeyboardBlockedByUiFocus = false;
        float m_selectedMoveSpeed = 0.003f;
    };
}
