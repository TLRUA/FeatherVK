#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

#include "Device.hpp"
#include "ECS/SceneRegistry.hpp"
#include "Managers/EditorInspectorSystem.hpp"
#include "Managers/EditorSceneUtils.hpp"
#include "Managers/EditorSelectionService.hpp"
#include "Managers/EntityCommandService.hpp"
#include "Managers/HierarchyService.hpp"
#include "Managers/TransformService.hpp"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_glfw.h"
#include "Imgui/imgui_impl_vulkan.h"
#include "MyWindow.hpp"
#include "Renderer.h"
#include "StructureInfos.h"

namespace FeatherVK {
    class GUI {
    public:
        GUI() = delete;

        static void Destroy() {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            if (imguiDevice != VK_NULL_HANDLE && imguiDescPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(imguiDevice, imguiDescPool, nullptr);
            }
            imguiDescPool = VK_NULL_HANDLE;
            imguiDevice = VK_NULL_HANDLE;
        }

        static void Init(Renderer &renderer, MyWindow &myWindow, Device &device) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            imguiDevice = device.device();
            VkDescriptorPoolSize pool_sizes[] = {
                    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
            };
            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000;
            pool_info.poolSizeCount = std::size(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            vkCreateDescriptorPool(imguiDevice, &pool_info, nullptr, &imguiDescPool);

            ImGui::StyleColorsDark();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\arial.ttf)", 16.0f);
            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.Instance = device.getInstance();
            init_info.PhysicalDevice = device.getPhysicalDevice();
            init_info.Device = device.device();
            init_info.QueueFamily = device.getQueueFamilyIndices().graphicsFamily;
            init_info.Queue = device.graphicsQueue();
            init_info.DescriptorPool = imguiDescPool;
            init_info.MinImageCount = SwapChain::MAX_FRAMES_IN_FLIGHT;
            init_info.ImageCount = SwapChain::MAX_FRAMES_IN_FLIGHT;
            init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.Allocator = nullptr;
            init_info.RenderPass = renderer.getSwapChainRenderPass();
            ImGui_ImplGlfw_InitForVulkan(myWindow.getGLFWwindow(), true);
            ImGui_ImplVulkan_Init(&init_info);
            ImGui_ImplVulkan_CreateFontsTexture();

            ImGuiStyle &style = ImGui::GetStyle();
            style.FramePadding = ImVec2(3, 3);
            style.ItemSpacing = ImVec2(0, 6);
            style.WindowPadding = ImVec2(10, 0);
            style.IndentSpacing = 8;
        }

