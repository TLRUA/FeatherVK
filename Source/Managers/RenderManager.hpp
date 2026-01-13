#include <algorithm>
#include <utility>

#include "../Components/MeshRendererComponent.hpp"
#include "../RenderSystems/ComputeSystem.hpp"
#include "../RenderSystems/EditorPickingRenderSystem.hpp"
#include "../RenderSystems/GizmosRenderSystem.hpp"
#include "../RenderSystems/GrassSystem.hpp"
#include "../RenderSystems/PostSystem.hpp"
#include "../RenderSystems/RayTracingSystem.hpp"
#include "../RenderSystems/RenderSystem.h"
#include "../RenderSystems/ShadowSystem.hpp"
#include "../RenderSystems/SkyBoxSystem.hpp"
#include "../ShaderBuilder.h"
#include "ResourceManager.hpp"
#ifdef RAY_TRACING
#include "../RayTracing/TLAS.hpp"
#endif

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
            for (auto &materialPair: materials) {
                auto material = materialPair.second;
                const auto pipelineCategory = material->getPipelineCategory();

                if (pipelineCategory == PipelineCategory.Gizmos) {
                    m_gizmosRenderSystem = std::make_shared<GizmosRenderSystem>(device, renderer.getSwapChainRenderPass(), material);
                    continue;
                }

#ifdef RAY_TRACING
                if (pipelineCategory == PipelineCategory.RayTracing) {
                    m_rayTracingSystem = std::make_shared<RayTracingSystem>(device, nullptr, materialPair.second);
                    m_rayTracingSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Post) {
                    m_postSystem = std::make_shared<PostSystem>(device, renderer.getSwapChainRenderPass(), materialPair.second);
                    m_postSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Compute) {
                    m_computeSystem = std::make_shared<ComputeSystem>(device, nullptr, materialPair.second);
                    m_computeSystem->Init();
                }
#else
                if (m_renderSystemMap.find(material->getMaterialId()) != m_renderSystemMap.end()) {
                    continue;
                }

                std::shared_ptr<RenderSystem> renderSystem;
                if (pipelineCategory == PipelineCategory.Shadow) {
                    m_shadowSystem = std::make_shared<ShadowSystem>(device, renderer.getShadowRenderPass(), material);
                    continue;
                }

                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem = std::make_shared<GrassSystem>(device, renderer.getSwapChainRenderPass(), material);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    renderSystem = std::make_shared<SkyBoxSystem>(device, renderer.getSwapChainRenderPass(), material);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    renderSystem = std::make_shared<RenderSystem>(device, renderer.getSwapChainRenderPass(), material);
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
#ifndef RAY_TRACING
            frameInfo.globalUbo.shadowViewMatrix[0] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, 90, 180));
            frameInfo.globalUbo.shadowViewMatrix[1] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, -90, 180));
            frameInfo.globalUbo.shadowViewMatrix[2] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(-90, 0, 0));
            frameInfo.globalUbo.shadowViewMatrix[3] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(90, 0, 0));
            frameInfo.globalUbo.shadowViewMatrix[4] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(180, 0, 0));
            frameInfo.globalUbo.shadowViewMatrix[5] = m_shadowSystem->calculateViewMatrixForRotation(frameInfo.globalUbo.lights[0].position, glm::vec3(0, 0, 180));
            frameInfo.globalUbo.shadowProjMatrix = CameraComponent::CorrectionMatrix * glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 5.0f);
            frameInfo.globalUbo.lightProjectionViewMatrix = frameInfo.globalUbo.shadowProjMatrix * frameInfo.globalUbo.shadowViewMatrix[0];
