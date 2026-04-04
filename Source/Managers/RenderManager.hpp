#include <algorithm>
#include <utility>

#include "../Components/MeshRendererComponent.hpp"
#include "../RenderSystems/ComputeSystem.hpp"
#include "../RenderSystems/EditorPickingRenderSystem.hpp"
#include "../RenderSystems/GizmosRenderSystem.hpp"
#include "../RenderSystems/GrassSystem.hpp"
#include "../RenderSystems/LightSystem.hpp"
#include "../RenderSystems/PostSystem.hpp"
#include "../RenderSystems/RayTracingSystem.hpp"
#include "../RenderSystems/RenderSystem.h"
#include "../RenderSystems/ShadowSystem.hpp"
#include "../RenderSystems/SkyBoxSystem.hpp"
#include "../RenderScene/RenderSceneBuilder.hpp"
#include "../Components/RayTracingInstanceComponent.hpp"
#include "../RenderCore/FrameData.hpp"
#include "EntityLifecycleUtils.hpp"
#include "ResourceManager.hpp"

namespace FeatherVK {
    class ResourceManager;

    class RenderManager {
    public:
        RenderManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = std::move(resourceManager);
            CreateRenderSystems(m_resourceManager->GetMaterials(), m_resourceManager->GetDevice(), m_resourceManager->GetRenderer());
        }

        ~RenderManager() = default;

        RenderManager(const RenderManager &) = delete;

        RenderManager &operator=(const RenderManager &) = delete;

        void CreateRenderSystems(Material::Map &materials, Device &device, Renderer &renderer) {
            auto &pipelineLibrary = m_resourceManager->GetRenderCore().GetPipelineLibrary();
            for (auto &materialPair: materials) {
                auto material = materialPair.second;
                const auto pipelineCategory = material->getPipelineCategory();

                if (pipelineCategory == PipelineCategory.Gizmos) {
                    m_gizmosRenderSystem = std::make_shared<GizmosRenderSystem>(
                        device,
                        renderer.getSwapChainRenderPass(),
                        material,
                        m_resourceManager->GetModelRepository(),
                        m_resourceManager->GetRenderCore());
                    continue;
                }

#ifdef RAY_TRACING
                if (pipelineCategory == PipelineCategory.RayTracing) {
                    m_rayTracingSystem = std::make_shared<RayTracingSystem>(device, nullptr, materialPair.second, pipelineLibrary);
                    m_rayTracingSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Post) {
                    m_postSystem = std::make_shared<PostSystem>(device, renderer.getSwapChainRenderPass(), materialPair.second, pipelineLibrary);
                    m_postSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Compute) {
                    m_computeSystem = std::make_shared<ComputeSystem>(device, nullptr, materialPair.second, pipelineLibrary);
                    m_computeSystem->Init();
                }

                if (m_renderSystemMap.find(material->getMaterialId()) != m_renderSystemMap.end()) {
                    continue;
                }

                std::shared_ptr<RenderSystem> renderSystem;
                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem = std::make_shared<GrassSystem>(device, renderer.getSceneColorRenderPass(), material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    renderSystem = std::make_shared<SkyBoxSystem>(device, renderer.getSceneColorRenderPass(), material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    renderSystem = std::make_shared<RenderSystem>(device, renderer.getSceneColorRenderPass(), material, pipelineLibrary);
                }

                if (renderSystem != nullptr) {
                    renderSystem->Init();
                    m_renderSystemMap[material->getMaterialId()] = renderSystem;
                }
#else
                if (m_renderSystemMap.find(material->getMaterialId()) != m_renderSystemMap.end()) {
                    continue;
                }

                std::shared_ptr<RenderSystem> renderSystem;
                if (pipelineCategory == PipelineCategory.Shadow) {
                    m_shadowSystem = std::make_shared<ShadowSystem>(device, renderer.getShadowRenderPass(), material, pipelineLibrary);
                    continue;
                }

                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem = std::make_shared<GrassSystem>(device, renderer.getSwapChainRenderPass(), material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    renderSystem = std::make_shared<SkyBoxSystem>(device, renderer.getSwapChainRenderPass(), material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    renderSystem = std::make_shared<RenderSystem>(device, renderer.getSwapChainRenderPass(), material, pipelineLibrary);
                }

                if (renderSystem != nullptr) {
                    renderSystem->Init();
                    m_renderSystemMap[material->getMaterialId()] = renderSystem;
                }
#endif
            }

            CreateEditorPickingSystem(materials, device, renderer);
        }

        void UpdateUbo(FrameInfo &frameInfo) {
            m_lightSystem.Collect(frameInfo);
#ifndef RAY_TRACING
            if (frameInfo.globalUbo.lightNum > 0) {
                frameInfo.globalUbo.shadowViewMatrix[0] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, 90, 180));
                frameInfo.globalUbo.shadowViewMatrix[1] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, -90, 180));
                frameInfo.globalUbo.shadowViewMatrix[2] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(-90, 0, 0));
                frameInfo.globalUbo.shadowViewMatrix[3] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(90, 0, 0));
                frameInfo.globalUbo.shadowViewMatrix[4] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(180, 0, 0));
                frameInfo.globalUbo.shadowViewMatrix[5] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, 0, 180));
                frameInfo.globalUbo.shadowProjMatrix = CameraComponent::CorrectionMatrix * glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 5.0f);
                frameInfo.globalUbo.lightProjectionViewMatrix = frameInfo.globalUbo.shadowProjMatrix * frameInfo.globalUbo.shadowViewMatrix[0];
            }
