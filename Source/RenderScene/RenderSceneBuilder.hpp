#pragma once

#include <algorithm>

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
        static RenderScene Build(const FrameInfo &frameInfo, const Renderer &renderer) {
            RenderScene renderScene{};
            renderScene.SetView({
                frameInfo.scenePanelRect,
                frameInfo.sceneViewportRect,
                frameInfo.sceneRenderExtent,
                frameInfo.sceneRenderExtent.height == 0
                    ? 1.0f
                    : static_cast<float>(frameInfo.sceneRenderExtent.width) /
                          static_cast<float>(frameInfo.sceneRenderExtent.height)});

            BuildCamera(frameInfo, renderer, renderScene);
            BuildMeshInstances(frameInfo, renderScene);
            BuildLightInstances(frameInfo, renderScene);
            return renderScene;
        }

    private:
        static void BuildCamera(const FrameInfo &frameInfo, const Renderer &renderer, RenderScene &renderScene) {
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
                for (const auto entityId: sceneRegistry.View<CameraComponent, TransformComponent>()) {
                    if (!EntityLifecycle::IsActive(frameInfo, entityId)) {
                        continue;
                    }

                    CameraComponent *camera = nullptr;
                    TransformComponent *transform = nullptr;
                    if (!sceneRegistry.TryGetComponent(entityId, camera) || camera == nullptr ||
                        !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                        continue;
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
                }
            }

            if (renderCamera.entityId == RenderCamera::InvalidEntityId) {
                renderCamera.valid = frameInfo.sceneRenderExtent.width > 0 && frameInfo.sceneRenderExtent.height > 0;
            }

            renderScene.SetCamera(renderCamera);
        }

        static void BuildMeshInstances(const FrameInfo &frameInfo, RenderScene &renderScene) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            const auto meshEntities = sceneRegistry.View<MeshRendererComponent, TransformComponent>();
            renderScene.ReserveMeshInstances(meshEntities.size());
            for (const auto entityId: meshEntities) {
                if (EntityLifecycle::IsPendingDestroy(frameInfo, entityId)) {
                    continue;
                }

                MeshRendererComponent *meshRenderer = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, meshRenderer) || meshRenderer == nullptr ||
                    !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                    continue;
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
                const auto materialIt = frameInfo.materials.find(meshInstance.materialId);
                if (materialIt != frameInfo.materials.end() && materialIt->second != nullptr) {
                    meshInstance.pipelineCategory = materialIt->second->getPipelineCategory();
                    const auto renderQueueIt = PipelineRenderQueue.find(meshInstance.pipelineCategory);
                    if (renderQueueIt != PipelineRenderQueue.end()) {
                        meshInstance.renderQueue = renderQueueIt->second;
                    }
                    meshInstance.skyboxLike = meshInstance.pipelineCategory == PipelineCategory.SkyBox;
                    meshInstance.overlayLike = meshInstance.pipelineCategory == PipelineCategory.Overlay;
                    meshInstance.lightPassLike = meshInstance.pipelineCategory == PipelineCategory.Light;
                    meshInstance.specialPipeline =
                        meshInstance.skyboxLike ||
                        meshInstance.overlayLike ||
                        meshInstance.lightPassLike ||
                        meshInstance.pipelineCategory == PipelineCategory.TessellationGeometry;
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

                if (frameInfo.rayTracingInstanceIds != nullptr) {
                    const auto rayTracingInstanceIt = frameInfo.rayTracingInstanceIds->find(entityId);
                    if (rayTracingInstanceIt != frameInfo.rayTracingInstanceIds->end()) {
                        meshInstance.rayTracingInstanceId = rayTracingInstanceIt->second;
                    }
                }

                renderScene.AddMeshInstance(std::move(meshInstance));
            }
        }

        static void BuildLightInstances(const FrameInfo &frameInfo, RenderScene &renderScene) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            const auto lightEntities = sceneRegistry.View<LightComponent, TransformComponent>();
            renderScene.ReserveLightInstances(lightEntities.size());
            for (const auto entityId: lightEntities) {
                if (EntityLifecycle::IsPendingDestroy(frameInfo, entityId)) {
                    continue;
                }

                LightComponent *light = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, light) || light == nullptr ||
                    !sceneRegistry.TryGetComponent(entityId, transform) || transform == nullptr) {
                    continue;
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
                renderScene.AddLightInstance(std::move(lightInstance));
            }
        }
    };
}
