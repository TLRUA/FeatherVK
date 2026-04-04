#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../Components/CameraComponent.hpp"
#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RayTracingInstanceComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Components/UIComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Model.hpp"
#include "../StructureInfos.h"
#include "../SwapChain.hpp"
#include "EditorSceneUtils.hpp"
#include "LightEmitterMeshUtils.hpp"
#include "EditorSelectionService.hpp"
#include "HierarchyService.hpp"
#include "ModelRepository.hpp"
#include "TransformService.hpp"
#include <glm/gtc/constants.hpp>

#ifdef RAY_TRACING
#include "RayTracingSceneContext.hpp"
#endif

namespace FeatherVK {
    class EntityCommandService {
    public:
        enum class EntityPreset {
            CreateEmpty,
            Cube,
            Sphere,
            Cylinder,
            Plane,
            Torus,
            DirectionalLight,
            PointLight,
            Camera,
            UICanvas,
            UIPanel,
            UIImage,
            UIText,
            UIButton
        };

        enum class ComponentPreset {
            CameraComponent,
            CameraMovementComponent,
            ObjectMovementComponent,
            MeshRendererComponent,
            LightComponent,
            RigidBodyComponent,
            UIComponent,
#ifdef RAY_TRACING
            RayTracingInstanceComponent,
#endif
        };

        struct ComponentAvailability {
            ComponentPreset preset{};
            const char *label = "";
            bool enabled = true;
            const char *disabledReason = nullptr;
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

        std::vector<ComponentAvailability> GetAvailableComponentOptions(const ECS::SceneRegistry &sceneRegistry,
                                                                        id_t entityId) const {
            std::vector<ComponentAvailability> options{};
            if (!sceneRegistry.IsAlive(entityId)) {
                return options;
            }

            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::MeshRendererComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::LightComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::RigidBodyComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::CameraComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::CameraMovementComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::ObjectMovementComponent);
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::UIComponent);
#ifdef RAY_TRACING
            AppendComponentOption(options, sceneRegistry, entityId, ComponentPreset::RayTracingInstanceComponent);
#endif
            return options;
        }

