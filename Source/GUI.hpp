#pragma once

#include <iomanip>

#include "ComponentFactory.hpp"
#include "GameObject.hpp"
#include "Components/TransformComponent.hpp"
#include "Device.hpp"
#include "ECS/SceneRegistry.hpp"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_glfw.h"
#include "Imgui/imgui_impl_vulkan.h"
#include "MyWindow.hpp"
#include "Renderer.h"

namespace Kaamoo {
    class GUI {
    public:
        GUI() = delete;

        static id_t GetSelectedId() { return selectedId; }

        static bool HasSelection() { return bSelected && selectedId >= 0; }

        static void SetSelectedId(id_t id) {
            selectedId = id;
            bSelected = id >= 0;
        }

        static void ClearSelection() {
            selectedId = -1;
            bSelected = false;
        }

        static void Destroy() {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            vkDestroyDescriptorPool(Device::getDeviceSingleton()->device(), imguiDescPool, nullptr);
        }

        static void Init(Renderer &renderer, MyWindow &myWindow) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto device = Device::getDeviceSingleton();
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
            vkCreateDescriptorPool(device->device(), &pool_info, nullptr, &imguiDescPool);

            ImGui::StyleColorsDark();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\arial.ttf)", 16.0f);
            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.Instance = device->getInstance();
            init_info.PhysicalDevice = device->getPhysicalDevice();
            init_info.Device = device->device();
            init_info.QueueFamily = device->getQueueFamilyIndices().graphicsFamily;
            init_info.Queue = device->graphicsQueue();
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
            io.DisplaySize = ImVec2(UI_LEFT_WIDTH, windowExtent.y);
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

#ifdef RAY_TRACING
        static void ShowWindow(ImVec2 windowExtent,
                               ECS::SceneRegistry *sceneRegistry,
                               std::vector<GameObjectDesc> *gameObjectDescs,
                               HierarchyTree *hierarchyTree,
                               FrameInfo &frameInfo) {
            ShowWindowCommon(windowExtent, sceneRegistry, hierarchyTree, frameInfo);
            ShowInspectorRayTracing(windowExtent, sceneRegistry, gameObjectDescs, frameInfo);
        }
#else
        static void ShowWindow(ImVec2 windowExtent,
                               ECS::SceneRegistry *sceneRegistry,
                               Material::Map *materials,
                               HierarchyTree *hierarchyTree,
                               FrameInfo &frameInfo) {
            ShowWindowCommon(windowExtent, sceneRegistry, hierarchyTree, frameInfo);
            ShowInspectorRaster(windowExtent, sceneRegistry, materials, frameInfo);
        }
#endif

        static void EndFrame(VkCommandBuffer &commandBuffer) {
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
            ImGui::EndFrame();
        }

    private:
        inline static VkDescriptorPool imguiDescPool{};
        inline static bool bSelected = false;
        inline static id_t selectedId = -1;

        static void ShowWindowCommon(ImVec2 windowExtent,
                                     ECS::SceneRegistry *sceneRegistry,
                                     HierarchyTree *hierarchyTree,
                                     FrameInfo &frameInfo) {
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(UI_LEFT_WIDTH_2, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(UI_LEFT_WIDTH, windowExtent.y), ImGuiCond_Always);

            ImGui::Begin("Scene", nullptr, window_flags);
            ShowPerformance(frameInfo);
            if (sceneRegistry != nullptr && hierarchyTree != nullptr && ImGui::TreeNode("Hierarchy")) {
                ShowHierarchyTree(hierarchyTree->GetRoot(), *sceneRegistry);
                ImGui::TreePop();
            }
            ImGui::End();
        }

#ifdef RAY_TRACING
        static void ShowInspectorRayTracing(ImVec2 windowExtent,
                                            ECS::SceneRegistry *sceneRegistry,
                                            std::vector<GameObjectDesc> *gameObjectDescs,
                                            FrameInfo &frameInfo) {
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(UI_LEFT_WIDTH_2, windowExtent.y), ImGuiCond_Always);
            ImGui::Begin("Inspector", nullptr, window_flags);

            ShowInspectorContent(sceneRegistry, gameObjectDescs, nullptr, frameInfo);
            ImGui::End();
        }
