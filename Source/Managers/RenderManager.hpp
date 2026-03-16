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
#include "../Components/RayTracingInstanceComponent.hpp"
#include "../RenderCore/FrameData.hpp"
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
            UpdateUbo(frameInfo);

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
            frameInfo.pEntityDescBuffer->writeToBuffer(frameInfo.pEntityDescs.data(), frameInfo.pEntityDescs.size() * sizeof(EntityDesc));

            renderer.beginSceneColorRenderPass(frameInfo.commandBuffer, frameIndex % 2);
            RenderRasterScene(frameInfo);
            renderer.endSceneColorRenderPass(frameInfo.commandBuffer);
            renderer.setSceneColorToPostSynchronization(frameInfo.commandBuffer, frameIndex % 2);

            m_rayTracingSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
            m_rayTracingSystem->rayTrace(frameInfo);

            renderer.setDenoiseRtxToComputeSynchronization(frameInfo.commandBuffer, frameIndex % 2);

            m_computeSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
            m_computeSystem->render(frameInfo);

            renderer.setDenoiseComputeToPostSynchronization(frameInfo.commandBuffer, frameIndex % 2);

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

            std::vector<std::pair<std::shared_ptr<RenderSystem>, id_t>> renderQueue;
            if (frameInfo.sceneRegistry != nullptr) {
                auto &sceneRegistry = *frameInfo.sceneRegistry;
                for (const id_t entityId: sceneRegistry.View<MeshRendererComponent, TransformComponent>()) {
                    if (!sceneRegistry.IsEntityActive(entityId)) {
                        continue;
                    }

                    MeshRendererComponent *meshRendererComponent = nullptr;
                    if (!sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr) {
                        continue;
                    }
                    if (!meshRendererComponent->IsVisible() || !meshRendererComponent->IsOnDefaultRenderLayer()) {
                        continue;
                    }

                    const auto renderSystemIt = m_renderSystemMap.find(meshRendererComponent->GetMaterialID());
                    if (renderSystemIt == m_renderSystemMap.end() || renderSystemIt->second == nullptr) {
                        continue;
                    }
                    renderQueue.emplace_back(renderSystemIt->second, entityId);
                }

                std::sort(renderQueue.begin(), renderQueue.end(), [](const auto &a, const auto &b) {
                    return a.first->GetRenderQueue() < b.first->GetRenderQueue();
                });

                for (auto &item: renderQueue) {
                    auto renderSystem = item.first;
                    const id_t entityId = item.second;
                    renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameIndex);
                    renderSystem->render(frameInfo, entityId, sceneRegistry);
                }
            }
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
            if (m_editorPickingRenderSystem == nullptr || frameInfo.sceneRegistry == nullptr) {
                return;
            }

            renderer.beginPickingRenderPass(frameInfo.commandBuffer);
            m_editorPickingRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);

            for (const auto entityId: frameInfo.sceneRegistry->GetEntityOrder()) {
                m_editorPickingRenderSystem->render(frameInfo, entityId, *frameInfo.sceneRegistry);
            }

            renderer.endPickingRenderPass(frameInfo.commandBuffer);
        }

        void RenderRasterScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            std::vector<std::pair<std::shared_ptr<RenderSystem>, id_t>> renderQueue;
            for (const id_t entityId: sceneRegistry.View<MeshRendererComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId)) {
                    continue;
                }

                MeshRendererComponent *meshRendererComponent = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr) {
                    continue;
                }
                if (!meshRendererComponent->IsVisible() || !meshRendererComponent->IsOnDefaultRenderLayer()) {
                    continue;
                }

                const auto renderSystemIt = m_renderSystemMap.find(meshRendererComponent->GetMaterialID());
                if (renderSystemIt == m_renderSystemMap.end() || renderSystemIt->second == nullptr) {
                    continue;
                }
                renderQueue.emplace_back(renderSystemIt->second, entityId);
            }

            std::sort(renderQueue.begin(), renderQueue.end(), [](const auto &a, const auto &b) {
                return a.first->GetRenderQueue() < b.first->GetRenderQueue();
            });

            for (auto &item: renderQueue) {
                auto renderSystem = item.first;
                const id_t entityId = item.second;
                renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
                renderSystem->render(frameInfo, entityId, sceneRegistry);
            }
        }