#endif
        }

        void UpdateRendering(Renderer &renderer, FrameInfo &frameInfo) {
            [[maybe_unused]] auto renderFrame = RenderCore::BuildFrameContext(frameInfo);
            m_resourceManager->GetRenderCore().GetResourceRegistry().AdvanceFrame();
            m_resourceManager->SyncRenderCoreSceneResources();
            SanitizeSelection(frameInfo);

            const auto frameIndex = frameInfo.frameIndex;
#ifdef RAY_TRACING
            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry,
                            &frameInfo.pEntityDescs,
                            m_resourceManager->GetHierarchyService(),
                            m_resourceManager->GetEntityCommandService(),
                            m_resourceManager->GetEditorSelectionService(),
                            m_resourceManager->GetTransformService(),
                            frameInfo);
            BuildRenderScene(frameInfo, renderer);
            UpdateUbo(frameInfo);
            if (GUI::IsLayoutInteractionActive()) {
                renderer.beginSwapChainRenderPass(frameInfo.commandBuffer);
                if (m_postSystem != nullptr) {
                    m_postSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
                    m_postSystem->RenderWithImageIndex(frameInfo, m_lastPresentedSceneImageIndex);
                }
                GUI::EndFrame(frameInfo.commandBuffer);
                renderer.endSwapChainRenderPass(frameInfo.commandBuffer);
                renderer.endFrame();
                return;
            }

            SyncRayTracingScene(frameInfo);
            const bool hasValidRayTracingTlas = m_resourceManager->HasValidRayTracingTlas();
            if (hasValidRayTracingTlas && frameInfo.pEntityDescBuffer != nullptr && !frameInfo.pEntityDescs.empty()) {
                frameInfo.pEntityDescBuffer->writeToBuffer(frameInfo.pEntityDescs.data(), frameInfo.pEntityDescs.size() * sizeof(EntityDesc));
            }

            renderer.beginSceneColorRenderPass(frameInfo.commandBuffer, frameIndex % 2);
            RenderRasterScene(frameInfo);
            renderer.endSceneColorRenderPass(frameInfo.commandBuffer);
            renderer.setSceneColorToPostSynchronization(frameInfo.commandBuffer, frameIndex % 2);

            if (hasValidRayTracingTlas) {
                m_rayTracingSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
                m_rayTracingSystem->rayTrace(frameInfo);

                renderer.setDenoiseRtxToComputeSynchronization(frameInfo.commandBuffer, frameIndex % 2);

                m_computeSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
                m_computeSystem->render(frameInfo);

                renderer.setDenoiseComputeToPostSynchronization(frameInfo.commandBuffer, frameIndex % 2);
            }

            RenderEditorPickingPass(renderer, frameInfo);

            renderer.beginSwapChainRenderPass(frameInfo.commandBuffer);

            m_postSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
            m_postSystem->render(frameInfo);
            m_lastPresentedSceneImageIndex = frameIndex % 2;
