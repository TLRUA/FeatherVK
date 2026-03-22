#include <utility>

#include "../RenderSystems/RenderSystem.h"
#include "../RenderSystems/ShadowSystem.hpp"
#include "../RenderSystems/GrassSystem.hpp"
#include "../RenderSystems/SkyBoxSystem.hpp"
#include "../RenderSystems/RayTracingSystem.hpp"
#include "../RenderSystems/PostSystem.hpp"
#include "../RenderSystems/GizmosRenderSystem.hpp"
#include "../RenderSystems/ComputeSystem.hpp"
#include "../RenderSystems/EditorPickingRenderSystem.hpp"
#include "../ShaderBuilder.h"
#ifdef RAY_TRACING
#include "../RayTracing/TLAS.hpp"
#endif

namespace FeatherVK {
    class RenderManager {
    public:
        RenderManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = std::move(resourceManager);
            CreateRenderSystems(m_resourceManager->GetMaterials(), m_resourceManager->GetDevice(), m_resourceManager->GetRenderer());
        };

        ~RenderManager() {};

        RenderManager(const RenderManager &) = delete;

        RenderManager &operator=(const RenderManager &) = delete;

        void CreateRenderSystems(Material::Map &materials, Device &device, Renderer &renderer) {
            for (auto &materialPair: materials) {
                auto _material = materialPair.second;
                auto pipelineCategory = _material->getPipelineCategory();

                if (pipelineCategory == PipelineCategory.Gizmos) {
                    m_gizmosRenderSystem = std::make_shared<GizmosRenderSystem>(device, renderer.getSwapChainRenderPass(), _material);
                    continue;
//          This render system contains multiple pipelines, so I initialize it in the constructor.
//          m_gizmosRenderSystem->Init();
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

                if (m_renderSystemMap.find(_material->getMaterialId()) != m_renderSystemMap.end()) continue;

                std::shared_ptr<RenderSystem> _renderSystem;
                if (pipelineCategory == PipelineCategory.Shadow) {
                    m_shadowSystem = std::make_shared<ShadowSystem>(device, renderer.getShadowRenderPass(), _material);
                    continue;
                }

                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    _renderSystem = std::make_shared<GrassSystem>(device, renderer.getSwapChainRenderPass(), _material);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    _renderSystem = std::make_shared<SkyBoxSystem>(device, renderer.getSwapChainRenderPass(), _material);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    _renderSystem = std::make_shared<RenderSystem>(device, renderer.getSwapChainRenderPass(), _material);
                }

                if (_renderSystem != nullptr) {
                    _renderSystem->Init();
                    m_renderSystemMap[_material->getMaterialId()] = _renderSystem;
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

            auto _frameIndex = frameInfo.frameIndex;
#ifdef RAY_TRACING
            SyncRayTracingScene(frameInfo);

            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry, &frameInfo.pGameObjectDescs, &hierarchyTree, frameInfo);
            frameInfo.pGameObjectDescBuffer->writeToBuffer(frameInfo.pGameObjectDescs.data(), frameInfo.pGameObjectDescs.size() * sizeof(GameObjectDesc));
            m_rayTracingSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, _frameIndex);
            m_rayTracingSystem->rayTrace(frameInfo);

            renderer.setDenoiseRtxToComputeSynchronization(frameInfo.commandBuffer, _frameIndex % 2);

            m_computeSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, _frameIndex);
            m_computeSystem->render(frameInfo);

            renderer.setDenoiseComputeToPostSynchronization(frameInfo.commandBuffer, _frameIndex % 2);

            RenderEditorPickingPass(renderer, frameInfo);

            renderer.beginSwapChainRenderPass(frameInfo.commandBuffer);

            m_postSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, _frameIndex);
            m_postSystem->render(frameInfo);
