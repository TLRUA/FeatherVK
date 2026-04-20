#pragma once

#include <algorithm>
#include <unordered_map>

#include <glm/ext/matrix_transform.hpp>

#include "../Components/CameraComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Managers/EntityLifecycleUtils.hpp"
#include "../Pipeline.hpp"
#include "../Renderer.h"
#include "../StructureInfos.h"
#include "RenderScene.hpp"

namespace FeatherVK {
    class RenderSceneBuilder {
    public:
        struct MaterialTraits {
            std::string pipelineCategory{};
            unsigned int renderQueue{0};
            bool skyboxLike{false};
            bool overlayLike{false};
            bool lightPassLike{false};
            bool specialPipeline{false};
        };

        using MaterialTraitsCache = std::unordered_map<Material::id_t, MaterialTraits>;

        static RenderScene Build(const FrameInfo &frameInfo,
                                 const Renderer &renderer,
                                 const MaterialTraitsCache *materialTraitsCache = nullptr) {
            RenderScene renderScene{};
            renderScene.SetView(BuildView(frameInfo));
            renderScene.SetCamera(BuildCameraData(frameInfo, renderer));
            MaterialTraitsCache localMaterialTraits{};
            const MaterialTraitsCache *resolvedMaterialTraits = materialTraitsCache;
            if (resolvedMaterialTraits == nullptr) {
                localMaterialTraits = BuildMaterialTraitsCache(frameInfo);
                resolvedMaterialTraits = &localMaterialTraits;
            }
            BuildMeshInstances(frameInfo, renderScene, *resolvedMaterialTraits);
            BuildLightInstances(frameInfo, renderScene);
            return renderScene;
        }

        static RenderView BuildView(const FrameInfo &frameInfo) {
            return {
                frameInfo.scenePanelRect,
                frameInfo.sceneViewportRect,
                frameInfo.sceneRenderExtent,
                frameInfo.sceneRenderExtent.height == 0
                    ? 1.0f
                    : static_cast<float>(frameInfo.sceneRenderExtent.width) /
                          static_cast<float>(frameInfo.sceneRenderExtent.height)};
        }

        static RenderCamera BuildCameraData(const FrameInfo &frameInfo, const Renderer &renderer) {
            RenderCamera renderCamera{};
            renderCamera.viewMatrix = frameInfo.globalUbo.viewMatrix;
            renderCamera.projectionMatrix = frameInfo.globalUbo.projectionMatrix;
            renderCamera.inverseViewMatrix = frameInfo.globalUbo.inverseViewMatrix;
#ifdef RAY_TRACING
            renderCamera.inverseProjectionMatrix = frameInfo.globalUbo.inverseProjectionMatrix;
#else
            renderCamera.inverseProjectionMatrix = glm::inverse(frameInfo.globalUbo.projectionMatrix);
#endif
            renderCamera.worldPosition = glm::vec3(frameInfo.globalUbo.inverseViewMatrix[3]);
            renderCamera.fovY = renderer.FOV_Y;
            renderCamera.nearClip = renderer.NEAR_CLIP;
            renderCamera.farClip = renderer.FAR_CLIP;
            renderCamera.aspectRatio = frameInfo.sceneRenderExtent.height == 0
                                           ? 1.0f
                                           : static_cast<float>(frameInfo.sceneRenderExtent.width) /
                                                 static_cast<float>(frameInfo.sceneRenderExtent.height);

            if (frameInfo.sceneRegistry != nullptr) {
                auto &sceneRegistry = *frameInfo.sceneRegistry;
                sceneRegistry.ForEachView<CameraComponent, TransformComponent>([&](const id_t entityId) {
                    if (!EntityLifecycle::IsActive(frameInfo, entityId)) {
                        return;
                    }

                    CameraComponent *camera = nullptr;
                    TransformComponent *transform = nullptr;
                    if (!sceneRegistry.TryGetComponent(entityId, camera) || camera == nullptr ||
                        !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                        return;
                    }

                    renderCamera.entityId = entityId;
                    renderCamera.viewMatrix = camera->viewMatrix;
                    renderCamera.projectionMatrix = camera->projectionMatrix;
                    renderCamera.inverseViewMatrix = camera->inverseViewMatrix;
#ifdef RAY_TRACING
                    renderCamera.inverseProjectionMatrix = frameInfo.globalUbo.inverseProjectionMatrix;
#else
                    renderCamera.inverseProjectionMatrix = glm::inverse(camera->projectionMatrix);
#endif
                    renderCamera.worldPosition = transform->GetTranslation();
                    renderCamera.valid = true;
                });
            }

            if (renderCamera.entityId == RenderCamera::InvalidEntityId) {
                renderCamera.valid = frameInfo.sceneRenderExtent.width > 0 && frameInfo.sceneRenderExtent.height > 0;
            }

            return renderCamera;
        }