#else
            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry,
                            &frameInfo.materials,
                            m_resourceManager->GetHierarchyService(),
                            m_resourceManager->GetEntityCommandService(),
                            m_resourceManager->GetEditorSelectionService(),
                            m_resourceManager->GetTransformService(),
                            frameInfo);
            BuildRenderScene(frameInfo, renderer);
            UpdateUbo(frameInfo);
            if (GUI::IsLayoutInteractionActive()) {
                // Raster path renders directly to the swapchain; keep rendering to avoid a black scene region.
            }

            renderer.beginShadowRenderPass(frameInfo.commandBuffer);
            m_shadowSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
            m_shadowSystem->renderShadow(frameInfo);
            renderer.endShadowRenderPass(frameInfo.commandBuffer);

            renderer.setShadowMapSynchronization(frameInfo.commandBuffer);

            RenderEditorPickingPass(renderer, frameInfo);

            renderer.beginSwapChainRenderPass(frameInfo.commandBuffer);
            RenderRasterScene(frameInfo);
#endif
            GUI::EndFrame(frameInfo.commandBuffer);
            renderer.endSwapChainRenderPass(frameInfo.commandBuffer);

            renderer.beginGizmosRenderPass(frameInfo.commandBuffer);
            m_gizmosRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
            m_gizmosRenderSystem->render(frameInfo, GizmosType::EdgeDetectionStencil);
            m_gizmosRenderSystem->render(frameInfo, GizmosType::EdgeDetection);
            m_gizmosRenderSystem->render(frameInfo, GizmosType::Axis);
            renderer.endGizmosRenderPass(frameInfo.commandBuffer);

            renderer.endFrame();
        }

    private:
        template<typename Predicate, typename Func>
        void ForEachRenderSceneMeshInstance(const FrameInfo &frameInfo, Predicate &&predicate, Func &&func) const {
            if (frameInfo.renderScene == nullptr) {
                return;
            }

            for (const auto &meshInstance: frameInfo.renderScene->GetMeshInstances()) {
                if (predicate(meshInstance)) {
                    func(meshInstance);
                }
            }
        }

        template<typename Func>
        void ForEachDefaultLayerMeshInstance(const FrameInfo &frameInfo, Func &&func) const {
            ForEachRenderSceneMeshInstance(
                frameInfo,
                [](const RenderMeshInstance &meshInstance) {
                    return meshInstance.IsDefaultLayerRenderable();
                },
                std::forward<Func>(func));
        }

#ifdef RAY_TRACING
        template<typename Func>
        void ForEachRayTracingMeshInstance(const FrameInfo &frameInfo, Func &&func) const {
            ForEachRenderSceneMeshInstance(
                frameInfo,
                [](const RenderMeshInstance &meshInstance) {
                    return meshInstance.IsRayTracingRenderable();
                },
                std::forward<Func>(func));
        }