#ifdef RAY_TRACING
        void SyncRayTracingScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &rayTracingSceneContext = m_resourceManager->GetRayTracingSceneContext();
            rayTracingSceneContext.ProcessDeferredDestroy();
            auto &sceneRegistry = *frameInfo.sceneRegistry;
            bool addedNewTlasInstance = false;
            bool tlasHandleChanged = false;

            for (const auto entityId: sceneRegistry.View<MeshRendererComponent, TransformComponent>()) {
                MeshRendererComponent *meshRendererComponent = nullptr;
                TransformComponent *transformComponent = nullptr;
                RayTracingInstanceComponent *rayTracingInstanceComponent = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                    !sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                    meshRendererComponent->GetModelPtr() == nullptr) {
                    continue;
                }

                if (!sceneRegistry.TryGetComponent(entityId, rayTracingInstanceComponent) || rayTracingInstanceComponent == nullptr) {
                    rayTracingInstanceComponent =
                        sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(entityId, rayTracingSceneContext.AllocateInstanceId());
                } else if (!rayTracingInstanceComponent->IsValid()) {
                    rayTracingInstanceComponent->instanceId = rayTracingSceneContext.AllocateInstanceId();
                }

                const bool isActive = sceneRegistry.IsEntityActive(entityId) && meshRendererComponent->IsVisible();
                const uint32_t instanceMask = isActive ? meshRendererComponent->GetRayTracingVisibilityMask() : 0x00;
                const glm::mat4 currentTransform = transformComponent->mat4();
                const id_t tlasId = rayTracingInstanceComponent->instanceId;
                m_entityToTlasId[entityId] = tlasId;

                if (static_cast<size_t>(tlasId) >= static_cast<size_t>(RuntimeEntityDescCapacity)) {
                    continue;
                }

                if (static_cast<size_t>(tlasId) >= frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs.resize(static_cast<size_t>(tlasId) + 1);
                }

                EntityDesc &entityDesc = frameInfo.pEntityDescs[tlasId];
                EntityDesc baseEntityDesc{};
                const bool hasBaseEntityDesc =
                    m_resourceManager->TryGetRayTracingMaterialDesc(meshRendererComponent->GetMaterialID(), baseEntityDesc);
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
                entityDesc.vertexBufferAddress = meshRendererComponent->GetModelPtr()->getVertexBuffer()->getDeviceAddress();
                entityDesc.indexBufferAddress = meshRendererComponent->GetModelPtr()->getIndexBuffer()->getDeviceAddress();
                if (meshRendererComponent->HasPbrOverride()) {
                    entityDesc.pbr = *meshRendererComponent->GetPbrOverride();
                }
                entityDesc.renderOptions =
                    (meshRendererComponent->CastsShadow() ? EntityRenderOptionCastShadow : 0) |
                    (meshRendererComponent->ReceivesShadow() ? EntityRenderOptionReceiveShadow : 0);
                entityDesc.renderLayer = static_cast<int32_t>(std::min(meshRendererComponent->GetRenderLayer(), 7u));

                if (!rayTracingSceneContext.HasBlas(meshRendererComponent->GetModelPtr())) {
                    rayTracingSceneContext.EnsureBlasBuilt(meshRendererComponent->GetModelPtr());
                }

                if (!rayTracingSceneContext.HasInstance(tlasId)) {
                    const uint32_t shaderOffset = m_resourceManager->GetRayTracingShaderOffset(meshRendererComponent->GetMaterialID());
                    rayTracingSceneContext.CreateInstance(
                        *meshRendererComponent->GetModelPtr(),
                        tlasId,
                        static_cast<id_t>(shaderOffset),
                        currentTransform,
                        instanceMask);
                    addedNewTlasInstance = true;
                    frameInfo.sceneUpdated = true;
                }

                auto maskEntry = m_meshRendererMaskCache.find(entityId);
                bool maskChanged = maskEntry == m_meshRendererMaskCache.end();
                if (maskChanged) {
                    m_meshRendererMaskCache.emplace(entityId, instanceMask);
                } else if (maskEntry->second != instanceMask) {
                    maskChanged = true;
                    maskEntry->second = instanceMask;
                }

                auto transformEntry = m_meshRendererTransformCache.find(entityId);
                bool transformChanged = transformEntry == m_meshRendererTransformCache.end();
                if (transformChanged) {
                    m_meshRendererTransformCache.emplace(entityId, currentTransform);
                } else if (transformEntry->second != currentTransform) {
                    transformChanged = true;
                    transformEntry->second = currentTransform;
                }

                if (maskChanged || transformChanged) {
                    if (rayTracingSceneContext.UpdateInstance(tlasId, currentTransform, instanceMask)) {
                        frameInfo.sceneUpdated = true;
                    }
                }
            }

            for (auto it = m_entityToTlasId.begin(); it != m_entityToTlasId.end();) {
                MeshRendererComponent *meshRendererComponent = nullptr;
                const bool hasMeshRenderer = sceneRegistry.IsAlive(it->first) &&
                                             sceneRegistry.TryGetComponent(it->first, meshRendererComponent) &&
                                             meshRendererComponent != nullptr;
                if (hasMeshRenderer) {
                    ++it;
                    continue;
                }

                const id_t staleTlasId = it->second;
                rayTracingSceneContext.UpdateInstance(staleTlasId, glm::mat4{1.0f}, 0x00);
                if (staleTlasId >= 0 && static_cast<size_t>(staleTlasId) < frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs[staleTlasId] = EntityDesc{};
                }
                m_meshRendererTransformCache.erase(it->first);
                m_meshRendererMaskCache.erase(it->first);
                frameInfo.sceneUpdated = true;
                it = m_entityToTlasId.erase(it);
            }

            for (auto it = m_meshRendererMaskCache.begin(); it != m_meshRendererMaskCache.end();) {
                if (!sceneRegistry.IsAlive(it->first)) {
                    m_meshRendererTransformCache.erase(it->first);
                    it = m_meshRendererMaskCache.erase(it);
                } else {
                    ++it;
                }
            }

            if (addedNewTlasInstance) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, false);
                rayTracingSceneContext.ClearUpdateFlag();
            } else if (rayTracingSceneContext.ShouldUpdate()) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, true);
                rayTracingSceneContext.ClearUpdateFlag();
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