#else
        static void ShowInspectorRaster(ImVec2 windowExtent,
                                        ECS::SceneRegistry *sceneRegistry,
                                        Material::Map *materials,
                                        FrameInfo &frameInfo) {
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoResize;

            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(UI_LEFT_WIDTH_2, windowExtent.y), ImGuiCond_Always);
            ImGui::Begin("Inspector", nullptr, window_flags);

            ShowInspectorContent(sceneRegistry, nullptr, materials, frameInfo);
            ImGui::End();
        }
#endif

        static void ShowInspectorContent(ECS::SceneRegistry *sceneRegistry,
                                         std::vector<GameObjectDesc> *gameObjectDescs,
                                         Material::Map *materials,
                                         FrameInfo &frameInfo) {
            if (!bSelected || sceneRegistry == nullptr || !sceneRegistry->IsAlive(selectedId)) {
                return;
            }

            ImGui::Text("Name:");
            ImGui::SameLine(70);
            ImGui::Text(sceneRegistry->GetEntityName(selectedId).c_str());

            TransformComponent *transform = nullptr;
            if (sceneRegistry->TryGetComponent(selectedId, transform) && transform != nullptr) {
                if (ImGui::TreeNode("Transform")) {
                    ImGui::Text("Position:");
                    ImGui::SameLine(90);
                    glm::vec3 tempPosition = transform->GetRelativeTranslation();
                    ImGui::InputFloat3("##Position", &tempPosition.x);
                    transform->SetTranslation(tempPosition);

                    ImGui::Text("Rotation:");
                    ImGui::SameLine(90);
                    glm::vec3 rotationByDegrees = glm::degrees(transform->GetRelativeRotation());
                    ImGui::InputFloat3("##Rotation", &rotationByDegrees.x);
                    transform->SetRotation(glm::radians(rotationByDegrees));

                    ImGui::Text("Scale:");
                    ImGui::SameLine(90);
                    glm::vec3 tempScale = transform->GetRelativeScale();
                    ImGui::InputFloat3("##Scale", &tempScale.x);
                    transform->SetScale(tempScale);
                    ImGui::TreePop();
                }
            }

            if (ImGui::TreeNode("Components")) {
                for (auto *component: sceneRegistry->GetComponents(selectedId)) {
                    if (component == nullptr || component->GetName() == ComponentName::TransformComponent) {
                        continue;
                    }
                    if (ImGui::TreeNode(component->GetName().c_str())) {
#ifdef RAY_TRACING
                        component->SetUI(gameObjectDescs, frameInfo);
#else
                        component->SetUI(materials, frameInfo);
#endif
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
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

        static void ShowHierarchyTree(HierarchyTree::Node *node, ECS::SceneRegistry &sceneRegistry) {
            for (auto &child: node->children) {
                const id_t entityId = static_cast<id_t>(child->id);
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }

                ImGui::SetNextItemAllowOverlap();
                const bool isSelected = selectedId == entityId;
                const std::string &entityName = sceneRegistry.GetEntityName(entityId);
                if (!child->children.empty()) {
                    if (ImGui::TreeNodeEx(entityName.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | (isSelected ? ImGuiTreeNodeFlags_Selected : 0))) {
                        if (ImGui::IsItemClicked()) {
                            selectedId = entityId;
                            bSelected = true;
                        }
                        DrawSelectionRect(entityId, sceneRegistry);
                        ShowHierarchyTree(child, sceneRegistry);
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::TreeNodeEx(entityName.c_str(),
                                      ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                      (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
                    if (ImGui::IsItemClicked()) {
                        selectedId = entityId;
                        bSelected = true;
                    }
                    DrawSelectionRect(entityId, sceneRegistry);
                }
            }
        }

        static void DrawSelectionRect(id_t entityId, ECS::SceneRegistry &sceneRegistry) {
            auto extent = ImGui::GetContentRegionAvail();
            ImGui::PushID(static_cast<int>(entityId));
            ImGui::SameLine(extent.x);

            const bool currentActive = sceneRegistry.IsEntityActive(entityId);
            bool requestedActive = currentActive;

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::Checkbox(("##Checkbox" + std::to_string(entityId)).c_str(), &requestedActive);
            ImGui::PopStyleVar();

            if (requestedActive != currentActive) {
                if (auto *meta = sceneRegistry.TryGetEntityMeta(entityId)) {
                    meta->onDisabled = !requestedActive;
                    meta->onEnabled = requestedActive;
                }
            }

            ImGui::PopID();
        }
    };
}