#endif

        void SanitizeSelection(FrameInfo &frameInfo) {
            auto &selectionService = m_resourceManager->GetEditorSelectionService();
            if (selectionService.HasSelection() &&
                !EntityLifecycle::IsAlive(frameInfo, selectionService.GetSelectedId())) {
                selectionService.ClearSelection();
            }
            frameInfo.selectedEntityId =
                selectionService.HasSelection() ? selectionService.GetSelectedId() : EditorSelectionService::InvalidEntityId;
        }

        void BuildRenderScene(FrameInfo &frameInfo, Renderer &renderer) {
            m_renderScene = RenderSceneBuilder::Build(frameInfo, renderer);
            frameInfo.renderScene = &m_renderScene;
        }

        void CreateEditorPickingSystem(Material::Map &materials, Device &device, Renderer &renderer) {
            auto gizmosMaterialEntry = materials.find(Material::MaterialId::gizmos);
            if (gizmosMaterialEntry == materials.end()) {
                return;
            }

            auto &shaderLibrary = m_resourceManager->GetRenderCore().GetShaderLibrary();
            std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                    shaderLibrary.LoadStage("Editor/ObjectId.vert.spv", ShaderCategory::vertex),
                    shaderLibrary.LoadStage("Editor/ObjectId.frag.spv", ShaderCategory::fragment)
            };

            auto descriptorSetLayoutPointers = gizmosMaterialEntry->second->getDescriptorSetLayoutPointers();
            auto descriptorSetPointers = gizmosMaterialEntry->second->getDescriptorSetPointers();
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            const auto &bufferPointersRef = gizmosMaterialEntry->second->getBufferPointers();
            std::vector<std::shared_ptr<Buffer>> bufferPointers{bufferPointersRef.begin(), bufferPointersRef.end()};

            m_editorPickingMaterial = std::make_shared<Material>(
                    device,
                    -1000,
                    shaderModulePointers,
                    descriptorSetLayoutPointers,
                    descriptorSetPointers,
                    imagePointers,
                    samplerPointers,
                    bufferPointers,
                    PipelineCategory.Opaque);

            m_editorPickingRenderSystem = std::make_shared<EditorPickingRenderSystem>(
                    device,
                    renderer.getPickingRenderPass(),
                    m_editorPickingMaterial,
                    m_resourceManager->GetRenderCore().GetPipelineLibrary());
        }

        void RenderEditorPickingPass(Renderer &renderer, FrameInfo &frameInfo) {
            if (m_editorPickingRenderSystem == nullptr || frameInfo.renderScene == nullptr) {
                return;
            }

            renderer.beginPickingRenderPass(frameInfo.commandBuffer);
            m_editorPickingRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);

            ForEachDefaultLayerMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                m_editorPickingRenderSystem->render(frameInfo, meshInstance);
            });

            renderer.endPickingRenderPass(frameInfo.commandBuffer);
        }

        void RenderRasterScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr || frameInfo.renderScene == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            struct RenderQueueItem {
                std::shared_ptr<RenderSystem> renderSystem{};
                const RenderMeshInstance *meshInstance{nullptr};
                id_t entityId{0};
            };

            std::vector<RenderQueueItem> renderQueue;
            ForEachDefaultLayerMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                const auto renderSystemIt = m_renderSystemMap.find(meshInstance.materialId);
                if (renderSystemIt == m_renderSystemMap.end() || renderSystemIt->second == nullptr) {
                    return;
                }
                renderQueue.push_back({renderSystemIt->second, &meshInstance, meshInstance.entityId});
            });

            std::sort(renderQueue.begin(), renderQueue.end(), [](const auto &a, const auto &b) {
                return a.renderSystem->GetRenderQueue() < b.renderSystem->GetRenderQueue();
            });

            for (auto &item: renderQueue) {
                auto renderSystem = item.renderSystem;
                renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
                const RenderMeshInstance &meshInstance = *item.meshInstance;
                const auto &pipelineCategory = renderSystem->GetPipelineCategory();
                if (pipelineCategory == PipelineCategory.SkyBox ||
                    pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem->render(frameInfo, meshInstance.entityId, sceneRegistry);
                } else {
                    renderSystem->render(frameInfo, meshInstance, &sceneRegistry);
                }
            }
        }