        static void BeginFrame(ImVec2 windowExtent) {
            ImGui_ImplVulkan_SetMinImageCount(SwapChain::MAX_FRAMES_IN_FLIGHT);
            ImGuiIO &io = ImGui::GetIO();
            io.DisplaySize = windowExtent;
            layoutInteractionActiveThisFrame = false;
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        static void UpdateLayout(ImVec2 windowExtent) {
            viewportLayoutWidth = std::max(1.0f, SnapToPixel(windowExtent.x));
            viewportLayoutHeight = std::max(1.0f, SnapToPixel(windowExtent.y));
            toolbarRect = {0.0f, 0.0f, viewportLayoutWidth, std::min(ComputeToolbarHeight(), viewportLayoutHeight)};

            const float editorPanelsTop = toolbarRect.Bottom();
            const float editorPanelsHeight = std::max(1.0f, viewportLayoutHeight - toolbarRect.height);

            const float minEditorPanelsWidth = MinInspectorPanelWidth + InspectorHierarchySplitterWidth + MinHierarchyPanelWidth;
            const float maxEditorPanelsWidth = std::max(
                minEditorPanelsWidth,
                viewportLayoutWidth - HierarchySceneSplitterWidth - MinScenePanelWidth);
            editorPanelsWidth = SnapToPixel(std::clamp(editorPanelsWidth, minEditorPanelsWidth, maxEditorPanelsWidth));

            const float maxInspectorPanelWidth = std::max(
                MinInspectorPanelWidth,
                editorPanelsWidth - InspectorHierarchySplitterWidth - MinHierarchyPanelWidth);
            inspectorPanelWidth = SnapToPixel(std::clamp(inspectorPanelWidth, MinInspectorPanelWidth, maxInspectorPanelWidth));

            const float hierarchyPanelWidth = std::max(
                MinHierarchyPanelWidth,
                editorPanelsWidth - inspectorPanelWidth - InspectorHierarchySplitterWidth);

            inspectorPanelRect = {0.0f, editorPanelsTop, inspectorPanelWidth, editorPanelsHeight};
            hierarchyPanelRect = {
                inspectorPanelRect.Right() + InspectorHierarchySplitterWidth,
                editorPanelsTop,
                SnapToPixel(hierarchyPanelWidth),
                editorPanelsHeight};
            scenePanelRect = {
                editorPanelsWidth + HierarchySceneSplitterWidth,
                editorPanelsTop,
                std::max(1.0f, viewportLayoutWidth - editorPanelsWidth - HierarchySceneSplitterWidth),
                editorPanelsHeight};
            sceneContentRect = FitRectToAspect(scenePanelRect, SceneAspectRatio);

            if (committedScenePanelRect.width <= 0.0f || committedScenePanelRect.height <= 0.0f) {
                committedScenePanelRect = scenePanelRect;
                committedSceneContentRect = sceneContentRect;
            }
        }

        [[nodiscard]] static const ViewportRect &GetScenePanelRect() {
            return layoutInteractionActive ? committedScenePanelRect : scenePanelRect;
        }

        [[nodiscard]] static const ViewportRect &GetSceneContentRect() {
            return layoutInteractionActive ? committedSceneContentRect : sceneContentRect;
        }

        [[nodiscard]] static bool IsLayoutInteractionActive() {
            return layoutInteractionActiveThisFrame;
        }

#ifdef RAY_TRACING
        static void ShowWindow(ImVec2 windowExtent,
                               ECS::SceneRegistry *sceneRegistry,
                               std::vector<EntityDesc> *gameObjectDescs,
                               HierarchyService &hierarchyService,
                               EntityCommandService &entityCommandService,
                               EditorSelectionService &selectionService,
                               TransformService &transformService,
                               FrameInfo &frameInfo) {
            (void)windowExtent;
            ShowToolbarWindow(frameInfo);
            ShowInspectorRayTracing(windowExtent, sceneRegistry, gameObjectDescs, entityCommandService, selectionService, transformService, frameInfo);
            ShowHierarchyWindow(sceneRegistry, hierarchyService, entityCommandService, selectionService, transformService, frameInfo);
            DrawSplitters();
        }
#else
        static void ShowWindow(ImVec2 windowExtent,
                               ECS::SceneRegistry *sceneRegistry,
                               Material::Map *materials,
                               HierarchyService &hierarchyService,
                               EntityCommandService &entityCommandService,
                               EditorSelectionService &selectionService,
                               TransformService &transformService,
                               FrameInfo &frameInfo) {
            (void)windowExtent;
            ShowToolbarWindow(frameInfo);
            ShowInspectorRaster(windowExtent, sceneRegistry, materials, entityCommandService, selectionService, transformService, frameInfo);
            ShowHierarchyWindow(sceneRegistry, hierarchyService, entityCommandService, selectionService, transformService, frameInfo);
            DrawSplitters();
        }
#endif

        static void EndFrame(VkCommandBuffer &commandBuffer) {
            if (!layoutInteractionActiveThisFrame) {
                committedScenePanelRect = scenePanelRect;
                committedSceneContentRect = sceneContentRect;
            }
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
            layoutInteractionActive = layoutInteractionActiveThisFrame;
            ImGui::EndFrame();
        }

    private:
        inline static VkDescriptorPool imguiDescPool{};
        inline static VkDevice imguiDevice{VK_NULL_HANDLE};
        inline static float inspectorPanelWidth = static_cast<float>(UI_LEFT_WIDTH_2);
        inline static float editorPanelsWidth = static_cast<float>(UI_LEFT_WIDTH + UI_LEFT_WIDTH_2);
        inline static float viewportLayoutWidth = static_cast<float>(SCENE_WIDTH + UI_LEFT_WIDTH + UI_LEFT_WIDTH_2);
        inline static float viewportLayoutHeight = static_cast<float>(SCENE_HEIGHT);
        inline static bool layoutInteractionActive = false;
        inline static bool layoutInteractionActiveThisFrame = false;
        inline static constexpr float ToolbarHorizontalPadding = 10.0f;
        inline static constexpr float ToolbarVerticalPadding = 4.0f;
        inline static constexpr float ToolbarItemSpacingX = 8.0f;
        inline static constexpr float InspectorHierarchySplitterWidth = 10.0f;
        inline static constexpr float HierarchySceneSplitterWidth = 10.0f;
        inline static constexpr float MinInspectorPanelWidth = 180.0f;
        inline static constexpr float MinHierarchyPanelWidth = 180.0f;
        inline static constexpr float MinScenePanelWidth = 180.0f;
        inline static constexpr float SceneAspectRatio = static_cast<float>(SCENE_WIDTH) / static_cast<float>(SCENE_HEIGHT);
        inline static ViewportRect toolbarRect{};
        inline static ViewportRect inspectorPanelRect{};
        inline static ViewportRect hierarchyPanelRect{};
        inline static ViewportRect scenePanelRect{};
        inline static ViewportRect sceneContentRect{};
        inline static ViewportRect committedScenePanelRect{};
        inline static ViewportRect committedSceneContentRect{};
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        struct HierarchyRenameState {
            id_t entityId{InvalidEntityId};
            std::array<char, 256> buffer{};
            bool requestFocus{false};
        };

        inline static HierarchyRenameState hierarchyRenameState{};

        static float SnapToPixel(float value) {
            return std::max(0.0f, std::round(value));
        }

        static float ComputeToolbarHeight() {
            if (ImGui::GetCurrentContext() == nullptr) {
                return 32.0f;
            }

            const float rowHeight = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeight());
            return SnapToPixel(rowHeight + ToolbarVerticalPadding * 2.0f);
        }