#else
            renderer.beginShadowRenderPass(frameInfo.commandBuffer);
            m_shadowSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, _frameIndex);
            m_shadowSystem->renderShadow(frameInfo);
            renderer.endShadowRenderPass(frameInfo.commandBuffer);

            renderer.setShadowMapSynchronization(frameInfo.commandBuffer);

            RenderEditorPickingPass(renderer, frameInfo);

            renderer.beginSwapChainRenderPass(frameInfo.commandBuffer);

            //Todo: SceneManager
            std::vector<std::pair<std::shared_ptr<RenderSystem>, GameObject *>> _renderQueue;
            for (auto &item: frameInfo.gameObjects) {
                auto &_gameObject = item.second;
                if (!_gameObject.IsActive()) continue;
                MeshRendererComponent *_meshRendererComponent;
                if (_gameObject.TryGetComponent(_meshRendererComponent)) {
                    _renderQueue.push_back(std::make_pair(m_renderSystemMap[_meshRendererComponent->GetMaterialID()], &_gameObject));
                    auto _renderSystem = m_renderSystemMap[_meshRendererComponent->GetMaterialID()];
                }
            }

            std::sort(_renderQueue.begin(), _renderQueue.end(), [](const auto &a, const auto &b) {
                return a.first->GetRenderQueue() < b.first->GetRenderQueue();
            });

            for (auto &item: _renderQueue) {
                auto _renderSystem = item.first;
                auto &_gameObject = *item.second;
                _renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, _frameIndex);
                _renderSystem->render(frameInfo, &_gameObject);
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
            if (m_editorPickingRenderSystem == nullptr) {
                return;
            }

            renderer.beginPickingRenderPass(frameInfo.commandBuffer);
            m_editorPickingRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);

            if (frameInfo.sceneRegistry != nullptr) {
                for (const auto entityId: frameInfo.sceneRegistry->GetEntityOrder()) {
                    m_editorPickingRenderSystem->renderEntity(frameInfo, entityId, *frameInfo.sceneRegistry);
                }
            } else {
                for (auto &item: frameInfo.gameObjects) {
                    m_editorPickingRenderSystem->render(frameInfo, &item.second);
                }
            }

            renderer.endPickingRenderPass(frameInfo.commandBuffer);
        }

#ifdef RAY_TRACING
        void SyncRayTracingScene(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry != nullptr) {
                auto &sceneRegistry = *frameInfo.sceneRegistry;

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

                    auto activeStateEntry = m_meshRendererActiveState.find(entityId);
                    if (activeStateEntry == m_meshRendererActiveState.end()) {
                        m_meshRendererActiveState.emplace(entityId, isActive);
                        if (!isActive) {
                            TLAS::updateTLAS(meshRendererComponent->GetTLASId(), currentTransform, 0x00);
                            frameInfo.sceneUpdated = true;
                        }
                    } else if (activeStateEntry->second != isActive) {
                        TLAS::updateTLAS(meshRendererComponent->GetTLASId(), currentTransform, isActive ? 0xFF : 0x00);
                        frameInfo.sceneUpdated = true;
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
                        TLAS::updateTLAS(meshRendererComponent->GetTLASId(), currentTransform);
                        frameInfo.sceneUpdated = true;
                    }
                }

                for (auto it = m_meshRendererActiveState.begin(); it != m_meshRendererActiveState.end();) {
                    if (!sceneRegistry.IsAlive(it->first)) {
                        m_meshRendererTransformCache.erase(it->first);
                        it = m_meshRendererActiveState.erase(it);
                    } else {
                        ++it;
                    }
                }
            } else {
                for (auto &item: frameInfo.gameObjects) {
                    auto &gameObject = item.second;
                    MeshRendererComponent *meshRendererComponent;
                    if (!gameObject.TryGetComponent(meshRendererComponent) || meshRendererComponent->GetModelPtr() == nullptr) {
                        continue;
                    }

                    const id_t gameObjectId = gameObject.GetId();
                    const bool isActive = gameObject.IsActive();

                    auto activeStateEntry = m_meshRendererActiveState.find(gameObjectId);
                    if (activeStateEntry == m_meshRendererActiveState.end()) {
                        m_meshRendererActiveState.emplace(gameObjectId, isActive);
                        if (!isActive) {
                            TLAS::updateTLAS(meshRendererComponent->GetTLASId(), gameObject.transform->mat4(), 0x00);
                            frameInfo.sceneUpdated = true;
                        }
                    } else if (activeStateEntry->second != isActive) {
                        TLAS::updateTLAS(meshRendererComponent->GetTLASId(), gameObject.transform->mat4(), isActive ? 0xFF : 0x00);
                        frameInfo.sceneUpdated = true;
                        activeStateEntry->second = isActive;
                    }

                    if (!isActive) {
                        continue;
                    }

                    if (meshRendererComponent->ConsumeTransformDirty()) {
                        TLAS::updateTLAS(meshRendererComponent->GetTLASId(), meshRendererComponent->GetCachedTransform());
                        frameInfo.sceneUpdated = true;
                    }
                }
            }

            if (TLAS::shouldUpdate) {
                TLAS::buildTLAS(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, true);
                TLAS::shouldUpdate = false;
            }
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
#else
        std::shared_ptr<ShadowSystem> m_shadowSystem;
#endif

    };
}