#ifdef RAY_TRACING
        void SyncRayTracingScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr || frameInfo.renderScene == nullptr) {
                return;
            }

            auto &rayTracingSceneContext = m_resourceManager->GetRayTracingSceneContext();
            auto &renderResourceRegistry = m_resourceManager->GetRenderCore().GetResourceRegistry();
            rayTracingSceneContext.ProcessDeferredDestroy();
            auto &sceneRegistry = *frameInfo.sceneRegistry;
            bool addedNewTlasInstance = false;
            bool tlasHandleChanged = false;

            auto processMeshInstance = [&](const RenderMeshInstance &meshInstance, const std::shared_ptr<Model> &model) {
                if (model == nullptr) {
                    return;
                }

                RayTracingInstanceComponent *rayTracingInstanceComponent = nullptr;
                if (!sceneRegistry.TryGetComponent(meshInstance.entityId, rayTracingInstanceComponent) || rayTracingInstanceComponent == nullptr) {
                    rayTracingInstanceComponent =
                        sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(meshInstance.entityId, rayTracingSceneContext.AllocateInstanceId());
                } else if (!rayTracingInstanceComponent->IsValid()) {
                    rayTracingInstanceComponent->instanceId = rayTracingSceneContext.AllocateInstanceId();
                }

                const bool isActive = meshInstance.active && meshInstance.visible;
                const uint32_t instanceMask = isActive ? (1u << std::min(meshInstance.renderLayer, 7u)) : 0x00;
                const glm::mat4 currentTransform = meshInstance.worldTransform;
                const id_t tlasId = rayTracingInstanceComponent->instanceId;
                m_entityToTlasId[meshInstance.entityId] = tlasId;

                if (static_cast<size_t>(tlasId) >= static_cast<size_t>(RuntimeEntityDescCapacity)) {
                    return;
                }

                if (static_cast<size_t>(tlasId) >= frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs.resize(static_cast<size_t>(tlasId) + 1);
                }

                EntityDesc &entityDesc = frameInfo.pEntityDescs[tlasId];
                EntityDesc baseEntityDesc{};
                const bool hasBaseEntityDesc =
                    m_resourceManager->TryGetRayTracingMaterialDesc(meshInstance.materialId, baseEntityDesc);
                if (hasBaseEntityDesc) {
                    entityDesc = baseEntityDesc;
                } else if (entityDesc.vertexBufferAddress == 0 || entityDesc.indexBufferAddress == 0) {
                    entityDesc.textureEntry = glm::ivec2{0, 0};
                    entityDesc.pbr.albedo = glm::vec3{0.8f, 0.2f, 0.2f};
                    entityDesc.pbr.normal = glm::vec3{0.0f};
                    entityDesc.pbr.metallic = 0.0f;
                    entityDesc.pbr.roughness = 1.0f;
                    entityDesc.pbr.opacity = 1.0f;
                    entityDesc.pbr.AO = 1.0f;
                    entityDesc.pbr.emissive = glm::vec3{0.0f};
                }

                entityDesc.vertexBufferAddress = model->getVertexBuffer()->getDeviceAddress();
                entityDesc.indexBufferAddress = model->getIndexBuffer()->getDeviceAddress();
                if (meshInstance.pbrOverride.has_value()) {
                    entityDesc.pbr = *meshInstance.pbrOverride;
                }
                entityDesc.renderOptions =
                    (meshInstance.castShadow ? EntityRenderOptionCastShadow : 0) |
                    (meshInstance.receiveShadow ? EntityRenderOptionReceiveShadow : 0);
                entityDesc.renderLayer = static_cast<int32_t>(std::min(meshInstance.renderLayer, 7u));

                if (!rayTracingSceneContext.HasBlas(model)) {
                    rayTracingSceneContext.EnsureBlasBuilt(model);
                }

                if (!rayTracingSceneContext.HasInstance(tlasId)) {
                    const uint32_t shaderOffset = m_resourceManager->GetRayTracingShaderOffset(meshInstance.materialId);
                    rayTracingSceneContext.CreateInstance(
                        *model,
                        tlasId,
                        static_cast<id_t>(shaderOffset),
                        currentTransform,
                        instanceMask);
                    addedNewTlasInstance = true;
                    frameInfo.sceneUpdated = true;
                }

                auto maskEntry = m_meshRendererMaskCache.find(meshInstance.entityId);
                bool maskChanged = maskEntry == m_meshRendererMaskCache.end();
                if (maskChanged) {
                    m_meshRendererMaskCache.emplace(meshInstance.entityId, instanceMask);
                } else if (maskEntry->second != instanceMask) {
                    maskChanged = true;
                    maskEntry->second = instanceMask;
                }

                auto transformEntry = m_meshRendererTransformCache.find(meshInstance.entityId);
                bool transformChanged = transformEntry == m_meshRendererTransformCache.end();
                if (transformChanged) {
                    m_meshRendererTransformCache.emplace(meshInstance.entityId, currentTransform);
                } else if (transformEntry->second != currentTransform) {
                    transformChanged = true;
                    transformEntry->second = currentTransform;
                }

                if (maskChanged || transformChanged) {
                    if (rayTracingSceneContext.UpdateInstance(tlasId, currentTransform, instanceMask)) {
                        frameInfo.sceneUpdated = true;
                    }
                }
            };

            ForEachRayTracingMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                const auto *meshResource = renderResourceRegistry.GetMesh(meshInstance.meshResource);
                if (meshResource == nullptr || meshResource->legacyModel == nullptr) {
                    return;
                }
                processMeshInstance(meshInstance, meshResource->legacyModel);
            });

            for (auto it = m_entityToTlasId.begin(); it != m_entityToTlasId.end();) {
                MeshRendererComponent *meshRendererComponent = nullptr;
                const bool hasMeshRenderer = EntityLifecycle::IsAlive(frameInfo, it->first) &&
                                             sceneRegistry.TryGetComponent(it->first, meshRendererComponent) &&
                                             meshRendererComponent != nullptr;
                if (hasMeshRenderer) {
                    ++it;
                    continue;
                }

                const id_t staleTlasId = it->second;
                rayTracingSceneContext.RetireInstance(staleTlasId);
                if (staleTlasId >= 0 && static_cast<size_t>(staleTlasId) < frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs[staleTlasId] = EntityDesc{};
                }
                m_meshRendererTransformCache.erase(it->first);
                m_meshRendererMaskCache.erase(it->first);
                frameInfo.sceneUpdated = true;
                it = m_entityToTlasId.erase(it);
            }

            for (auto it = m_meshRendererMaskCache.begin(); it != m_meshRendererMaskCache.end();) {
                if (!EntityLifecycle::IsAlive(frameInfo, it->first)) {
                    m_meshRendererTransformCache.erase(it->first);
                    it = m_meshRendererMaskCache.erase(it);
                } else {
                    ++it;
                }
            }

            if (addedNewTlasInstance || rayTracingSceneContext.RequiresRebuild()) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, false);
                rayTracingSceneContext.ClearBuildFlags();
            } else if (rayTracingSceneContext.ShouldUpdate()) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, true);
                rayTracingSceneContext.ClearBuildFlags();
            }

            if (tlasHandleChanged) {
                m_resourceManager->RefreshRayTracingTlasDescriptor();
            }

            m_resourceManager->GetEntityDescs() = frameInfo.pEntityDescs;
        }
#endif

        std::shared_ptr<ResourceManager> m_resourceManager;

        std::unordered_map<id_t, std::shared_ptr<RenderSystem>> m_renderSystemMap;
        std::shared_ptr<PostSystem> m_postSystem;
        std::shared_ptr<GizmosRenderSystem> m_gizmosRenderSystem;
        std::shared_ptr<ComputeSystem> m_computeSystem;
        std::shared_ptr<Material> m_editorPickingMaterial;
        std::shared_ptr<EditorPickingRenderSystem> m_editorPickingRenderSystem;
        LightSystem m_lightSystem;
        RenderScene m_renderScene{};

#ifdef RAY_TRACING
        std::shared_ptr<RayTracingSystem> m_rayTracingSystem;
        std::unordered_map<id_t, uint32_t> m_meshRendererMaskCache{};
        std::unordered_map<id_t, glm::mat4> m_meshRendererTransformCache{};
        std::unordered_map<id_t, id_t> m_entityToTlasId{};
        int m_lastPresentedSceneImageIndex = 0;
#else
        std::shared_ptr<ShadowSystem> m_shadowSystem;
#endif
    };
}