        static std::optional<RenderMeshInstance> BuildMeshInstance(const FrameInfo &frameInfo, id_t entityId) {
            return BuildMeshInstanceInternal(frameInfo, entityId, nullptr);
        }

        static std::optional<RenderMeshInstance> BuildMeshInstance(const FrameInfo &frameInfo,
                                                                   id_t entityId,
                                                                   const MaterialTraitsCache &materialTraitsCache) {
            return BuildMeshInstanceInternal(frameInfo, entityId, &materialTraitsCache);
        }

        static std::optional<RenderLightInstance> BuildLightInstance(const FrameInfo &frameInfo, id_t entityId) {
            return BuildLightInstanceInternal(frameInfo, entityId);
        }

        static MaterialTraitsCache BuildMaterialTraitsCache(const FrameInfo &frameInfo) {
            MaterialTraitsCache materialTraits{};
            materialTraits.reserve(frameInfo.materials.size());
            for (const auto &[materialId, material]: frameInfo.materials) {
                if (material == nullptr) {
                    continue;
                }

                MaterialTraits traits{};
                traits.pipelineCategory = material->getPipelineCategory();
                const auto renderQueueIt = PipelineRenderQueue.find(traits.pipelineCategory);
                if (renderQueueIt != PipelineRenderQueue.end()) {
                    traits.renderQueue = renderQueueIt->second;
                }
                traits.skyboxLike = traits.pipelineCategory == PipelineCategory.SkyBox;
                traits.overlayLike = traits.pipelineCategory == PipelineCategory.Overlay;
                traits.lightPassLike = traits.pipelineCategory == PipelineCategory.Light;
                traits.specialPipeline =
                    traits.skyboxLike ||
                    traits.overlayLike ||
                    traits.lightPassLike ||
                    traits.pipelineCategory == PipelineCategory.TessellationGeometry;
                materialTraits.emplace(materialId, std::move(traits));
            }
            return materialTraits;
        }

    private:

        static void BuildMeshInstances(const FrameInfo &frameInfo,
                                       RenderScene &renderScene,
                                       const MaterialTraitsCache &materialTraits) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            renderScene.ReserveMeshInstances(sceneRegistry.ViewSizeHint<MeshRendererComponent, TransformComponent>());
            const auto *rayTracingInstanceIds = frameInfo.rayTracingInstanceIds;
            sceneRegistry.ForEachView<MeshRendererComponent, TransformComponent>([&](const id_t entityId) {
                if (const auto meshInstance = BuildMeshInstanceInternal(
                        frameInfo,
                        entityId,
                        &materialTraits,
                        rayTracingInstanceIds);
                    meshInstance.has_value()) {
                    renderScene.AddMeshInstance(*meshInstance);
                }
            });
        }