#endif
        }

        void UpdateRendering(Renderer &renderer, FrameInfo &frameInfo, HierarchyTree &hierarchyTree) {
            UpdateUbo(frameInfo);

            const auto frameIndex = frameInfo.frameIndex;
#ifdef RAY_TRACING
            SyncRayTracingScene(frameInfo);

            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry, &frameInfo.pEntityDescs, &hierarchyTree, frameInfo);
            frameInfo.pEntityDescBuffer->writeToBuffer(frameInfo.pEntityDescs.data(), frameInfo.pEntityDescs.size() * sizeof(EntityDesc));
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
#else
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

            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry, &frameInfo.materials, &hierarchyTree, frameInfo);
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

            ShaderBuilder shaderBuilder(device);
            std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                    std::make_shared<ShaderModule>(shaderBuilder.createShaderModule("Editor/ObjectId.vert.spv"), ShaderCategory::vertex),
                    std::make_shared<ShaderModule>(shaderBuilder.createShaderModule("Editor/ObjectId.frag.spv"), ShaderCategory::fragment)
            };

            auto descriptorSetLayoutPointers = gizmosMaterialEntry->second->getDescriptorSetLayoutPointers();
            auto descriptorSetPointers = gizmosMaterialEntry->second->getDescriptorSetPointers();
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            const auto &bufferPointersRef = gizmosMaterialEntry->second->getBufferPointers();
            std::vector<std::shared_ptr<Buffer>> bufferPointers{bufferPointersRef.begin(), bufferPointersRef.end()};

            m_editorPickingMaterial = std::make_shared<Material>(
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
                    m_editorPickingMaterial);
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

