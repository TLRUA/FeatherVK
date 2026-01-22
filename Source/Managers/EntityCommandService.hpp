#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "../Components/CameraComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RayTracingInstanceComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Components/UIComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Model.hpp"
#include "../StructureInfos.h"
#include "../SwapChain.hpp"
#include "EditorSelectionService.hpp"
#include "HierarchyService.hpp"
#include "ModelRepository.hpp"
#include "TransformService.hpp"

#ifdef RAY_TRACING
#include "RayTracingSceneContext.hpp"
#endif

namespace FeatherVK {
    class EntityCommandService {
    public:
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

        void SetDependencies(ModelRepository &modelRepository
#ifdef RAY_TRACING
            , RayTracingSceneContext *rayTracingSceneContext
#endif
        ) {
            m_modelRepository = &modelRepository;
#ifdef RAY_TRACING
            m_rayTracingSceneContext = rayTracingSceneContext;
#endif
        }

        void Reset() {
            m_pendingCreateRequest.reset();
            m_pendingDeleteRequest.reset();
            m_deferredDestroyRequests.clear();
            m_pendingDestroyedEntities.clear();
        }

        void QueueCreateEntity(EntityPreset preset, std::optional<id_t> parentEntityId) {
            m_pendingCreateRequest = PendingCreateRequest{preset, parentEntityId};
        }

        void QueueDeleteEntity(id_t entityId) {
            m_pendingDeleteRequest = entityId;
        }

        bool IsPendingDestroy(id_t entityId) const {
            return m_pendingDestroyedEntities.count(entityId) > 0;
        }

        void ApplyPendingCommands(ECS::SceneRegistry &sceneRegistry,
                                  HierarchyService &hierarchyService,
                                  TransformService &transformService,
                                  EditorSelectionService &selectionService,
                                  FrameInfo &frameInfo) {
            ProcessDeferredDestroy(sceneRegistry, selectionService, frameInfo);

            if (m_pendingDeleteRequest.has_value()) {
                DestroyEntitySubtree(sceneRegistry, hierarchyService, transformService, *m_pendingDeleteRequest, selectionService, frameInfo);
                m_pendingDeleteRequest.reset();
            }

            if (m_pendingCreateRequest.has_value()) {
                const PendingCreateRequest request = *m_pendingCreateRequest;
                m_pendingCreateRequest.reset();
                const id_t createdEntityId =
                    CreateEntityByPreset(sceneRegistry, hierarchyService, transformService, request.preset, request.parentEntityId, frameInfo);
                selectionService.Select(createdEntityId);
            }
        }

    private:
        struct PendingCreateRequest {
            EntityPreset preset = EntityPreset::CreateEmpty;
            std::optional<id_t> parentEntityId{};
        };

        struct DeferredDestroyRequest {
            std::vector<id_t> entityIds{};
            int framesRemaining = SwapChain::MAX_FRAMES_IN_FLIGHT + 1;
        };

