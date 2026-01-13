#pragma once

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "ComponentFactory.hpp"
#include "Components/CameraComponent.hpp"
#include "Components/LightComponent.hpp"
#include "Components/MeshRendererComponent.hpp"
#include "Components/TransformComponent.hpp"
#include "Components/UIComponent.hpp"
#include "Device.hpp"
#include "ECS/HierarchyTree.hpp"
#include "ECS/SceneRegistry.hpp"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_glfw.h"
#include "Imgui/imgui_impl_vulkan.h"
#include "Model.hpp"
#include "MyWindow.hpp"
#ifdef RAY_TRACING
#include "RayTracing/BLAS.hpp"
#endif
#include "Renderer.h"

namespace FeatherVK {
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
                               std::vector<EntityDesc> *gameObjectDescs,
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
        enum class EntityPreset {
            CreateEmpty,
            Cube,
            DirectionalLight,
            PointLight,
            Camera,
            UICanvas,
            UIPanel,
            UIImage,
            UIText,
            UIButton
        };

        struct PendingCreateRequest {
            EntityPreset preset = EntityPreset::CreateEmpty;
            std::optional<id_t> parentEntityId{};
        };

        struct DeferredDestroyRequest {
            std::vector<id_t> entityIds{};
            int framesRemaining = SwapChain::MAX_FRAMES_IN_FLIGHT + 1;
        };

        inline static VkDescriptorPool imguiDescPool{};
        inline static bool bSelected = false;
        inline static id_t selectedId = -1;
        inline static std::optional<PendingCreateRequest> pendingCreateRequest{};
        inline static std::optional<id_t> pendingDeleteRequest{};
        inline static std::vector<DeferredDestroyRequest> deferredDestroyRequests{};
        inline static std::unordered_set<id_t> pendingDestroyedEntities{};

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
                ImGui::BeginChild("HierarchyTreePanel", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                ShowHierarchyTree(hierarchyTree->GetRoot(), *sceneRegistry);
                ShowHierarchyBackgroundContextMenu();
                ImGui::EndChild();
                ApplyPendingHierarchyActions(*sceneRegistry, *hierarchyTree, frameInfo.materials, frameInfo);
                ImGui::TreePop();
            }
            ImGui::End();
        }