#ifdef RAY_TRACING
        void SyncRayTracingScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            TLAS::ProcessDeferredDestroy();

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            bool addedNewTlasInstance = false;
            bool tlasHandleChanged = false;

            for (const auto entityId: sceneRegistry.View<MeshRendererComponent, TransformComponent>()) {
                MeshRendererComponent *meshRendererComponent = nullptr;
                TransformComponent *transformComponent = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                    !sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                    meshRendererComponent->GetModelPtr() == nullptr) {
                    continue;
                }

                const bool isActive = sceneRegistry.IsEntityActive(entityId);
                const glm::mat4 currentTransform = transformComponent->mat4();
                const bool transformDirty = meshRendererComponent->ConsumeTransformDirty();
                const id_t tlasId = meshRendererComponent->GetTLASId();
                m_entityToTlasId[entityId] = tlasId;

                if (tlasId < 0) {
                    continue;
                }

                if (static_cast<size_t>(tlasId) >= static_cast<size_t>(RuntimeEntityDescCapacity)) {
                    continue;
                }

                if (static_cast<size_t>(tlasId) >= frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs.resize(static_cast<size_t>(tlasId) + 1);
                }

                EntityDesc &entityDesc = frameInfo.pEntityDescs[tlasId];
                const bool descWasEmpty = entityDesc.vertexBufferAddress == 0 || entityDesc.indexBufferAddress == 0;
                entityDesc.vertexBufferAddress = meshRendererComponent->GetModelPtr()->getVertexBuffer()->getDeviceAddress();
                entityDesc.indexBufferAddress = meshRendererComponent->GetModelPtr()->getIndexBuffer()->getDeviceAddress();
                if (descWasEmpty) {
                    entityDesc.textureEntry = glm::ivec2{0, 0};
                    entityDesc.pbr.albedo = glm::vec3{0.8f, 0.2f, 0.2f};
                    entityDesc.pbr.normal = glm::vec3{0.0f};
                    entityDesc.pbr.metallic = 0.0f;
                    entityDesc.pbr.roughness = 1.0f;
                    entityDesc.pbr.opacity = 1.0f;
                    entityDesc.pbr.AO = 1.0f;
                    entityDesc.pbr.emissive = glm::vec3{0.0f};
                }

                if (!TLAS::HasTLASInstance(tlasId)) {
                    const uint32_t shaderOffset = ResolveRayTracingShaderOffset(sceneRegistry, entityId, meshRendererComponent->GetMaterialID());
                    TLAS::createTLAS(*meshRendererComponent->GetModelPtr(), tlasId, static_cast<id_t>(shaderOffset), currentTransform);
                    addedNewTlasInstance = true;
                    frameInfo.sceneUpdated = true;
                }

                auto activeStateEntry = m_meshRendererActiveState.find(entityId);
                if (activeStateEntry == m_meshRendererActiveState.end()) {
                    m_meshRendererActiveState.emplace(entityId, isActive);
                    if (!isActive) {
                        if (TLAS::updateTLAS(tlasId, currentTransform, 0x00)) {
                            frameInfo.sceneUpdated = true;
                        }
                    }
                } else if (activeStateEntry->second != isActive) {
                    if (TLAS::updateTLAS(tlasId, currentTransform, isActive ? 0xFF : 0x00)) {
                        frameInfo.sceneUpdated = true;
                    }
                    activeStateEntry->second = isActive;
                }

                auto transformEntry = m_meshRendererTransformCache.find(entityId);
                bool transformChanged = transformEntry == m_meshRendererTransformCache.end();
                if (transformChanged) {
                    m_meshRendererTransformCache.emplace(entityId, currentTransform);
                } else if (transformEntry->second != currentTransform) {
                    transformChanged = true;
                    transformEntry->second = currentTransform;
                }

                if (!isActive) {
                    continue;
                }

                if (transformDirty || transformChanged) {
                    if (TLAS::updateTLAS(tlasId, currentTransform)) {
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
                TLAS::updateTLAS(staleTlasId, glm::mat4{1.0f}, 0x00);
                if (staleTlasId >= 0 && static_cast<size_t>(staleTlasId) < frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs[staleTlasId] = EntityDesc{};
                }
                m_meshRendererTransformCache.erase(it->first);
                m_meshRendererActiveState.erase(it->first);
                frameInfo.sceneUpdated = true;
                it = m_entityToTlasId.erase(it);
            }

            for (auto it = m_meshRendererActiveState.begin(); it != m_meshRendererActiveState.end();) {
                if (!sceneRegistry.IsAlive(it->first)) {
                    m_meshRendererTransformCache.erase(it->first);
                    it = m_meshRendererActiveState.erase(it);
                } else {
                    ++it;
                }
            }

            if (addedNewTlasInstance) {
                tlasHandleChanged = TLAS::buildTLAS(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, false);
                TLAS::shouldUpdate = false;
            } else if (TLAS::shouldUpdate) {
                tlasHandleChanged = TLAS::buildTLAS(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, true);
                TLAS::shouldUpdate = false;
            }

            if (tlasHandleChanged) {
                m_resourceManager->RefreshRayTracingTlasDescriptor();
            }

            m_resourceManager->GetEntityDescs() = frameInfo.pEntityDescs;
        }

        uint32_t ResolveRayTracingShaderOffset(ECS::SceneRegistry &sceneRegistry,
                                               id_t entityId,
                                               Material::id_t materialId) const {
            for (const auto candidateEntityId: sceneRegistry.View<MeshRendererComponent>()) {
                if (candidateEntityId == entityId) {
                    continue;
                }

                MeshRendererComponent *candidateMeshRenderer = nullptr;
                if (!sceneRegistry.TryGetComponent(candidateEntityId, candidateMeshRenderer) || candidateMeshRenderer == nullptr) {
                    continue;
                }
                if (candidateMeshRenderer->GetMaterialID() != materialId) {
                    continue;
                }

                uint32_t shaderOffset = 0;
                if (TLAS::TryGetShaderOffset(candidateMeshRenderer->GetTLASId(), shaderOffset)) {
                    return shaderOffset;
                }
            }
            return 0;
        }
#endif

        std::shared_ptr<ResourceManager> m_resourceManager;

        std::unordered_map<id_t, std::shared_ptr<RenderSystem>> m_renderSystemMap;
        std::shared_ptr<PostSystem> m_postSystem;
        std::shared_ptr<GizmosRenderSystem> m_gizmosRenderSystem;
        std::shared_ptr<ComputeSystem> m_computeSystem;
        std::shared_ptr<Material> m_editorPickingMaterial;
        std::shared_ptr<EditorPickingRenderSystem> m_editorPickingRenderSystem;

#ifdef RAY_TRACING
        std::shared_ptr<RayTracingSystem> m_rayTracingSystem;
        std::unordered_map<id_t, bool> m_meshRendererActiveState{};
        std::unordered_map<id_t, glm::mat4> m_meshRendererTransformCache{};
        std::unordered_map<id_t, id_t> m_entityToTlasId{};
#else
        std::shared_ptr<ShadowSystem> m_shadowSystem;
#endif
    };
}

