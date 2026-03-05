#pragma once

#include <algorithm>
#include <limits>
#include <optional>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/geometric.hpp>

#include "../Components/TransformComponent.hpp"
#include "../Core/InputState.hpp"
#include "../MyWindow.hpp"
#include "../Renderer.h"
#include "Imgui/imgui.h"
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
            SyncSelectedId(frameInfo);
            const bool gizmoConsumed = HandleTranslationGizmo(frameInfo);
            if (!gizmoConsumed) {
                HandleSceneSelection(frameInfo);
            }
            SyncSelectedId(frameInfo);
            HandleSelectedObjectMovement(frameInfo);
        }

    private:
        static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();
        static constexpr float GizmoAxisHitThresholdPixels = 10.0f;
        static constexpr float WidgetAxisReferenceDistance = 5.0f;
        static constexpr float WidgetAxisWorldLength = 0.35f;

        enum class GizmoAxis {
            None,
            X,
            Y,
            Z
        };

        struct TranslationGizmoState {
            GizmoAxis hoveredAxis{GizmoAxis::None};
            GizmoAxis activeAxis{GizmoAxis::None};
            glm::vec3 dragStartPivotWorldPosition{0.0f};
            glm::vec3 dragStartRelativeTranslation{0.0f};
            glm::vec3 dragPlaneNormal{0.0f};
            float dragStartAxisOffset{0.0f};

            [[nodiscard]] bool IsDragging() const {
                return activeAxis != GizmoAxis::None;
            }
        };

        void SyncSelectedId(FrameInfo &frameInfo) const {
            frameInfo.selectedEntityId = m_selectionService.HasSelection() ? m_selectionService.GetSelectedId() : InvalidEntityId;
        }

        bool HandleTranslationGizmo(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr || ImGui::GetCurrentContext() == nullptr) {
                ResetGizmoState();
                return false;
            }

            const id_t selectedId = frameInfo.selectedEntityId;
            if (!m_selectionService.HasSelection() ||
                !frameInfo.sceneRegistry->IsAlive(selectedId) ||
                !frameInfo.sceneRegistry->IsEntityActive(selectedId)) {
                ResetGizmoState();
                return false;
            }

            TransformComponent *selectedTransform = nullptr;
            if (!frameInfo.sceneRegistry->TryGetComponent(selectedId, selectedTransform) || selectedTransform == nullptr) {
                ResetGizmoState();
                return false;
            }

            const glm::mat4 &viewMatrix = frameInfo.globalUbo.viewMatrix;
            const glm::mat4 &projectionMatrix = frameInfo.globalUbo.projectionMatrix;
            const glm::mat4 &inverseViewMatrix = frameInfo.globalUbo.inverseViewMatrix;

            float cursorFramebufferX = 0.0f;
            float cursorFramebufferY = 0.0f;
            if (!m_inputState.TryGetCursorFramebufferPosition(cursorFramebufferX, cursorFramebufferY)) {
                ResetGizmoState();
                return false;
            }

            const auto &sceneViewportRect = frameInfo.sceneViewportRect;
            const bool cursorInViewport = sceneViewportRect.Contains(cursorFramebufferX, cursorFramebufferY);
            const bool imguiInputCaptured = IsImGuiInputCaptured();
            const bool leftMousePressed = m_inputState.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftMouseJustPressed = leftMousePressed && !m_leftMousePressedLastGizmoFrame;
            const bool leftMouseJustReleased = !leftMousePressed && m_leftMousePressedLastGizmoFrame;
            m_leftMousePressedLastGizmoFrame = leftMousePressed;

            const glm::vec3 gizmoOrigin = GetGizmoPivotWorld(*selectedTransform);
            const glm::vec3 cameraPosition = glm::vec3(inverseViewMatrix[3]);

            std::optional<glm::vec2> originScreen = ProjectWorldToViewport(frameInfo, viewMatrix, projectionMatrix, gizmoOrigin);
            if (!originScreen.has_value()) {
                ResetGizmoState();
                return false;
            }

            DrawTranslationGizmo(viewMatrix,
                                 inverseViewMatrix,
                                 projectionMatrix,
                                 frameInfo.sceneViewportRect,
                                 *originScreen,
                                 {cursorFramebufferX, cursorFramebufferY});

            if (m_gizmoState.IsDragging()) {
                if (leftMouseJustReleased) {
                    m_gizmoState.activeAxis = GizmoAxis::None;
                    return true;
                }

                if (!leftMousePressed) {
                    return false;
                }

                glm::vec3 rayOrigin{0.0f};
                glm::vec3 rayDirection{0.0f};
                if (!BuildCursorRay(frameInfo, viewMatrix, projectionMatrix, cursorFramebufferX, cursorFramebufferY, rayOrigin, rayDirection)) {
                    return true;
                }

                glm::vec3 hitPoint{0.0f};
                if (!IntersectRayWithPlane(
                        rayOrigin,
                        rayDirection,
                        m_gizmoState.dragStartPivotWorldPosition,
                        m_gizmoState.dragPlaneNormal,
                        hitPoint)) {
                    return true;
                }

                const glm::vec3 axisDirection = AxisDirection(m_gizmoState.activeAxis);
                const float axisOffset = glm::dot(hitPoint - m_gizmoState.dragStartPivotWorldPosition, axisDirection);
                const float deltaAlongAxis = axisOffset - m_gizmoState.dragStartAxisOffset;
                const glm::vec3 delta = axisDirection * deltaAlongAxis;
                m_transformService.SetTranslation(
                    *frameInfo.sceneRegistry,
                    selectedId,
                    m_gizmoState.dragStartRelativeTranslation + delta);
                return true;
            }

            if (!cursorInViewport || imguiInputCaptured) {
                m_gizmoState.hoveredAxis = GizmoAxis::None;
                return false;
            }

            m_gizmoState.hoveredAxis = HitTestTranslationAxis(
                viewMatrix,
                inverseViewMatrix,
                projectionMatrix,
                frameInfo.sceneViewportRect,
                *originScreen,
                {cursorFramebufferX, cursorFramebufferY});

            if (!leftMouseJustPressed || m_gizmoState.hoveredAxis == GizmoAxis::None) {
                return false;
            }

            glm::vec3 rayOrigin{0.0f};
            glm::vec3 rayDirection{0.0f};
            if (!BuildCursorRay(frameInfo, viewMatrix, projectionMatrix, cursorFramebufferX, cursorFramebufferY, rayOrigin, rayDirection)) {
                return false;
            }

            const glm::vec3 activeAxisDirection = AxisDirection(m_gizmoState.hoveredAxis);
            const glm::vec3 dragPlaneNormal = ComputeDragPlaneNormal(activeAxisDirection, cameraPosition - gizmoOrigin);
            glm::vec3 hitPoint{0.0f};
            if (!IntersectRayWithPlane(rayOrigin, rayDirection, gizmoOrigin, dragPlaneNormal, hitPoint)) {
                return false;
            }

            m_gizmoState.activeAxis = m_gizmoState.hoveredAxis;
            m_gizmoState.dragStartPivotWorldPosition = gizmoOrigin;
            m_gizmoState.dragStartRelativeTranslation = selectedTransform->GetRelativeTranslation();
            m_gizmoState.dragPlaneNormal = dragPlaneNormal;
            m_gizmoState.dragStartAxisOffset = glm::dot(hitPoint - gizmoOrigin, activeAxisDirection);
            return true;
        }

        static glm::vec3 GetGizmoPivotWorld(const TransformComponent &transform) {
            return transform.GetTranslation();
        }

        static glm::vec3 AxisDirection(const GizmoAxis axis) {
            switch (axis) {
                case GizmoAxis::X:
                    return {1.0f, 0.0f, 0.0f};
                case GizmoAxis::Y:
                    return {0.0f, 1.0f, 0.0f};
                case GizmoAxis::Z:
                    return {0.0f, 0.0f, 1.0f};
                case GizmoAxis::None:
                default:
                    return {0.0f, 0.0f, 0.0f};
            }
        }

        void DrawTranslationGizmo(const glm::mat4 &viewMatrix,
                                  const glm::mat4 &inverseViewMatrix,
                                  const glm::mat4 &projectionMatrix,
                                  const ViewportRect &sceneViewportRect,
                                  const glm::vec2 &originScreen,
                                  const glm::vec2 &cursorFramebufferPosition) {
            if (sceneViewportRect.width <= 0.0f || sceneViewportRect.height <= 0.0f) {
                return;
            }

            ImGui::SetNextWindowPos(ImVec2(sceneViewportRect.x, sceneViewportRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sceneViewportRect.width, sceneViewportRect.height), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            constexpr ImGuiWindowFlags overlayWindowFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoBackground;

            ImGui::Begin("##SceneGizmoOverlay", nullptr, overlayWindowFlags);
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(
                ImVec2(sceneViewportRect.x, sceneViewportRect.y),
                ImVec2(sceneViewportRect.Right(), sceneViewportRect.Bottom()),
                true);

            for (const GizmoAxis axis: {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                const glm::vec2 screenAxisDirection =
                    GetWidgetEquivalentScreenAxisDirection(axis, viewMatrix, inverseViewMatrix, projectionMatrix, sceneViewportRect);
                if (glm::dot(screenAxisDirection, screenAxisDirection) <= std::numeric_limits<float>::epsilon()) {
                    drawList->AddCircleFilled(ToImVec2(originScreen), 3.0f, AxisColor(axis, m_gizmoState.activeAxis == axis || m_gizmoState.hoveredAxis == axis));
                    continue;
                }

                const glm::vec2 endScreen = originScreen + screenAxisDirection;
                DrawGizmoAxis(drawList, axis, originScreen, endScreen, cursorFramebufferPosition);
            }

            drawList->PopClipRect();
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        void DrawGizmoAxis(ImDrawList *drawList,
                           GizmoAxis axis,
                           const glm::vec2 &start,
                           const glm::vec2 &end,
                           const glm::vec2 &cursorFramebufferPosition) const {
            const bool highlighted = m_gizmoState.activeAxis == axis || m_gizmoState.hoveredAxis == axis;
            const ImU32 color = AxisColor(axis, highlighted);
            const float thickness = highlighted ? 4.0f : 2.5f;
            drawList->AddLine(ToImVec2(start), ToImVec2(end), color, thickness);
            drawList->AddCircleFilled(ToImVec2(end), highlighted ? 6.0f : 4.0f, color);
            drawList->AddCircleFilled(ToImVec2(start), 3.0f, IM_COL32(230, 230, 230, 255));
            if (highlighted) {
                drawList->AddCircle(ToImVec2(cursorFramebufferPosition), 10.0f, color, 20, 1.5f);
            }
        }

        GizmoAxis HitTestTranslationAxis(const glm::mat4 &viewMatrix,
                                         const glm::mat4 &inverseViewMatrix,
                                         const glm::mat4 &projectionMatrix,
                                         const ViewportRect &sceneViewportRect,
                                         const glm::vec2 &originScreen,
                                         const glm::vec2 &cursorFramebufferPosition) const {
            float closestDistance = GizmoAxisHitThresholdPixels;
            GizmoAxis closestAxis = GizmoAxis::None;

            for (const GizmoAxis axis: {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                const glm::vec2 screenAxisDirection =
                    GetWidgetEquivalentScreenAxisDirection(axis, viewMatrix, inverseViewMatrix, projectionMatrix, sceneViewportRect);
                if (glm::dot(screenAxisDirection, screenAxisDirection) <= std::numeric_limits<float>::epsilon()) {
                    continue;
                }

                const glm::vec2 endScreen = originScreen + screenAxisDirection;
                const float distance = DistanceToSegment(cursorFramebufferPosition, originScreen, endScreen);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    closestAxis = axis;
                }
            }

            return closestAxis;
        }

        std::optional<glm::vec2> ProjectWorldToViewport(const FrameInfo &frameInfo,
                                                        const glm::mat4 &viewMatrix,
                                                        const glm::mat4 &projectionMatrix,
                                                        const glm::vec3 &worldPosition) const {
            const auto &viewport = frameInfo.sceneViewportRect;
            if (viewport.width <= 0.0f || viewport.height <= 0.0f) {
                return std::nullopt;
            }

            const glm::vec4 clipPosition =
                projectionMatrix * viewMatrix * glm::vec4(worldPosition, 1.0f);
            if (std::abs(clipPosition.w) <= std::numeric_limits<float>::epsilon()) {
                return std::nullopt;
            }

            const glm::vec3 ndc = glm::vec3(clipPosition) / clipPosition.w;
            const float screenX = viewport.x + (ndc.x * 0.5f + 0.5f) * viewport.width;
            const float screenY = viewport.y + (ndc.y * 0.5f + 0.5f) * viewport.height;
            return glm::vec2{screenX, screenY};
        }

        bool BuildCursorRay(const FrameInfo &frameInfo,
                            const glm::mat4 &viewMatrix,
                            const glm::mat4 &projectionMatrix,
                            float cursorFramebufferX,
                            float cursorFramebufferY,
                            glm::vec3 &rayOrigin,
                            glm::vec3 &rayDirection) const {
            const auto &viewport = frameInfo.sceneViewportRect;
            if (viewport.width <= 0.0f || viewport.height <= 0.0f) {
                return false;
            }

            const float ndcX = ((cursorFramebufferX - viewport.x) / viewport.width) * 2.0f - 1.0f;
            const float ndcY = ((cursorFramebufferY - viewport.y) / viewport.height) * 2.0f - 1.0f;

            const glm::mat4 inverseViewProjection = glm::inverse(projectionMatrix * viewMatrix);
            glm::vec4 nearPoint = inverseViewProjection * glm::vec4{ndcX, ndcY, 0.0f, 1.0f};
            glm::vec4 farPoint = inverseViewProjection * glm::vec4{ndcX, ndcY, 1.0f, 1.0f};
            if (std::abs(nearPoint.w) <= std::numeric_limits<float>::epsilon() ||
                std::abs(farPoint.w) <= std::numeric_limits<float>::epsilon()) {
                return false;
            }

            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;
            rayOrigin = glm::vec3(nearPoint);
            rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
            return glm::dot(rayDirection, rayDirection) > std::numeric_limits<float>::epsilon();
        }

        static bool IntersectRayWithPlane(const glm::vec3 &rayOrigin,
                                          const glm::vec3 &rayDirection,
                                          const glm::vec3 &planePoint,
                                          const glm::vec3 &planeNormal,
                                          glm::vec3 &intersectionPoint) {
            const float denominator = glm::dot(planeNormal, rayDirection);
            if (std::abs(denominator) <= 1e-5f) {
                return false;
            }

            const float distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
            if (distance < 0.0f) {
                return false;
            }

            intersectionPoint = rayOrigin + rayDirection * distance;
            return true;
        }

        static glm::vec3 ComputeDragPlaneNormal(const glm::vec3 &axisDirection, const glm::vec3 &cameraToOrigin) {
            glm::vec3 planeTangent = glm::cross(glm::normalize(cameraToOrigin), axisDirection);
            if (glm::dot(planeTangent, planeTangent) <= std::numeric_limits<float>::epsilon()) {
                const glm::vec3 fallback = std::abs(axisDirection.y) < 0.9f
                                               ? glm::vec3{0.0f, 1.0f, 0.0f}
                                               : glm::vec3{1.0f, 0.0f, 0.0f};
                planeTangent = glm::cross(fallback, axisDirection);
            }

            glm::vec3 planeNormal = glm::cross(axisDirection, planeTangent);
            if (glm::dot(planeNormal, planeNormal) <= std::numeric_limits<float>::epsilon()) {
                return glm::vec3{0.0f, 0.0f, 1.0f};
            }
            return glm::normalize(planeNormal);
        }

        static float DistanceToSegment(const glm::vec2 &point,
                                       const glm::vec2 &segmentStart,
                                       const glm::vec2 &segmentEnd) {
            const glm::vec2 segment = segmentEnd - segmentStart;
            const float segmentLengthSquared = glm::dot(segment, segment);
            if (segmentLengthSquared <= std::numeric_limits<float>::epsilon()) {
                return glm::length(point - segmentStart);
            }

            const float t = std::clamp(glm::dot(point - segmentStart, segment) / segmentLengthSquared, 0.0f, 1.0f);
            const glm::vec2 projection = segmentStart + t * segment;
            return glm::length(point - projection);
        }

        static glm::vec2 GetWidgetEquivalentScreenAxisDirection(GizmoAxis axis,
                                                                const glm::mat4 &viewMatrix,
                                                                const glm::mat4 &inverseViewMatrix,
                                                                const glm::mat4 &projectionMatrix,
                                                                const ViewportRect &sceneViewportRect) {
            // Reuse the same world-space setup as the top-right world-axis widget:
            // place the axis origin in front of the camera, then project world X/Y/Z.
            if (sceneViewportRect.width <= 0.0f || sceneViewportRect.height <= 0.0f) {
                return glm::vec2{0.0f};
            }

            const glm::vec3 cameraWorldPosition = glm::vec3(inverseViewMatrix[3]);
            glm::vec3 cameraForward = glm::vec3(inverseViewMatrix[2]);
            const float forwardLength = glm::length(cameraForward);
            if (forwardLength <= std::numeric_limits<float>::epsilon()) {
                return glm::vec2{0.0f};
            }
            cameraForward /= forwardLength;

            const glm::vec3 widgetOriginWorld = cameraWorldPosition + cameraForward * WidgetAxisReferenceDistance;
            const glm::vec3 widgetAxisEndpointWorld = widgetOriginWorld + AxisDirection(axis) * WidgetAxisWorldLength;

            const glm::vec4 originClip = projectionMatrix * viewMatrix * glm::vec4(widgetOriginWorld, 1.0f);
            const glm::vec4 endClip = projectionMatrix * viewMatrix * glm::vec4(widgetAxisEndpointWorld, 1.0f);
            if (std::abs(originClip.w) <= std::numeric_limits<float>::epsilon() ||
                std::abs(endClip.w) <= std::numeric_limits<float>::epsilon()) {
                return glm::vec2{0.0f};
            }

            const glm::vec2 originNdc = glm::vec2(originClip) / originClip.w;
            const glm::vec2 endNdc = glm::vec2(endClip) / endClip.w;
            const glm::vec2 projectedDirectionNdc = endNdc - originNdc;
            const float squareViewportScale = std::min(sceneViewportRect.width, sceneViewportRect.height) * 0.5f;
            return projectedDirectionNdc * squareViewportScale;
        }

        static ImU32 AxisColor(GizmoAxis axis, bool highlighted) {
            switch (axis) {
                case GizmoAxis::X:
                    return highlighted ? IM_COL32(255, 180, 80, 255) : IM_COL32(235, 80, 80, 255);
                case GizmoAxis::Y:
                    return highlighted ? IM_COL32(255, 220, 80, 255) : IM_COL32(90, 220, 90, 255);
                case GizmoAxis::Z:
                    return highlighted ? IM_COL32(255, 220, 80, 255) : IM_COL32(80, 150, 255, 255);
                case GizmoAxis::None:
                default:
                    return IM_COL32(220, 220, 220, 255);
            }
        }

        static ImVec2 ToImVec2(const glm::vec2 &value) {
            return {value.x, value.y};
        }

        void ResetGizmoState() {
            m_gizmoState = {};
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
        bool m_leftMousePressedLastGizmoFrame = false;
        bool m_leftMousePressedForUiFocusLastFrame = false;
        bool m_sceneKeyboardBlockedByUiFocus = false;
        float m_selectedMoveSpeed = 0.003f;
        TranslationGizmoState m_gizmoState{};
    };
}