        bool AddComponent(ECS::SceneRegistry &sceneRegistry,
                          id_t entityId,
                          ComponentPreset preset,
                          FrameInfo &frameInfo) {
            if (!sceneRegistry.IsAlive(entityId)) {
                return false;
            }

            const auto availability = GetComponentAvailability(sceneRegistry, entityId, preset);
            if (!availability.enabled || HasComponentPreset(sceneRegistry, entityId, preset)) {
                return false;
            }

            if (!AddComponentByPreset(sceneRegistry, entityId, preset, frameInfo.materials)) {
                return false;
            }

            EditorSceneUtils::MarkSceneDirty(frameInfo, true);
            return true;
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
        enum class PrimitiveMeshType {
            Cube,
            Sphere,
            Cylinder,
            Plane,
            Torus
        };

        struct PrimitiveModelSpec {
            PrimitiveMeshType type{};
            const char *displayName = "Primitive";
            const char *assetName = nullptr;
            const char *assetRelativePath = nullptr;
            const char *generatedName = nullptr;
        };

        ComponentAvailability GetComponentAvailability(const ECS::SceneRegistry &sceneRegistry,
                                                       id_t entityId,
                                                       ComponentPreset preset) const {
            ComponentAvailability availability{};
            availability.preset = preset;
            availability.label = GetComponentPresetLabel(preset);
            availability.enabled = sceneRegistry.IsAlive(entityId) && !HasComponentPreset(sceneRegistry, entityId, preset);
            availability.disabledReason = nullptr;

            if (!availability.enabled) {
                return availability;
            }

            switch (preset) {
                case ComponentPreset::CameraMovementComponent:
                    if (!sceneRegistry.HasComponent<CameraComponent>(entityId)) {
                        availability.enabled = false;
                        availability.disabledReason = "Requires CameraComponent";
                    }
                    break;
                case ComponentPreset::RigidBodyComponent:
                    if (!sceneRegistry.HasComponent<MeshRendererComponent>(entityId)) {
                        availability.enabled = false;
                        availability.disabledReason = "Requires MeshRendererComponent";
                    }
                    break;
#ifdef RAY_TRACING
                case ComponentPreset::RayTracingInstanceComponent:
                    if (!sceneRegistry.HasComponent<MeshRendererComponent>(entityId)) {
                        availability.enabled = false;
                        availability.disabledReason = "Requires MeshRendererComponent";
                    } else if (m_rayTracingSceneContext == nullptr) {
                        availability.enabled = false;
                        availability.disabledReason = "Ray tracing is unavailable";
                    }
                    break;
#endif
                default:
                    break;
            }

            return availability;
        }

        void AppendComponentOption(std::vector<ComponentAvailability> &options,
                                   const ECS::SceneRegistry &sceneRegistry,
                                   id_t entityId,
                                   ComponentPreset preset) const {
            if (HasComponentPreset(sceneRegistry, entityId, preset)) {
                return;
            }
            options.push_back(GetComponentAvailability(sceneRegistry, entityId, preset));
        }

        static bool HasComponentPreset(const ECS::SceneRegistry &sceneRegistry,
                                       id_t entityId,
                                       ComponentPreset preset) {
            switch (preset) {
                case ComponentPreset::CameraComponent:
                    return sceneRegistry.HasComponent<CameraComponent>(entityId);
                case ComponentPreset::CameraMovementComponent:
                    return sceneRegistry.HasComponent<CameraMovementComponent>(entityId);
                case ComponentPreset::ObjectMovementComponent:
                    return sceneRegistry.HasComponent<ObjectMovementComponent>(entityId);
                case ComponentPreset::MeshRendererComponent:
                    return sceneRegistry.HasComponent<MeshRendererComponent>(entityId);
                case ComponentPreset::LightComponent:
                    return sceneRegistry.HasComponent<LightComponent>(entityId);
                case ComponentPreset::RigidBodyComponent:
                    return sceneRegistry.HasComponent<RigidBodyComponent>(entityId);
                case ComponentPreset::UIComponent:
                    return sceneRegistry.HasComponent<UIComponent>(entityId);
#ifdef RAY_TRACING
                case ComponentPreset::RayTracingInstanceComponent:
                    return sceneRegistry.HasComponent<RayTracingInstanceComponent>(entityId);
#endif
                default:
                    return false;
            }
        }

        bool AddComponentByPreset(ECS::SceneRegistry &sceneRegistry,
                                  id_t entityId,
                                  ComponentPreset preset,
                                  Material::Map &materials) {
            switch (preset) {
                case ComponentPreset::CameraComponent:
                    sceneRegistry.EmplaceComponent<CameraComponent>(entityId);
                    return true;
                case ComponentPreset::CameraMovementComponent:
                    sceneRegistry.EmplaceComponent<CameraMovementComponent>(entityId);
                    return true;
                case ComponentPreset::ObjectMovementComponent:
                    sceneRegistry.EmplaceComponent<ObjectMovementComponent>(entityId);
                    return true;
                case ComponentPreset::MeshRendererComponent:
                    AddCubeRendererComponent(sceneRegistry, entityId, materials);
                    return sceneRegistry.HasComponent<MeshRendererComponent>(entityId);
                case ComponentPreset::LightComponent: {
                    auto *lightComponent = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::POINT_LIGHT);
                    ApplyLightEmitterDefaults(sceneRegistry, entityId, *lightComponent);
                    return true;
                }
                case ComponentPreset::RigidBodyComponent:
                    sceneRegistry.EmplaceComponent<RigidBodyComponent>(entityId);
                    return true;
                case ComponentPreset::UIComponent: {
                    const bool hasCanvas = FindFirstCanvasEntity(sceneRegistry).has_value();
                    const auto elementType = hasCanvas ? UIComponent::ElementType::Panel : UIComponent::ElementType::Canvas;
                    sceneRegistry.EmplaceComponent<UIComponent>(entityId, elementType);
                    return true;
                }
#ifdef RAY_TRACING
                case ComponentPreset::RayTracingInstanceComponent:
                    sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(
                        entityId,
                        m_rayTracingSceneContext != nullptr ? m_rayTracingSceneContext->AllocateInstanceId() : RayTracingInstanceComponent::InvalidInstanceId);
                    return true;
#endif
                default:
                    return false;
            }
        }

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
                    AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Cube, frameInfo.materials);
                    break;
                case EntityPreset::Sphere:
                    AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Sphere, frameInfo.materials);
                    break;
                case EntityPreset::Cylinder:
                    AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Cylinder, frameInfo.materials);
                    break;
                case EntityPreset::Plane:
                    AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Plane, frameInfo.materials);
                    break;
                case EntityPreset::Torus:
                    AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Torus, frameInfo.materials);
                    break;
                case EntityPreset::DirectionalLight: {
                    auto *lightComponent = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::DIRECTIONAL_LIGHT);
                    ApplyLightEmitterDefaults(sceneRegistry, entityId, *lightComponent);
                    transformService.SetTranslation(sceneRegistry, entityId, glm::vec3{0.0f, 2.0f, 0.0f});
                    break;
                }
                case EntityPreset::PointLight: {
                    auto *lightComponent = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
                    lightComponent->SetLightCategory(LightCategory::POINT_LIGHT);
                    ApplyLightEmitterDefaults(sceneRegistry, entityId, *lightComponent);
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

            EditorSceneUtils::MarkSceneDirty(frameInfo, true);
            return entityId;
        }

        void AddCubeRendererComponent(ECS::SceneRegistry &sceneRegistry,
                                      id_t entityId,
                                      Material::Map &materials) {
            AddPrimitiveRendererComponent(sceneRegistry, entityId, PrimitiveMeshType::Cube, materials);
        }

        void AddPrimitiveRendererComponent(ECS::SceneRegistry &sceneRegistry,
                                           id_t entityId,
                                           PrimitiveMeshType primitiveType,
                                           Material::Map &materials) {
            if (m_modelRepository == nullptr) {
                std::cerr << "[Editor] Create primitive failed: model repository is not configured.\n";
                return;
            }

            const PrimitiveModelSpec spec = GetPrimitiveModelSpec(primitiveType);
            std::shared_ptr<Model> model = ResolvePrimitiveModel(spec);
            if (!IsModelRenderable(model)) {
                std::cerr << "[Editor] Create " << spec.displayName << " failed: no valid model is available.\n";
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
            auto *meshRenderer = sceneRegistry.EmplaceComponent<MeshRendererComponent>(entityId, model, materialId);
            if (meshRenderer != nullptr) {
                meshRenderer->SetMeshResourceHandle(m_modelRepository->FindMeshResource(model->GetName()));
                meshRenderer->SetPbrOverride(LightEmitterMeshUtils::CreateDefaultPrimitivePbr());
                LightComponent *lightComponent = nullptr;
                if (sceneRegistry.TryGetComponent(entityId, lightComponent) && lightComponent != nullptr) {
                    ApplyLightEmitterDefaults(sceneRegistry, entityId, *lightComponent);
                }
            }
#ifdef RAY_TRACING
            if (m_rayTracingSceneContext != nullptr && !sceneRegistry.HasComponent<RayTracingInstanceComponent>(entityId)) {
                sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(entityId, m_rayTracingSceneContext->AllocateInstanceId());
            }
#endif
            std::cerr << "[Editor] Created " << spec.displayName << " entity " << entityId
                      << " model='" << model->GetName() << "'"
                      << " materialId=" << materialId << "\n";
        }

        std::shared_ptr<Model> ResolvePrimitiveModel(const PrimitiveModelSpec &spec) {
            std::shared_ptr<Model> model{};
            if (spec.assetName != nullptr && spec.assetRelativePath != nullptr) {
                model = m_modelRepository->Find(spec.assetName);
                if (model == nullptr) {
                    try {
                        model = m_modelRepository->GetOrLoad(spec.assetName, spec.assetRelativePath);
                    } catch (const std::exception &exception) {
                        std::cerr << "[Editor] Failed to load " << spec.displayName << " model '" << spec.assetRelativePath
                                  << "': " << exception.what() << "\n";
                        model = nullptr;
                    }
                }
            }

            if (!IsModelRenderable(model) && spec.generatedName != nullptr) {
                if (auto generatedModel = m_modelRepository->Find(spec.generatedName); generatedModel != nullptr) {
                    model = generatedModel;
                } else {
                    model = CreateRuntimePrimitiveModel(spec.type);
                    if (model != nullptr) {
                        model = m_modelRepository->Store(spec.generatedName, model);
                    }
                }
            }

            return IsModelRenderable(model) ? model : nullptr;
        }

        static PrimitiveModelSpec GetPrimitiveModelSpec(PrimitiveMeshType primitiveType) {
            switch (primitiveType) {
                case PrimitiveMeshType::Cube:
                    return {primitiveType, "Cube", "cube.obj", "cube.obj", "generated_cube"};
                case PrimitiveMeshType::Sphere:
                    return {primitiveType, "Sphere", "sphere.obj", "sphere.obj", "generated_sphere"};
                case PrimitiveMeshType::Cylinder:
                    return {primitiveType, "Cylinder", nullptr, nullptr, "generated_cylinder"};
                case PrimitiveMeshType::Plane:
                    return {primitiveType, "Plane", nullptr, nullptr, "generated_plane"};
                case PrimitiveMeshType::Torus:
                    return {primitiveType, "Torus", nullptr, nullptr, "generated_torus"};
                default:
                    return {PrimitiveMeshType::Cube, "Cube", "cube.obj", "cube.obj", "generated_cube"};
            }
        }

        std::shared_ptr<Model> CreateRuntimePrimitiveModel(PrimitiveMeshType primitiveType) {
            switch (primitiveType) {
                case PrimitiveMeshType::Cube:
                    return CreateRuntimeCubeModel();
                case PrimitiveMeshType::Sphere:
                    return CreateRuntimeSphereModel();
                case PrimitiveMeshType::Cylinder:
                    return CreateRuntimeCylinderModel();
                case PrimitiveMeshType::Plane:
                    return CreateRuntimePlaneModel();
                case PrimitiveMeshType::Torus:
                    return CreateRuntimeTorusModel();
                default:
                    return nullptr;
            }
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
            EditorSceneUtils::MarkSceneDirty(frameInfo, true);
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
                EditorSceneUtils::MarkSceneDirty(frameInfo, true);
                requestIt = m_deferredDestroyRequests.erase(requestIt);
            }
        }

        static bool IsModelRenderable(const std::shared_ptr<Model> &model) {
            return model != nullptr &&
                   model->getVertexCount() >= 3 &&
                   model->getIndexCount() >= 3;
        }

        static void AppendTriangle(std::vector<uint32_t> &indices, uint32_t a, uint32_t b, uint32_t c) {
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
        }

        static void ApplyLightEmitterDefaults(ECS::SceneRegistry &sceneRegistry,
                                              id_t entityId,
                                              const LightComponent &lightComponent) {
            MeshRendererComponent *meshRenderer = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, meshRenderer) || meshRenderer == nullptr) {
                return;
            }

            meshRenderer->SetCastShadow(false);
            meshRenderer->SetReceiveShadow(false);
            meshRenderer->SetPbrOverride(LightEmitterMeshUtils::CreateLightEmitterPbr(*meshRenderer, lightComponent));
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

        std::shared_ptr<Model> CreateRuntimeSphereModel() {
            if (m_modelRepository == nullptr) {
                return nullptr;
            }

            constexpr uint32_t SectorCount = 32;
            constexpr uint32_t StackCount = 16;
            constexpr float Radius = 1.0f;

            Model::Builder builder{};
            builder.vertices.reserve((StackCount + 1) * (SectorCount + 1));
            builder.indices.reserve(StackCount * SectorCount * 6);
            builder.maxRadius = Radius;

            for (uint32_t stack = 0; stack <= StackCount; ++stack) {
                const float v = static_cast<float>(stack) / static_cast<float>(StackCount);
                const float phi = v * glm::pi<float>();
                const float y = std::cos(phi);
                const float ringRadius = std::sin(phi);

                for (uint32_t sector = 0; sector <= SectorCount; ++sector) {
                    const float u = static_cast<float>(sector) / static_cast<float>(SectorCount);
                    const float theta = u * glm::two_pi<float>();
                    const glm::vec3 normal{
                        std::cos(theta) * ringRadius,
                        y,
                        std::sin(theta) * ringRadius};

                    Model::Vertex vertex{};
                    vertex.position = normal * Radius;
                    vertex.color = glm::vec3(1.0f);
                    vertex.normal = glm::normalize(normal);
                    vertex.smoothedNormal = vertex.normal;
                    vertex.uv = glm::vec2(u, 1.0f - v);
                    builder.vertices.push_back(vertex);
                }
            }

            for (uint32_t stack = 0; stack < StackCount; ++stack) {
                for (uint32_t sector = 0; sector < SectorCount; ++sector) {
                    const uint32_t current = stack * (SectorCount + 1) + sector;
                    const uint32_t next = current + SectorCount + 1;

                    if (stack != 0) {
                        AppendTriangle(builder.indices, current, next, current + 1);
                    }
                    if (stack != StackCount - 1) {
                        AppendTriangle(builder.indices, current + 1, next, next + 1);
                    }
                }
            }

            auto model = std::make_shared<Model>(m_modelRepository->GetDevice(), builder);
            model->SetName("generated_sphere");
            return model;
        }

        std::shared_ptr<Model> CreateRuntimeCylinderModel() {
            if (m_modelRepository == nullptr) {
                return nullptr;
            }

            constexpr uint32_t SegmentCount = 32;
            constexpr float Radius = 1.0f;
            constexpr float HalfHeight = 1.0f;

            Model::Builder builder{};
            builder.vertices.reserve((SegmentCount + 1) * 4 + 2);
            builder.indices.reserve(SegmentCount * 12);
            builder.maxRadius = std::sqrt(Radius * Radius + HalfHeight * HalfHeight);

            for (uint32_t segment = 0; segment <= SegmentCount; ++segment) {
                const float u = static_cast<float>(segment) / static_cast<float>(SegmentCount);
                const float angle = u * glm::two_pi<float>();
                const float x = std::cos(angle) * Radius;
                const float z = std::sin(angle) * Radius;
                const glm::vec3 sideNormal = glm::normalize(glm::vec3{x, 0.0f, z});

                Model::Vertex bottomVertex{};
                bottomVertex.position = {x, -HalfHeight, z};
                bottomVertex.color = glm::vec3(1.0f);
                bottomVertex.normal = sideNormal;
                bottomVertex.smoothedNormal = sideNormal;
                bottomVertex.uv = {u, 1.0f};
                builder.vertices.push_back(bottomVertex);

                Model::Vertex topVertex = bottomVertex;
                topVertex.position.y = HalfHeight;
                topVertex.uv = {u, 0.0f};
                builder.vertices.push_back(topVertex);
            }

            for (uint32_t segment = 0; segment < SegmentCount; ++segment) {
                const uint32_t current = segment * 2;
                const uint32_t next = current + 2;
                AppendTriangle(builder.indices, current, current + 1, next);
                AppendTriangle(builder.indices, next, current + 1, next + 1);
            }

            const uint32_t topCenterIndex = static_cast<uint32_t>(builder.vertices.size());
            Model::Vertex topCenter{};
            topCenter.position = {0.0f, HalfHeight, 0.0f};
            topCenter.color = glm::vec3(1.0f);
            topCenter.normal = {0.0f, 1.0f, 0.0f};
            topCenter.smoothedNormal = topCenter.normal;
            topCenter.uv = {0.5f, 0.5f};
            builder.vertices.push_back(topCenter);

            for (uint32_t segment = 0; segment <= SegmentCount; ++segment) {
                const float u = static_cast<float>(segment) / static_cast<float>(SegmentCount);
                const float angle = u * glm::two_pi<float>();
                const float x = std::cos(angle) * Radius;
                const float z = std::sin(angle) * Radius;

                Model::Vertex topVertex{};
                topVertex.position = {x, HalfHeight, z};
                topVertex.color = glm::vec3(1.0f);
                topVertex.normal = {0.0f, 1.0f, 0.0f};
                topVertex.smoothedNormal = topVertex.normal;
                topVertex.uv = {x * 0.5f + 0.5f, z * 0.5f + 0.5f};
                builder.vertices.push_back(topVertex);
            }

            for (uint32_t segment = 0; segment < SegmentCount; ++segment) {
                const uint32_t current = topCenterIndex + 1 + segment;
                AppendTriangle(builder.indices, topCenterIndex, current + 1, current);
            }

            const uint32_t bottomCenterIndex = static_cast<uint32_t>(builder.vertices.size());
            Model::Vertex bottomCenter = topCenter;
            bottomCenter.position.y = -HalfHeight;
            bottomCenter.normal = {0.0f, -1.0f, 0.0f};
            bottomCenter.smoothedNormal = bottomCenter.normal;
            builder.vertices.push_back(bottomCenter);

            for (uint32_t segment = 0; segment <= SegmentCount; ++segment) {
                const float u = static_cast<float>(segment) / static_cast<float>(SegmentCount);
                const float angle = u * glm::two_pi<float>();
                const float x = std::cos(angle) * Radius;
                const float z = std::sin(angle) * Radius;

                Model::Vertex bottomVertex{};
                bottomVertex.position = {x, -HalfHeight, z};
                bottomVertex.color = glm::vec3(1.0f);
                bottomVertex.normal = {0.0f, -1.0f, 0.0f};
                bottomVertex.smoothedNormal = bottomVertex.normal;
                bottomVertex.uv = {x * 0.5f + 0.5f, z * 0.5f + 0.5f};
                builder.vertices.push_back(bottomVertex);
            }

            for (uint32_t segment = 0; segment < SegmentCount; ++segment) {
                const uint32_t current = bottomCenterIndex + 1 + segment;
                AppendTriangle(builder.indices, bottomCenterIndex, current, current + 1);
            }

            auto model = std::make_shared<Model>(m_modelRepository->GetDevice(), builder);
            model->SetName("generated_cylinder");
            return model;
        }

        std::shared_ptr<Model> CreateRuntimePlaneModel() {
            if (m_modelRepository == nullptr) {
                return nullptr;
            }

            Model::Builder builder{};
            builder.vertices.reserve(4);
            builder.indices.reserve(6);
            builder.maxRadius = glm::length(glm::vec3(1.0f, 0.0f, 1.0f));

            const std::array<glm::vec3, 4> positions{{
                {-1.0f, 0.0f, -1.0f},
                {-1.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, -1.0f}
            }};
            const std::array<glm::vec2, 4> uvs{{
                {0.0f, 0.0f},
                {0.0f, 1.0f},
                {1.0f, 1.0f},
                {1.0f, 0.0f}
            }};

            for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
                Model::Vertex vertex{};
                vertex.position = positions[vertexIndex];
                vertex.color = glm::vec3(1.0f);
                vertex.normal = {0.0f, 1.0f, 0.0f};
                vertex.smoothedNormal = vertex.normal;
                vertex.uv = uvs[vertexIndex];
                builder.vertices.push_back(vertex);
            }

            AppendTriangle(builder.indices, 0, 1, 2);
            AppendTriangle(builder.indices, 0, 2, 3);

            auto model = std::make_shared<Model>(m_modelRepository->GetDevice(), builder);
            model->SetName("generated_plane");
            return model;
        }

        std::shared_ptr<Model> CreateRuntimeTorusModel() {
            if (m_modelRepository == nullptr) {
                return nullptr;
            }

            constexpr uint32_t MajorSegmentCount = 40;
            constexpr uint32_t MinorSegmentCount = 20;
            constexpr float MajorRadius = 1.0f;
            constexpr float MinorRadius = 0.35f;

            Model::Builder builder{};
            builder.vertices.reserve((MajorSegmentCount + 1) * (MinorSegmentCount + 1));
            builder.indices.reserve(MajorSegmentCount * MinorSegmentCount * 6);
            builder.maxRadius = MajorRadius + MinorRadius;

            for (uint32_t majorSegment = 0; majorSegment <= MajorSegmentCount; ++majorSegment) {
                const float u = static_cast<float>(majorSegment) / static_cast<float>(MajorSegmentCount);
                const float majorAngle = u * glm::two_pi<float>();
                const float cosMajor = std::cos(majorAngle);
                const float sinMajor = std::sin(majorAngle);

                for (uint32_t minorSegment = 0; minorSegment <= MinorSegmentCount; ++minorSegment) {
                    const float v = static_cast<float>(minorSegment) / static_cast<float>(MinorSegmentCount);
                    const float minorAngle = v * glm::two_pi<float>();
                    const float cosMinor = std::cos(minorAngle);
                    const float sinMinor = std::sin(minorAngle);

                    const glm::vec3 normal{
                        cosMajor * cosMinor,
                        sinMinor,
                        sinMajor * cosMinor};
                    const glm::vec3 center{
                        cosMajor * MajorRadius,
                        0.0f,
                        sinMajor * MajorRadius};

                    Model::Vertex vertex{};
                    vertex.position = center + normal * MinorRadius;
                    vertex.color = glm::vec3(1.0f);
                    vertex.normal = glm::normalize(normal);
                    vertex.smoothedNormal = vertex.normal;
                    vertex.uv = {u, v};
                    builder.vertices.push_back(vertex);
                }
            }

            for (uint32_t majorSegment = 0; majorSegment < MajorSegmentCount; ++majorSegment) {
                for (uint32_t minorSegment = 0; minorSegment < MinorSegmentCount; ++minorSegment) {
                    const uint32_t current = majorSegment * (MinorSegmentCount + 1) + minorSegment;
                    const uint32_t nextMajor = current + MinorSegmentCount + 1;
                    const uint32_t nextMinor = current + 1;
                    const uint32_t nextMajorNextMinor = nextMajor + 1;

                    AppendTriangle(builder.indices, current, nextMinor, nextMajor);
                    AppendTriangle(builder.indices, nextMajor, nextMinor, nextMajorNextMinor);
                }
            }

            auto model = std::make_shared<Model>(m_modelRepository->GetDevice(), builder);
            model->SetName("generated_torus");
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

        static const char *GetComponentPresetLabel(ComponentPreset preset) {
            switch (preset) {
                case ComponentPreset::CameraComponent:
                    return "CameraComponent";
                case ComponentPreset::CameraMovementComponent:
                    return "CameraMovementComponent";
                case ComponentPreset::ObjectMovementComponent:
                    return "ObjectMovementComponent";
                case ComponentPreset::MeshRendererComponent:
                    return "MeshRendererComponent";
                case ComponentPreset::LightComponent:
                    return "LightComponent";
                case ComponentPreset::RigidBodyComponent:
                    return "RigidBodyComponent";
                case ComponentPreset::UIComponent:
                    return "UIComponent";
#ifdef RAY_TRACING
                case ComponentPreset::RayTracingInstanceComponent:
                    return "RayTracingInstanceComponent";
#endif
                default:
                    return "Component";
            }
        }

        static std::string GetPresetBaseName(EntityPreset preset) {
            switch (preset) {
                case EntityPreset::CreateEmpty:
                    return "Empty";
                case EntityPreset::Cube:
                    return "Cube";
                case EntityPreset::Sphere:
                    return "Sphere";
                case EntityPreset::Cylinder:
                    return "Cylinder";
                case EntityPreset::Plane:
                    return "Plane";
                case EntityPreset::Torus:
                    return "Torus";
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