#ifdef RAY_TRACING
        static void ShowInspectorRayTracing(ImVec2 windowExtent,
                                            ECS::SceneRegistry *sceneRegistry,
                                            std::vector<EntityDesc> *gameObjectDescs,
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
                                         std::vector<EntityDesc> *gameObjectDescs,
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
            if (node == nullptr) {
                return;
            }

            for (auto *child: node->children) {
                if (child == nullptr) {
                    continue;
                }

                const id_t entityId = static_cast<id_t>(child->id);
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }

                ImGui::SetNextItemAllowOverlap();
                const bool isSelected = selectedId == entityId;
                const std::string &entityName = sceneRegistry.GetEntityName(entityId);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isSelected) {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                if (child->children.empty()) {
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }

                const bool isOpen = ImGui::TreeNodeEx(
                        reinterpret_cast<void *>(static_cast<intptr_t>(entityId)),
                        flags,
                        "%s",
                        entityName.c_str());

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    SetSelectedId(entityId);
                }

                ShowHierarchyNodeContextMenu(entityId);
                DrawSelectionRect(entityId, sceneRegistry);

                if (!child->children.empty() && isOpen) {
                    ShowHierarchyTree(child, sceneRegistry);
                    ImGui::TreePop();
                }
            }
        }

        static void ShowHierarchyNodeContextMenu(id_t entityId) {
            const std::string popupName = "HierarchyEntityContext##" + std::to_string(entityId);
            if (ImGui::BeginPopupContextItem(popupName.c_str(), ImGuiPopupFlags_MouseButtonRight)) {
                SetSelectedId(entityId);
                if (ImGui::BeginMenu("Create Child")) {
                    ShowCreateEntityMenu(entityId);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Delete")) {
                    pendingDeleteRequest = entityId;
                }
                ImGui::EndPopup();
            }
        }

        static void ShowHierarchyBackgroundContextMenu() {
            if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContextMenu",
                                               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                ShowCreateEntityMenu(std::nullopt);
                ImGui::EndPopup();
            }
        }

        static void ShowCreateEntityMenu(std::optional<id_t> parentEntityId) {
            if (ImGui::MenuItem("Create Empty")) {
                QueueCreateEntity(EntityPreset::CreateEmpty, parentEntityId);
            }

            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) {
                    QueueCreateEntity(EntityPreset::Cube, parentEntityId);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Light")) {
                if (ImGui::MenuItem("Directional Light")) {
                    QueueCreateEntity(EntityPreset::DirectionalLight, parentEntityId);
                }
                if (ImGui::MenuItem("Point Light")) {
                    QueueCreateEntity(EntityPreset::PointLight, parentEntityId);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Camera")) {
                QueueCreateEntity(EntityPreset::Camera, parentEntityId);
            }

            if (ImGui::BeginMenu("UI")) {
                if (ImGui::MenuItem("Canvas")) {
                    QueueCreateEntity(EntityPreset::UICanvas, parentEntityId);
                }
                if (ImGui::MenuItem("Panel")) {
                    QueueCreateEntity(EntityPreset::UIPanel, parentEntityId);
                }
                if (ImGui::MenuItem("Image")) {
                    QueueCreateEntity(EntityPreset::UIImage, parentEntityId);
                }
                if (ImGui::MenuItem("Text")) {
                    QueueCreateEntity(EntityPreset::UIText, parentEntityId);
                }
                if (ImGui::MenuItem("Button")) {
                    QueueCreateEntity(EntityPreset::UIButton, parentEntityId);
                }
                ImGui::EndMenu();
            }
        }

        static void QueueCreateEntity(EntityPreset preset, std::optional<id_t> parentEntityId) {
            pendingCreateRequest = PendingCreateRequest{preset, parentEntityId};
        }

        static void ApplyPendingHierarchyActions(ECS::SceneRegistry &sceneRegistry,
                                                 HierarchyTree &hierarchyTree,
                                                 Material::Map &materials,
                                                 FrameInfo &frameInfo) {
            ProcessDeferredDestroy(sceneRegistry, frameInfo);

            if (pendingDeleteRequest.has_value()) {
                DestroyEntitySubtree(sceneRegistry, hierarchyTree, *pendingDeleteRequest, frameInfo);
                pendingDeleteRequest.reset();
            }

            if (pendingCreateRequest.has_value()) {
                const PendingCreateRequest request = *pendingCreateRequest;
                pendingCreateRequest.reset();
                const id_t createdEntityId =
                        CreateEntityByPreset(sceneRegistry, hierarchyTree, materials, request.preset, request.parentEntityId, frameInfo);
                SetSelectedId(createdEntityId);
            }
        }

        static id_t CreateEntityByPreset(ECS::SceneRegistry &sceneRegistry,
                                         HierarchyTree &hierarchyTree,
                                         Material::Map &materials,
                                         EntityPreset preset,
                                         std::optional<id_t> parentEntityId,
                                         FrameInfo &frameInfo) {
            if (parentEntityId.has_value() && !sceneRegistry.IsAlive(*parentEntityId)) {
                parentEntityId.reset();
            }

            if (!parentEntityId.has_value() && IsUIElementPreset(preset) && preset != EntityPreset::UICanvas) {
                parentEntityId = FindFirstCanvasEntity(sceneRegistry);
            }

            const std::string entityName = GenerateUniqueEntityName(sceneRegistry, GetPresetBaseName(preset));
            const id_t entityId = sceneRegistry.CreateEntity(entityName, true);

            auto *transform = sceneRegistry.AddOwnedComponent<TransformComponent>(entityId);
            transform->SetTransformId(static_cast<int32_t>(entityId));

            if (parentEntityId.has_value()) {
                TransformComponent *parentTransform = nullptr;
                if (sceneRegistry.TryGetComponent(*parentEntityId, parentTransform) && parentTransform != nullptr) {
                    parentTransform->AddChild(transform);
                } else {
                    parentEntityId.reset();
                }
            }

            const int parentNodeId = parentEntityId.has_value() ? static_cast<int>(*parentEntityId) : HierarchyTree::ROOT_ID;
            hierarchyTree.AddNode(parentNodeId, static_cast<int>(entityId), transform->GetTransformId());

            switch (preset) {
                case EntityPreset::Cube:
                    AddCubeRendererComponent(sceneRegistry, entityId, materials);
                    break;
                case EntityPreset::DirectionalLight: {
                    auto *lightComponent = sceneRegistry.AddOwnedComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::DIRECTIONAL_LIGHT);
                    transform->SetTranslation(glm::vec3{0.0f, 2.0f, 0.0f});
                    break;
                }
                case EntityPreset::PointLight: {
                    auto *lightComponent = sceneRegistry.AddOwnedComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::POINT_LIGHT);
                    transform->SetTranslation(glm::vec3{0.0f, 2.0f, 0.0f});
                    break;
                }
                case EntityPreset::Camera:
                    sceneRegistry.AddOwnedComponent<CameraComponent>(entityId);
                    transform->SetTranslation(glm::vec3{0.0f, 1.5f, -5.0f});
                    break;
                case EntityPreset::UICanvas:
                    sceneRegistry.AddOwnedComponent<UIComponent>(entityId, UIComponent::ElementType::Canvas);
                    break;
                case EntityPreset::UIPanel:
                    sceneRegistry.AddOwnedComponent<UIComponent>(entityId, UIComponent::ElementType::Panel);
                    break;
                case EntityPreset::UIImage:
                    sceneRegistry.AddOwnedComponent<UIComponent>(entityId, UIComponent::ElementType::Image);
                    break;
                case EntityPreset::UIText:
                    sceneRegistry.AddOwnedComponent<UIComponent>(entityId, UIComponent::ElementType::Text);
                    break;
                case EntityPreset::UIButton:
                    sceneRegistry.AddOwnedComponent<UIComponent>(entityId, UIComponent::ElementType::Button);
                    break;
                case EntityPreset::CreateEmpty:
                default:
                    break;
            }

            for (Component *component: sceneRegistry.GetComponents(entityId)) {
                if (component != nullptr) {
                    component->OnLoad(entityId, sceneRegistry);
                }
            }
            for (Component *component: sceneRegistry.GetComponents(entityId)) {
                if (component != nullptr) {
                    component->Loaded(entityId, sceneRegistry);
                }
            }

            frameInfo.sceneUpdated = true;
            return entityId;
        }

        static void AddCubeRendererComponent(ECS::SceneRegistry &sceneRegistry,
                                             id_t entityId,
                                             Material::Map &materials) {
            constexpr const char *CubeModelName = "cube.obj";
            constexpr const char *GeneratedCubeModelName = "generated_cube";
            std::shared_ptr<Model> model = nullptr;

            auto modelIt = Model::models.find(CubeModelName);
            if (modelIt != Model::models.end()) {
                model = modelIt->second;
            }

            if (model == nullptr) {
                const std::string cubeModelPath = Model::GetBaseModelsPath() + std::string(CubeModelName);
                try {
                    model = Model::createModelFromFile(*Device::getDeviceSingleton(), cubeModelPath);
                    if (model != nullptr) {
                        model->SetName(CubeModelName);
                        Model::models[CubeModelName] = model;
                    }
                } catch (const std::exception &exception) {
                    std::cerr << "[Editor] Failed to load cube model '" << cubeModelPath << "': " << exception.what() << "\n";
                    model = nullptr;
                }
            }

            if (!IsCubeModelRenderable(model)) {
                std::cerr << "[Editor] cube.obj is unavailable or invalid, falling back to runtime generated cube.\n";
                auto generatedModelIt = Model::models.find(GeneratedCubeModelName);
                if (generatedModelIt != Model::models.end()) {
                    model = generatedModelIt->second;
                } else {
                    model = CreateRuntimeCubeModel();
                    if (model != nullptr) {
                        Model::models[GeneratedCubeModelName] = model;
                    }
                }
            }

            if (model == nullptr) {
                std::cerr << "[Editor] Create Cube failed: no valid cube model is available.\n";
                return;
            }

#ifdef RAY_TRACING
            if (BLAS::blasBuildInfoMap.find(model->getIndexReference()) == BLAS::blasBuildInfoMap.end()) {
                BLAS::modelToBLASInput(model);
                BLAS::buildBLAS(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
            }
#endif

            const Material::id_t materialId = PickDefaultMeshMaterialId(materials);
            sceneRegistry.AddOwnedComponent<MeshRendererComponent>(entityId, model, materialId);
            std::cerr << "[Editor] Created Cube entity " << entityId
                      << " model='" << model->GetName() << "'"
                      << " materialId=" << materialId << "\n";
        }

        static void DestroyEntitySubtree(ECS::SceneRegistry &sceneRegistry,
                                         HierarchyTree &hierarchyTree,
                                         id_t entityId,
                                         FrameInfo &frameInfo) {
            if (!sceneRegistry.IsAlive(entityId) || pendingDestroyedEntities.count(entityId) > 0) {
                return;
            }

            TransformComponent *rootTransform = nullptr;
            if (sceneRegistry.TryGetComponent(entityId, rootTransform) && rootTransform != nullptr) {
                rootTransform->DetachFromParent();
            }

            std::vector<int> destroyOrder{};
            hierarchyTree.CollectSubtreeIds(static_cast<int>(entityId), destroyOrder);
            if (destroyOrder.empty()) {
                destroyOrder.push_back(static_cast<int>(entityId));
            }

            DeferredDestroyRequest destroyRequest{};
            destroyRequest.entityIds.reserve(destroyOrder.size());
            for (const int destroyIdInt: destroyOrder) {
                const id_t destroyId = static_cast<id_t>(destroyIdInt);
                destroyRequest.entityIds.push_back(destroyId);
                pendingDestroyedEntities.insert(destroyId);
                if (selectedId == destroyId) {
                    ClearSelection();
                }

                if (auto *meta = sceneRegistry.TryGetEntityMeta(destroyId)) {
                    meta->active = false;
                    meta->onDisabled = true;
                    meta->onEnabled = false;
                }
            }

            hierarchyTree.RemoveNode(static_cast<int>(entityId));
            deferredDestroyRequests.emplace_back(std::move(destroyRequest));
            frameInfo.sceneUpdated = true;
        }

        static void ProcessDeferredDestroy(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo) {
            for (auto requestIt = deferredDestroyRequests.begin(); requestIt != deferredDestroyRequests.end();) {
                if (--requestIt->framesRemaining > 0) {
                    ++requestIt;
                    continue;
                }

                for (const id_t entityId: requestIt->entityIds) {
                    pendingDestroyedEntities.erase(entityId);
                    if (selectedId == entityId) {
                        ClearSelection();
                    }
                    sceneRegistry.DestroyEntity(entityId);
                }
                frameInfo.sceneUpdated = true;
                requestIt = deferredDestroyRequests.erase(requestIt);
            }
        }

        static bool IsCubeModelRenderable(const std::shared_ptr<Model> &model) {
            return model != nullptr &&
                   model->getVertexCount() >= 24 &&
                   model->getIndexCount() >= 36;
        }

        static std::shared_ptr<Model> CreateRuntimeCubeModel() {
            Model::Builder builder{};
            builder.vertices.reserve(24);
            builder.indices.reserve(36);
            builder.maxRadius = glm::length(glm::vec3(0.5f));

            struct FaceDefinition {
                glm::vec3 normal;
                std::array<glm::vec3, 4> positions;
            };

            const std::array<FaceDefinition, 6> faces{{
                    {{ 0.0f,  0.0f,  1.0f}, {{{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}}},
                    {{ 0.0f,  0.0f, -1.0f}, {{{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}}},
                    {{ 1.0f,  0.0f,  0.0f}, {{{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}}},
                    {{-1.0f,  0.0f,  0.0f}, {{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}}},
                    {{ 0.0f,  1.0f,  0.0f}, {{{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}}},
                    {{ 0.0f, -1.0f,  0.0f}, {{{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}}}
            }};
            const std::array<glm::vec2, 4> uvs{{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};

            for (const auto &face: faces) {
                const uint32_t baseIndex = static_cast<uint32_t>(builder.vertices.size());
                for (size_t vertexIndex = 0; vertexIndex < face.positions.size(); ++vertexIndex) {
                    Model::Vertex vertex{};
                    vertex.position = face.positions[vertexIndex];
                    vertex.color = glm::vec3(1.0f);
                    vertex.normal = face.normal;
                    vertex.smoothedNormal = face.normal;
                    vertex.uv = uvs[vertexIndex];
                    builder.vertices.push_back(vertex);
                }

                builder.indices.push_back(baseIndex + 0);
                builder.indices.push_back(baseIndex + 1);
                builder.indices.push_back(baseIndex + 2);
                builder.indices.push_back(baseIndex + 0);
                builder.indices.push_back(baseIndex + 2);
                builder.indices.push_back(baseIndex + 3);
            }

            auto model = std::make_shared<Model>(*Device::getDeviceSingleton(), builder);
            model->SetName("generated_cube");
            std::cerr << "[Editor] Generated runtime cube mesh vertices=" << model->getVertexCount()
                      << " indices=" << model->getIndexCount() << "\n";
            return model;
        }

        static std::optional<id_t> FindFirstCanvasEntity(ECS::SceneRegistry &sceneRegistry) {
            for (const id_t entityId: sceneRegistry.GetEntityOrder()) {
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }
                UIComponent *uiComponent = nullptr;
                if (sceneRegistry.TryGetComponent(entityId, uiComponent) && uiComponent != nullptr &&
                    uiComponent->GetElementType() == UIComponent::ElementType::Canvas) {
                    return entityId;
                }
            }
            return std::nullopt;
        }

        static Material::id_t PickDefaultMeshMaterialId(const Material::Map &materials) {
            Material::id_t fallbackId = std::numeric_limits<Material::id_t>::max();
            for (const auto &materialEntry: materials) {
                if (materialEntry.first < 0 || materialEntry.second == nullptr) {
                    continue;
                }

                fallbackId = std::min(fallbackId, materialEntry.first);
                const std::string &pipelineCategory = materialEntry.second->getPipelineCategory();
#ifdef RAY_TRACING
                if (pipelineCategory == "RayTracing") {
                    return materialEntry.first;
                }
#else
                if (pipelineCategory == "Opaque") {
                    return materialEntry.first;
                }
#endif
            }

            if (fallbackId != std::numeric_limits<Material::id_t>::max()) {
                return fallbackId;
            }
#ifdef RAY_TRACING
            return 2;
#else
            return materials.empty() ? 0 : materials.begin()->first;
#endif
        }

        static std::string GenerateUniqueEntityName(const ECS::SceneRegistry &sceneRegistry, std::string_view baseName) {
            std::unordered_set<std::string> existingNames{};
            for (const id_t entityId: sceneRegistry.GetEntityOrder()) {
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }
                if (pendingDestroyedEntities.count(entityId) > 0) {
                    continue;
                }
                existingNames.insert(sceneRegistry.GetEntityName(entityId));
            }

            std::string candidate(baseName);
            if (existingNames.count(candidate) == 0) {
                return candidate;
            }

            uint32_t suffix = 1;
            while (true) {
                candidate = std::string(baseName) + " (" + std::to_string(suffix) + ")";
                if (existingNames.count(candidate) == 0) {
                    return candidate;
                }
                ++suffix;
            }
        }

        static std::string GetPresetBaseName(EntityPreset preset) {
            switch (preset) {
                case EntityPreset::CreateEmpty:
                    return "Empty";
                case EntityPreset::Cube:
                    return "Cube";
                case EntityPreset::DirectionalLight:
                    return "Directional Light";
                case EntityPreset::PointLight:
                    return "Point Light";
                case EntityPreset::Camera:
                    return "Camera";
                case EntityPreset::UICanvas:
                    return "Canvas";
                case EntityPreset::UIPanel:
                    return "Panel";
                case EntityPreset::UIImage:
                    return "Image";
                case EntityPreset::UIText:
                    return "Text";
                case EntityPreset::UIButton:
                    return "Button";
            }
            return "Entity";
        }

        static bool IsUIElementPreset(EntityPreset preset) {
            switch (preset) {
                case EntityPreset::UICanvas:
                case EntityPreset::UIPanel:
                case EntityPreset::UIImage:
                case EntityPreset::UIText:
                case EntityPreset::UIButton:
                    return true;
                default:
                    return false;
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