        static void BuildLightInstances(const FrameInfo &frameInfo, RenderScene &renderScene) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            renderScene.ReserveLightInstances(sceneRegistry.ViewSizeHint<LightComponent, TransformComponent>());
            sceneRegistry.ForEachView<LightComponent, TransformComponent>([&](const id_t entityId) {
                if (const auto lightInstance = BuildLightInstanceInternal(frameInfo, entityId); lightInstance.has_value()) {
                    renderScene.AddLightInstance(*lightInstance);
                }
            });
        }

        static std::optional<RenderMeshInstance> BuildMeshInstanceInternal(
            const FrameInfo &frameInfo,
            id_t entityId,
            const MaterialTraitsCache *materialTraits,
            const std::unordered_map<id_t, id_t> *rayTracingInstanceIdsOverride = nullptr) {
            if (frameInfo.sceneRegistry == nullptr || EntityLifecycle::IsPendingDestroy(frameInfo, entityId)) {
                return std::nullopt;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            MeshRendererComponent *meshRenderer = nullptr;
            TransformComponent *transform = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, meshRenderer) || meshRenderer == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                return std::nullopt;
            }

            RenderMeshInstance meshInstance{};
            meshInstance.entityId = entityId;
            meshInstance.worldTransform = transform->mat4();
            meshInstance.normalMatrix = transform->normalMatrix();
            meshInstance.meshResource = meshRenderer->GetMeshResourceHandle();
            meshInstance.materialResource = meshRenderer->GetMaterialResourceHandle();
            meshInstance.materialInstanceResource = meshRenderer->GetMaterialInstanceHandle();
            meshInstance.materialId = meshRenderer->GetMaterialID();
            meshInstance.renderLayer = meshRenderer->GetRenderLayer();
            meshInstance.pbrOverride = meshRenderer->GetPbrOverride();
            meshInstance.active = EntityLifecycle::IsActive(frameInfo, entityId);
            meshInstance.visible = meshInstance.active && meshRenderer->IsVisible();
            meshInstance.castShadow = meshRenderer->CastsShadow();
            meshInstance.receiveShadow = meshRenderer->ReceivesShadow();
            meshInstance.defaultRenderLayer = meshRenderer->IsOnDefaultRenderLayer();
            if (const auto model = meshRenderer->GetModelPtr(); model != nullptr) {
                meshInstance.renderMesh = model->GetRenderMesh();
            }

            MaterialTraits traits{};
            bool hasTraits = false;
            if (materialTraits != nullptr) {
                const auto materialTraitsIt = materialTraits->find(meshInstance.materialId);
                if (materialTraitsIt != materialTraits->end()) {
                    traits = materialTraitsIt->second;
                    hasTraits = true;
                }
            }
            if (!hasTraits) {
                const auto materialIt = frameInfo.materials.find(meshInstance.materialId);
                if (materialIt != frameInfo.materials.end() && materialIt->second != nullptr) {
                    traits.pipelineCategory = materialIt->second->getPipelineCategory();
                    const auto renderQueueIt = PipelineRenderQueue.find(traits.pipelineCategory);
                    if (renderQueueIt != PipelineRenderQueue.end()) {
                        traits.renderQueue = renderQueueIt->second;
                    }
                    traits.skyboxLike = traits.pipelineCategory == PipelineCategory.SkyBox;
                    traits.overlayLike = traits.pipelineCategory == PipelineCategory.Overlay;
                    traits.lightPassLike = traits.pipelineCategory == PipelineCategory.Light;
                    traits.specialPipeline =
                        traits.skyboxLike ||
                        traits.overlayLike ||
                        traits.lightPassLike ||
                        traits.pipelineCategory == PipelineCategory.TessellationGeometry;
                    hasTraits = true;
                }
            }

            if (hasTraits) {
                meshInstance.pipelineCategory = traits.pipelineCategory;
                meshInstance.renderQueue = traits.renderQueue;
                meshInstance.skyboxLike = traits.skyboxLike;
                meshInstance.overlayLike = traits.overlayLike;
                meshInstance.lightPassLike = traits.lightPassLike;
                meshInstance.specialPipeline = traits.specialPipeline;
            }

            LightComponent *lightComponent = nullptr;
            if (sceneRegistry.TryGetComponent(entityId, lightComponent) && lightComponent != nullptr) {
                RenderLightProxy lightProxy{};
                lightProxy.lightCategory = lightComponent->GetLightCategory();
                lightProxy.color = lightComponent->GetColor();
                lightProxy.intensity = lightComponent->GetLightIntensity();
                lightProxy.radius = transform->GetScale().x;
                meshInstance.lightProxy = lightProxy;
            }

            const auto *rayTracingInstanceIds =
                rayTracingInstanceIdsOverride == nullptr ? frameInfo.rayTracingInstanceIds : rayTracingInstanceIdsOverride;
            if (rayTracingInstanceIds != nullptr) {
                const auto rayTracingInstanceIt = rayTracingInstanceIds->find(entityId);
                if (rayTracingInstanceIt != rayTracingInstanceIds->end()) {
                    meshInstance.rayTracingInstanceId = rayTracingInstanceIt->second;
                }
            }

            return meshInstance;
        }

        static std::optional<RenderLightInstance> BuildLightInstanceInternal(const FrameInfo &frameInfo, id_t entityId) {
            if (frameInfo.sceneRegistry == nullptr || EntityLifecycle::IsPendingDestroy(frameInfo, entityId)) {
                return std::nullopt;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            LightComponent *light = nullptr;
            TransformComponent *transform = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, light) || light == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                return std::nullopt;
            }

            const auto rotation = transform->GetRotation();
            auto rotationMatrix = glm::rotate(glm::mat4(1.0f), rotation.y, {0, 1, 0});
            rotationMatrix = glm::rotate(rotationMatrix, rotation.x, {1, 0, 0});
            rotationMatrix = glm::rotate(rotationMatrix, rotation.z, {0, 0, 1});

            RenderLightInstance lightInstance{};
            lightInstance.entityId = entityId;
            lightInstance.lightCategory = light->GetLightCategory();
            lightInstance.position = transform->GetTranslation();
            lightInstance.direction = glm::vec3(rotationMatrix * glm::vec4(0, 0, 1, 0));
            lightInstance.color = light->GetColor();
            lightInstance.intensity = light->GetLightIntensity();
            lightInstance.radius = transform->GetScale().x;
            lightInstance.active = EntityLifecycle::IsActive(frameInfo, entityId);
            return lightInstance;
        }
    };
}