        id_t CreateEntityByPreset(ECS::SceneRegistry &sceneRegistry,
                                  HierarchyService &hierarchyService,
                                  TransformService &transformService,
                                  EntityPreset preset,
                                  std::optional<id_t> parentEntityId,
                                  FrameInfo &frameInfo) {
            if (parentEntityId.has_value() && !sceneRegistry.IsAlive(*parentEntityId)) {
                parentEntityId.reset();
            }

            if (!parentEntityId.has_value() && IsUiElementPreset(preset) && preset != EntityPreset::UICanvas) {
                parentEntityId = FindFirstCanvasEntity(sceneRegistry);
            }

            const std::string entityName = GenerateUniqueEntityName(sceneRegistry, GetPresetBaseName(preset));
            const id_t entityId = sceneRegistry.CreateEntity(entityName, true);
            sceneRegistry.EmplaceComponent<TransformComponent>(entityId);
            transformService.SetParent(sceneRegistry, hierarchyService, entityId, parentEntityId);

            switch (preset) {
                case EntityPreset::Cube:
                    AddCubeRendererComponent(sceneRegistry, entityId, frameInfo.materials);
                    break;
                case EntityPreset::DirectionalLight: {
                    auto *lightComponent = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::DIRECTIONAL_LIGHT);
                    transformService.SetTranslation(sceneRegistry, entityId, glm::vec3{0.0f, 2.0f, 0.0f});
                    break;
                }
                case EntityPreset::PointLight: {
                    auto *lightComponent = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::POINT_LIGHT);
                    transformService.SetTranslation(sceneRegistry, entityId, glm::vec3{0.0f, 2.0f, 0.0f});
                    break;
                }
                case EntityPreset::Camera:
                    sceneRegistry.EmplaceComponent<CameraComponent>(entityId);
                    transformService.SetTranslation(sceneRegistry, entityId, glm::vec3{0.0f, 1.5f, -5.0f});
                    break;
                case EntityPreset::UICanvas:
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, UIComponent::ElementType::Canvas);
                    break;
                case EntityPreset::UIPanel:
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, UIComponent::ElementType::Panel);
                    break;
                case EntityPreset::UIImage:
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, UIComponent::ElementType::Image);
                    break;
                case EntityPreset::UIText:
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, UIComponent::ElementType::Text);
                    break;
                case EntityPreset::UIButton:
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, UIComponent::ElementType::Button);
                    break;
                case EntityPreset::CreateEmpty:
                default:
                    break;
            }

            frameInfo.sceneUpdated = true;
            return entityId;
        }

        void AddCubeRendererComponent(ECS::SceneRegistry &sceneRegistry,
                                      id_t entityId,
                                      Material::Map &materials) {
            if (m_modelRepository == nullptr) {
                std::cerr << "[Editor] Create Cube failed: model repository is not configured.\n";
                return;
            }

            constexpr const char *CubeModelName = "cube.obj";
            constexpr const char *GeneratedCubeModelName = "generated_cube";
            std::shared_ptr<Model> model = m_modelRepository->Find(CubeModelName);

            if (model == nullptr) {
                try {
                    model = m_modelRepository->GetOrLoad(CubeModelName, CubeModelName);
                } catch (const std::exception &exception) {
                    std::cerr << "[Editor] Failed to load cube model '" << CubeModelName << "': " << exception.what() << "\n";
                    model = nullptr;
                }
            }

            if (!IsCubeModelRenderable(model)) {
                std::cerr << "[Editor] cube.obj is unavailable or invalid, falling back to runtime generated cube.\n";
                if (auto generatedModel = m_modelRepository->Find(GeneratedCubeModelName); generatedModel != nullptr) {
                    model = generatedModel;
                } else {
                    model = CreateRuntimeCubeModel();
                    if (model != nullptr) {
                        model = m_modelRepository->Store(GeneratedCubeModelName, model);
                    }
                }
            }

            if (model == nullptr) {
                std::cerr << "[Editor] Create Cube failed: no valid cube model is available.\n";
                return;
            }

#ifdef RAY_TRACING
            if (m_rayTracingSceneContext != nullptr) {
                m_rayTracingSceneContext->EnsureBlasBuilt(
                    model,
                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
            }
#endif

            const Material::id_t materialId = PickDefaultMeshMaterialId(materials);
            sceneRegistry.EmplaceComponent<MeshRendererComponent>(entityId, model, materialId);
#ifdef RAY_TRACING
            if (m_rayTracingSceneContext != nullptr && !sceneRegistry.HasComponent<RayTracingInstanceComponent>(entityId)) {
                sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(entityId, m_rayTracingSceneContext->AllocateInstanceId());
            }
#endif
            std::cerr << "[Editor] Created Cube entity " << entityId
                      << " model='" << model->GetName() << "'"
                      << " materialId=" << materialId << "\n";
        }

        void DestroyEntitySubtree(ECS::SceneRegistry &sceneRegistry,
                                  HierarchyService &hierarchyService,
                                  TransformService &transformService,
                                  id_t entityId,
                                  EditorSelectionService &selectionService,
                                  FrameInfo &frameInfo) {
            if (!sceneRegistry.IsAlive(entityId) || IsPendingDestroy(entityId)) {
                return;
            }

            transformService.ClearParent(sceneRegistry, hierarchyService, entityId);

            std::vector<id_t> destroyOrder{};
            hierarchyService.CollectSubtreeIds(entityId, destroyOrder);
            if (destroyOrder.empty()) {
                destroyOrder.push_back(entityId);
            }

            DeferredDestroyRequest destroyRequest{};
            destroyRequest.entityIds.reserve(destroyOrder.size());
            for (const id_t destroyId: destroyOrder) {
                destroyRequest.entityIds.push_back(destroyId);
                m_pendingDestroyedEntities.insert(destroyId);
                selectionService.ClearIfSelected(destroyId);

                if (auto *meta = sceneRegistry.TryGetEntityMeta(destroyId)) {
                    meta->active = false;
                }
            }

            hierarchyService.RemoveEntity(entityId);
            m_deferredDestroyRequests.emplace_back(std::move(destroyRequest));
            frameInfo.sceneUpdated = true;
        }

        void ProcessDeferredDestroy(ECS::SceneRegistry &sceneRegistry,
                                    EditorSelectionService &selectionService,
                                    FrameInfo &frameInfo) {
            for (auto requestIt = m_deferredDestroyRequests.begin(); requestIt != m_deferredDestroyRequests.end();) {
                if (--requestIt->framesRemaining > 0) {
                    ++requestIt;
                    continue;
                }

                for (const id_t entityId: requestIt->entityIds) {
                    m_pendingDestroyedEntities.erase(entityId);
                    selectionService.ClearIfSelected(entityId);
                    sceneRegistry.DestroyEntity(entityId);
                }
                frameInfo.sceneUpdated = true;
                requestIt = m_deferredDestroyRequests.erase(requestIt);
            }
        }

        static bool IsCubeModelRenderable(const std::shared_ptr<Model> &model) {
            return model != nullptr &&
                   model->getVertexCount() >= 24 &&
                   model->getIndexCount() >= 36;
        }

        std::shared_ptr<Model> CreateRuntimeCubeModel() {
            if (m_modelRepository == nullptr) {
                return nullptr;
            }

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

            auto model = std::make_shared<Model>(m_modelRepository->GetDevice(), builder);
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

        std::string GenerateUniqueEntityName(const ECS::SceneRegistry &sceneRegistry, std::string_view baseName) const {
            std::unordered_set<std::string> existingNames{};
            for (const id_t entityId: sceneRegistry.GetEntityOrder()) {
                if (!sceneRegistry.IsAlive(entityId) || IsPendingDestroy(entityId)) {
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

        static bool IsUiElementPreset(EntityPreset preset) {
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

        std::optional<PendingCreateRequest> m_pendingCreateRequest{};
        std::optional<id_t> m_pendingDeleteRequest{};
        std::vector<DeferredDestroyRequest> m_deferredDestroyRequests{};
        std::unordered_set<id_t> m_pendingDestroyedEntities{};
        ModelRepository *m_modelRepository = nullptr;
#ifdef RAY_TRACING
        RayTracingSceneContext *m_rayTracingSceneContext = nullptr;
#endif
    };
}