        static ViewportRect FitRectToAspect(const ViewportRect &containerRect, float aspectRatio) {
            const float containerWidth = std::max(1.0f, SnapToPixel(containerRect.width));
            const float containerHeight = std::max(1.0f, SnapToPixel(containerRect.height));
            const float containerAspectRatio = containerWidth / containerHeight;

            ViewportRect fittedRect{};
            if (containerAspectRatio > aspectRatio) {
                fittedRect.height = containerHeight;
                fittedRect.width = std::max(1.0f, std::floor(containerHeight * aspectRatio));
                fittedRect.x = containerRect.x + std::floor((containerWidth - fittedRect.width) * 0.5f);
                fittedRect.y = containerRect.y;
            } else {
                fittedRect.width = containerWidth;
                fittedRect.height = std::max(1.0f, std::floor(containerWidth / aspectRatio));
                fittedRect.x = containerRect.x;
                fittedRect.y = containerRect.y + std::floor((containerHeight - fittedRect.height) * 0.5f);
            }

            fittedRect.x = SnapToPixel(fittedRect.x);
            fittedRect.y = SnapToPixel(fittedRect.y);
            fittedRect.width = std::max(1.0f, SnapToPixel(fittedRect.width));
            fittedRect.height = std::max(1.0f, SnapToPixel(fittedRect.height));
            return fittedRect;
        }

        static float ClampInspectorPanelWidth(float width) {
            const float maxInspectorPanelWidth = std::max(
                MinInspectorPanelWidth,
                editorPanelsWidth - InspectorHierarchySplitterWidth - MinHierarchyPanelWidth);
            return SnapToPixel(std::clamp(width, MinInspectorPanelWidth, maxInspectorPanelWidth));
        }

        static float ClampEditorPanelsWidth(float width, float totalWindowWidth) {
            const float minEditorPanelsWidth = MinInspectorPanelWidth + InspectorHierarchySplitterWidth + MinHierarchyPanelWidth;
            const float maxEditorPanelsWidth = std::max(
                minEditorPanelsWidth,
                totalWindowWidth - HierarchySceneSplitterWidth - MinScenePanelWidth);
            return SnapToPixel(std::clamp(width, minEditorPanelsWidth, maxEditorPanelsWidth));
        }

        static void DrawVerticalSplitter(const char *windowName,
                                         const char *buttonName,
                                         float x,
                                         float y,
                                         float height,
                                         float width,
                                         const std::function<void(float)> &onDrag) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

            constexpr ImGuiWindowFlags splitterWindowFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoScrollbar;

            ImGui::Begin(windowName, nullptr, splitterWindowFlags);
            ImGui::InvisibleButton(buttonName, ImVec2(width, height));

            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemActive()) {
                layoutInteractionActiveThisFrame = true;
                onDrag(ImGui::GetIO().MouseDelta.x);
            }

            const ImU32 splitterColor = ImGui::GetColorU32(
                ImGui::IsItemActive() ? ImGuiCol_SeparatorActive :
                (ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
            const ImVec2 splitterWindowPosition = ImGui::GetWindowPos();
            const float centerX = splitterWindowPosition.x + width * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(centerX, splitterWindowPosition.y),
                ImVec2(centerX, splitterWindowPosition.y + height),
                splitterColor,
                2.0f);

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        static void DrawSplitters() {
            DrawVerticalSplitter(
                "InspectorHierarchySplitter",
                "##InspectorHierarchySplitterButton",
                inspectorPanelRect.Right(),
                inspectorPanelRect.y,
                inspectorPanelRect.height,
                InspectorHierarchySplitterWidth,
                [](float deltaX) {
                    inspectorPanelWidth = ClampInspectorPanelWidth(inspectorPanelWidth + deltaX);
                });

            DrawVerticalSplitter(
                "HierarchySceneSplitter",
                "##HierarchySceneSplitterButton",
                editorPanelsWidth,
                hierarchyPanelRect.y,
                hierarchyPanelRect.height,
                HierarchySceneSplitterWidth,
                [](float deltaX) {
                    editorPanelsWidth = ClampEditorPanelsWidth(editorPanelsWidth + deltaX, viewportLayoutWidth);
                    inspectorPanelWidth = ClampInspectorPanelWidth(inspectorPanelWidth);
                });
        }

        static void ShowToolbarWindow(FrameInfo &frameInfo) {
            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                           ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoScrollWithMouse |
                                           ImGuiWindowFlags_NoCollapse;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ToolbarHorizontalPadding, ToolbarVerticalPadding));
            ImGui::SetNextWindowPos(ImVec2(toolbarRect.x, toolbarRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(toolbarRect.width, toolbarRect.height), ImGuiCond_Always);
            ImGui::Begin("Editor Toolbar", nullptr, windowFlags);

            const float rowHeight = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeight());
            const float rowOffsetY = std::max(0.0f, (toolbarRect.height - rowHeight) * 0.5f);
            ImGui::SetCursorPosY(rowOffsetY);

            const bool sceneDirty = frameInfo.scenePersistence != nullptr && frameInfo.scenePersistence->IsSceneDirty();
            if (!sceneDirty) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Save") && frameInfo.scenePersistence != nullptr) {
                frameInfo.scenePersistence->RequestSceneSave();
            }
            if (!sceneDirty) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine(0.0f, ToolbarItemSpacingX);
            ImGui::AlignTextToFramePadding();
            const ImVec4 statusColor = sceneDirty ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f) : ImVec4(0.45f, 0.90f, 0.45f, 1.0f);
            ImGui::TextColored(statusColor, "%s", sceneDirty ? "Unsaved Changes" : "Saved");
            ImGui::End();
            ImGui::PopStyleVar();
        }

        static void ShowHierarchyWindow(ECS::SceneRegistry *sceneRegistry,
                                        HierarchyService &hierarchyService,
                                        EntityCommandService &entityCommandService,
                                        EditorSelectionService &selectionService,
                                        TransformService &transformService,
                                        FrameInfo &frameInfo) {
            (void)transformService;
            if (sceneRegistry == nullptr || !sceneRegistry->IsAlive(hierarchyRenameState.entityId)) {
                CancelHierarchyRename();
            }
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(hierarchyPanelRect.x, hierarchyPanelRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(hierarchyPanelRect.width, hierarchyPanelRect.height), ImGuiCond_Always);

            ImGui::Begin("Hierarchy", nullptr, window_flags);
            ShowPerformance(frameInfo);
            if (sceneRegistry != nullptr && ImGui::TreeNode("Hierarchy")) {
                ImGui::BeginChild("HierarchyTreePanel", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                ShowHierarchyChildren(hierarchyService, std::nullopt, *sceneRegistry, selectionService, entityCommandService, frameInfo);
                ShowHierarchyBackgroundContextMenu(entityCommandService);
                ImGui::EndChild();
                entityCommandService.ApplyPendingCommands(*sceneRegistry, hierarchyService, transformService, selectionService, frameInfo);
                ImGui::TreePop();
            }
            ImGui::End();
        }

#ifdef RAY_TRACING
        static void ShowInspectorRayTracing(ImVec2 windowExtent,
                                            ECS::SceneRegistry *sceneRegistry,
                                            std::vector<EntityDesc> *gameObjectDescs,
                                            EntityCommandService &entityCommandService,
                                            EditorSelectionService &selectionService,
                                            TransformService &transformService,
                                            FrameInfo &frameInfo) {
            (void)windowExtent;
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(inspectorPanelRect.x, inspectorPanelRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(inspectorPanelRect.width, inspectorPanelRect.height), ImGuiCond_Always);
            ImGui::Begin("Inspector", nullptr, window_flags);

            ShowInspectorContent(sceneRegistry, gameObjectDescs, nullptr, entityCommandService, selectionService, transformService, frameInfo);
            ImGui::End();
        }
#else
        static void ShowInspectorRaster(ImVec2 windowExtent,
                                        ECS::SceneRegistry *sceneRegistry,
                                        Material::Map *materials,
                                        EntityCommandService &entityCommandService,
                                        EditorSelectionService &selectionService,
                                        TransformService &transformService,
                                        FrameInfo &frameInfo) {
            (void)windowExtent;
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(inspectorPanelRect.x, inspectorPanelRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(inspectorPanelRect.width, inspectorPanelRect.height), ImGuiCond_Always);
            ImGui::Begin("Inspector", nullptr, window_flags);

            ShowInspectorContent(sceneRegistry, nullptr, materials, entityCommandService, selectionService, transformService, frameInfo);
            ImGui::End();
        }
#endif

        static void ShowInspectorContent(ECS::SceneRegistry *sceneRegistry,
                                         std::vector<EntityDesc> *gameObjectDescs,
                                         Material::Map *materials,
                                         EntityCommandService &entityCommandService,
                                         EditorSelectionService &selectionService,
                                         TransformService &transformService,
                                         FrameInfo &frameInfo) {
            if (sceneRegistry == nullptr || !selectionService.HasSelection()) {
                return;
            }

            const id_t selectedId = selectionService.GetSelectedId();
            if (!sceneRegistry->IsAlive(selectedId)) {
                selectionService.ClearSelection();
                return;
            }

            EditorInspectorSystem::RenderSelectedEntity(sceneRegistry, transformService, entityCommandService, selectedId, gameObjectDescs, materials, frameInfo);
        }

        static void ShowPerformance(FrameInfo &frameInfo) {
            if (ImGui::TreeNode("Performance")) {
                char fpsText[50];
                std::string framePerSecondStr = std::to_string(static_cast<int>(1.0f / frameInfo.frameTime));
                sprintf(fpsText, "FPS: %s", framePerSecondStr.c_str());
                ImGui::Text(fpsText);
                ImGui::TreePop();
            }
        }

        static void ShowHierarchyChildren(const HierarchyService &hierarchyService,
                                          std::optional<id_t> parentEntityId,
                                          ECS::SceneRegistry &sceneRegistry,
                                          EditorSelectionService &selectionService,
                                          EntityCommandService &entityCommandService,
                                          FrameInfo &frameInfo) {
            const auto &children = parentEntityId.has_value()
                ? hierarchyService.GetTree().GetChildren(*parentEntityId)
                : hierarchyService.GetTree().GetRootChildren();

            for (const id_t entityId: children) {
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }

                const auto &grandChildren = hierarchyService.GetTree().GetChildren(entityId);
                ImGui::SetNextItemAllowOverlap();
                const bool isSelected = selectionService.IsSelected(entityId);
                const std::string &entityName = sceneRegistry.GetEntityName(entityId);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isSelected) {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                if (grandChildren.empty()) {
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }

                ImGui::PushID(static_cast<int>(entityId));
                const bool isOpen = ImGui::TreeNodeEx("##HierarchyNode", flags);

                const bool treeNodeClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

                if (!IsHierarchyRenameActive(entityId)) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(entityName.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || treeNodeClicked) {
                        selectionService.Select(entityId);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        BeginHierarchyRename(entityId, sceneRegistry, selectionService);
                    }
                } else {
                    ImGui::SameLine();
                    const float renameWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - 24.0f);
                    ImGui::SetNextItemWidth(renameWidth);
                    if (hierarchyRenameState.requestFocus) {
                        ImGui::SetKeyboardFocusHere();
                        hierarchyRenameState.requestFocus = false;
                    }

                    const bool confirm = ImGui::InputText(
                        "##HierarchyRenameInput",
                        hierarchyRenameState.buffer.data(),
                        hierarchyRenameState.buffer.size(),
                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool deactivated = ImGui::IsItemDeactivated();

                    if (treeNodeClicked || ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        selectionService.Select(entityId);
                    }

                    if (cancel) {
                        CancelHierarchyRename();
                    } else if (confirm || deactivated) {
                        CommitHierarchyRename(sceneRegistry, frameInfo);
                    }
                }

                if (treeNodeClicked && !IsHierarchyRenameActive(entityId)) {
                    selectionService.Select(entityId);
                }

                ShowHierarchyNodeContextMenu(entityId, sceneRegistry, selectionService, entityCommandService);
                DrawSelectionRect(entityId, sceneRegistry, frameInfo);
                ImGui::PopID();

                if (!grandChildren.empty() && isOpen) {
                    ShowHierarchyChildren(hierarchyService, entityId, sceneRegistry, selectionService, entityCommandService, frameInfo);
                    ImGui::TreePop();
                }
            }
        }

        static void ShowHierarchyNodeContextMenu(id_t entityId,
                                                 ECS::SceneRegistry &sceneRegistry,
                                                 EditorSelectionService &selectionService,
                                                 EntityCommandService &entityCommandService) {
            const std::string popupName = "HierarchyEntityContext##" + std::to_string(entityId);
            if (ImGui::BeginPopupContextItem(popupName.c_str(), ImGuiPopupFlags_MouseButtonRight)) {
                selectionService.Select(entityId);
                if (ImGui::MenuItem("Rename")) {
                    BeginHierarchyRename(entityId, sceneRegistry, selectionService);
                }
                if (ImGui::BeginMenu("Create Child")) {
                    ShowCreateEntityMenu(entityCommandService, entityId);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Delete")) {
                    entityCommandService.QueueDeleteEntity(entityId);
                }
                ImGui::EndPopup();
            }
        }

        static void ShowHierarchyBackgroundContextMenu(EntityCommandService &entityCommandService) {
            if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContextMenu",
                                               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                ShowCreateEntityMenu(entityCommandService, std::nullopt);
                ImGui::EndPopup();
            }
        }

        static void ShowCreateEntityMenu(EntityCommandService &entityCommandService, std::optional<id_t> parentEntityId) {
            if (ImGui::MenuItem("Create Empty")) {
                entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::CreateEmpty, parentEntityId);
            }

            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Cube, parentEntityId);
                }
                if (ImGui::MenuItem("Sphere")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Sphere, parentEntityId);
                }
                if (ImGui::MenuItem("Cylinder")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Cylinder, parentEntityId);
                }
                if (ImGui::MenuItem("Plane")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Plane, parentEntityId);
                }
                if (ImGui::MenuItem("Torus")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Torus, parentEntityId);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Light")) {
                if (ImGui::MenuItem("Directional Light")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::DirectionalLight, parentEntityId);
                }
                if (ImGui::MenuItem("Point Light")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::PointLight, parentEntityId);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Camera")) {
                entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::Camera, parentEntityId);
            }

            if (ImGui::BeginMenu("UI")) {
                if (ImGui::MenuItem("Canvas")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::UICanvas, parentEntityId);
                }
                if (ImGui::MenuItem("Panel")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::UIPanel, parentEntityId);
                }
                if (ImGui::MenuItem("Image")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::UIImage, parentEntityId);
                }
                if (ImGui::MenuItem("Text")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::UIText, parentEntityId);
                }
                if (ImGui::MenuItem("Button")) {
                    entityCommandService.QueueCreateEntity(EntityCommandService::EntityPreset::UIButton, parentEntityId);
                }
                ImGui::EndMenu();
            }
        }

        static void DrawSelectionRect(id_t entityId, ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo) {
            auto extent = ImGui::GetContentRegionAvail();
            ImGui::SameLine(extent.x);

            const bool currentActive = sceneRegistry.IsEntityActive(entityId);
            bool requestedActive = currentActive;

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::Checkbox(("##Checkbox" + std::to_string(entityId)).c_str(), &requestedActive);
            ImGui::PopStyleVar();

            if (requestedActive != currentActive) {
                sceneRegistry.SetEntityActive(entityId, requestedActive);
                EditorSceneUtils::MarkSceneDirty(frameInfo, true);
            }
        }

        static bool IsHierarchyRenameActive(id_t entityId) {
            return hierarchyRenameState.entityId == entityId;
        }

        static void BeginHierarchyRename(id_t entityId,
                                         ECS::SceneRegistry &sceneRegistry,
                                         EditorSelectionService &selectionService) {
            if (!sceneRegistry.IsAlive(entityId)) {
                return;
            }

            selectionService.Select(entityId);
            hierarchyRenameState.entityId = entityId;
            hierarchyRenameState.requestFocus = true;
            const std::string &entityName = sceneRegistry.GetEntityName(entityId);
            std::snprintf(
                hierarchyRenameState.buffer.data(),
                hierarchyRenameState.buffer.size(),
                "%s",
                entityName.c_str());
        }

        static void CancelHierarchyRename() {
            hierarchyRenameState = {};
        }

        static void CommitHierarchyRename(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo) {
            if (!sceneRegistry.IsAlive(hierarchyRenameState.entityId)) {
                CancelHierarchyRename();
                return;
            }

            std::string newName = hierarchyRenameState.buffer.data();
            TrimInPlace(newName);
            if (!newName.empty() && newName != sceneRegistry.GetEntityName(hierarchyRenameState.entityId)) {
                sceneRegistry.SetEntityName(hierarchyRenameState.entityId, std::move(newName));
                EditorSceneUtils::MarkSceneDirty(frameInfo, false);
            }
            CancelHierarchyRename();
        }

        static void TrimInPlace(std::string &value) {
            const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        }
    };
}
