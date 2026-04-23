#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../Components/CameraComponent.hpp"
#include "../Components/LightComponent.hpp"
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
#include "../RenderGraph/RenderGraph.hpp"
#include "../RenderCore/FrameData.hpp"
#include "../RenderPipeline/HybridRenderPipeline.hpp"
#include "../RenderPipeline/RasterRenderPipeline.hpp"
#include "../RenderPipeline/RayTracingRenderPipeline.hpp"
#include "EntityLifecycleUtils.hpp"
#include "ResourceManager.hpp"

namespace FeatherVK {
    namespace {
        RenderGraph::RenderGraphGraphicsPipelineTarget MakeSwapchainPipelineTarget(Renderer &renderer,
                                                                                   const std::string &keyPrefix,
                                                                                   VkRenderPass renderPass) {
            return {
                {keyPrefix + "/" + std::to_string(renderer.getSwapChainImageFormat()) + "/" +
                 std::to_string(renderer.getSwapChainDepthFormat()),
                 {renderer.getSwapChainImageFormat()},
                 renderer.getSwapChainDepthFormat(),
                 VK_SAMPLE_COUNT_1_BIT},
                renderPass};
        }

        const std::unordered_map<RenderGraph::RenderGraphResourceState, VkImageLayout> &ShaderReadOnlyLayoutOverrides() {
            using RGState = RenderGraph::RenderGraphResourceState;
            static const std::unordered_map<RGState, VkImageLayout> overrides{
                {RGState::ColorAttachmentWrite, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {RGState::ShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
            };
            return overrides;
        }

        const std::unordered_map<RenderGraph::RenderGraphResourceState, VkImageLayout> &PickingLayoutOverrides() {
            using RGState = RenderGraph::RenderGraphResourceState;
            static const std::unordered_map<RGState, VkImageLayout> overrides{
                {RGState::ColorAttachmentWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                {RGState::ShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
            };
            return overrides;
        }

        const std::unordered_map<RenderGraph::RenderGraphResourceState, VkImageLayout> &GeneralStorageLayoutOverrides() {
            using RGState = RenderGraph::RenderGraphResourceState;
            static const std::unordered_map<RGState, VkImageLayout> overrides{
                {RGState::ShaderWrite, VK_IMAGE_LAYOUT_GENERAL},
                {RGState::ShaderRead, VK_IMAGE_LAYOUT_GENERAL},
                {RGState::ShaderReadWrite, VK_IMAGE_LAYOUT_GENERAL}
            };
            return overrides;
        }
    }

    class ResourceManager;

    class RenderManager {
    public:
        RenderManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = std::move(resourceManager);
            CreateRenderSystems(m_resourceManager->GetMaterials(), m_resourceManager->GetDevice(), m_resourceManager->GetRenderer());
            CreateRenderPipelines();
        }

        ~RenderManager() = default;

        RenderManager(const RenderManager &) = delete;

        RenderManager &operator=(const RenderManager &) = delete;

        void CreateRenderSystems(Material::Map &materials, Device &device, Renderer &renderer) {
            auto &pipelineLibrary = m_resourceManager->GetRenderCore().GetPipelineLibrary();
            auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            const auto sceneColorPipelineTarget = resourceCache.GetSceneColorPipelineTarget();
            const auto shadowPipelineTarget = resourceCache.GetShadowPipelineTarget();
            const auto swapchainPipelineTarget = MakeSwapchainPipelineTarget(renderer, "Swapchain", renderer.getSwapChainRenderPass());
            const auto gizmoPipelineTarget = MakeSwapchainPipelineTarget(renderer, "Gizmo", renderer.getGizmosRenderPass());
            for (auto &materialPair: materials) {
                auto material = materialPair.second;
                const auto pipelineCategory = material->getPipelineCategory();

                if (pipelineCategory == PipelineCategory.Gizmos) {
                    m_gizmosRenderSystem = std::make_shared<GizmosRenderSystem>(
                        device,
                        gizmoPipelineTarget,
                        material,
                        m_resourceManager->GetModelRepository(),
                        m_resourceManager->GetRenderCore());
                    continue;
                }

#ifdef RAY_TRACING
                if (pipelineCategory == PipelineCategory.RayTracing) {
                    m_rayTracingSystem = std::make_shared<RayTracingSystem>(device, materialPair.second, pipelineLibrary);
                    m_rayTracingSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Post) {
                    m_postSystem = std::make_shared<PostSystem>(device, swapchainPipelineTarget, materialPair.second, pipelineLibrary);
                    m_postSystem->Init();
                }
                if (pipelineCategory == PipelineCategory.Compute) {
                    m_computeSystem = std::make_shared<ComputeSystem>(device, materialPair.second, pipelineLibrary);
                    m_computeSystem->Init();
                }

                if (m_renderSystemMap.find(material->getMaterialId()) != m_renderSystemMap.end()) {
                    continue;
                }

                std::shared_ptr<RenderSystem> renderSystem;
                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem = std::make_shared<GrassSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    renderSystem = std::make_shared<SkyBoxSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    renderSystem = std::make_shared<RenderSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
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
                    m_shadowSystem = std::make_shared<ShadowSystem>(device, shadowPipelineTarget, material, pipelineLibrary);
                    continue;
                }

                if (pipelineCategory == PipelineCategory.TessellationGeometry) {
                    renderSystem = std::make_shared<GrassSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.SkyBox) {
                    renderSystem = std::make_shared<SkyBoxSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
                } else if (pipelineCategory == PipelineCategory.Opaque || pipelineCategory == PipelineCategory.Overlay
                           || pipelineCategory == PipelineCategory.Light || pipelineCategory == PipelineCategory.Transparent) {
                    renderSystem = std::make_shared<RenderSystem>(device, sceneColorPipelineTarget, material, pipelineLibrary);
                }

                if (renderSystem != nullptr) {
                    renderSystem->Init();
                    m_renderSystemMap[material->getMaterialId()] = renderSystem;
                }
#endif
            }

            CreateEditorPickingSystem(materials, device, renderer);
        }

        void CreateRenderPipelines() {
#ifdef RAY_TRACING
            m_rayTracingPipeline = std::make_unique<RayTracingRenderPipeline>();
            m_hybridPipeline = std::make_unique<HybridRenderPipeline>();
#else
            m_rasterPipeline = std::make_unique<RasterRenderPipeline>();
#endif
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
            SanitizeSelection(frameInfo);
#ifdef RAY_TRACING
            frameInfo.rayTracingInstanceIds = &m_entityToTlasId;
#endif

            const auto frameIndex = frameInfo.frameIndex;
#ifdef RAY_TRACING
            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry,
                            frameInfo.pEntityDescs,
                            m_resourceManager->GetHierarchyService(),
                            m_resourceManager->GetEntityCommandService(),
                            m_resourceManager->GetEditorSelectionService(),
                            m_resourceManager->GetTransformService(),
                            frameInfo);
            m_resourceManager->SyncRenderCoreSceneResources();
            BuildRenderScene(frameInfo, renderer);
            UpdateUbo(frameInfo);
            if (GUI::IsLayoutInteractionActive()) {
                (void) frameIndex;
                ExecuteFrameGraph(
                    EnsureLayoutInteractionFrameGraph(renderer, frameInfo),
                    renderer,
                    frameInfo,
                    FrameGraphBindingKind::LayoutInteraction);
                return;
            }

            (void) frameIndex;
            ExecuteFrameGraph(
                EnsureRayTracingFrameGraph(renderer, frameInfo),
                renderer,
                frameInfo,
                FrameGraphBindingKind::RayTracing);
            return;
#else
            GUI::ShowWindow(ImVec2(frameInfo.extent.width, frameInfo.extent.height),
                            frameInfo.sceneRegistry,
                            &frameInfo.materials,
                            m_resourceManager->GetHierarchyService(),
                            m_resourceManager->GetEntityCommandService(),
                            m_resourceManager->GetEditorSelectionService(),
                            m_resourceManager->GetTransformService(),
                            frameInfo);
            m_resourceManager->SyncRenderCoreSceneResources();
            BuildRenderScene(frameInfo, renderer);
            UpdateUbo(frameInfo);
            if (GUI::IsLayoutInteractionActive()) {
                // Raster path renders directly to the swapchain; keep rendering to avoid a black scene region.
            }

            (void) frameIndex;
            ExecuteFrameGraph(
                EnsureRasterFrameGraph(renderer, frameInfo),
                renderer,
                frameInfo,
                FrameGraphBindingKind::Raster);
            return;
#endif
        }

    private:
        struct RasterRenderQueueItem {
            RenderSystem *renderSystem{nullptr};
            const RenderMeshInstance *meshInstance{nullptr};
        };

#ifdef RAY_TRACING
        struct RayTracingEntityState {
            RenderCore::RenderResourceHandle meshResource{};
            Material::id_t materialId{0};
            std::optional<PBR> pbrOverride{};
            int32_t renderOptions{0};
            int32_t renderLayer{0};
            glm::mat4 worldTransform{1.0f};
            uint32_t instanceMask{0};
            bool instanceStateInitialized{false};
        };

        struct RayTracingSyncStats {
            uint32_t processedEntityCount{0};
            uint32_t removedEntityCount{0};
            bool fullSync{false};
        };
#endif

        struct FrameGraphCacheKey {
            VkExtent2D frameExtent{};
            VkExtent2D sceneExtent{};
            uint64_t resourceTopologyVersion{0};

            [[nodiscard]] bool Matches(const FrameInfo &frameInfo,
                                       const RenderGraph::RenderGraphResourceCache &resourceCache) const {
                return frameExtent.width == frameInfo.extent.width &&
                       frameExtent.height == frameInfo.extent.height &&
                       sceneExtent.width == frameInfo.sceneRenderExtent.width &&
                       sceneExtent.height == frameInfo.sceneRenderExtent.height &&
                       resourceTopologyVersion == resourceCache.GetTopologyVersion();
            }

            static FrameGraphCacheKey Capture(const FrameInfo &frameInfo,
                                              const RenderGraph::RenderGraphResourceCache &resourceCache) {
                return {frameInfo.extent, frameInfo.sceneRenderExtent, resourceCache.GetTopologyVersion()};
            }
        };

        enum class FrameGraphBindingKind : uint8_t {
            LayoutInteraction,
            RayTracing,
            Raster
        };

        struct FrameGraphBindingCacheKey {
            uint32_t frameParity{0};
            uint32_t swapchainImageIndex{std::numeric_limits<uint32_t>::max()};

            [[nodiscard]] bool Matches(const FrameInfo &frameInfo,
                                       const Renderer &renderer,
                                       const RenderGraph::RenderGraphResourceCache &) const {
                return frameParity == static_cast<uint32_t>(frameInfo.frameIndex & 1u) &&
                       swapchainImageIndex == renderer.getCurrentImageIndex();
            }

            static FrameGraphBindingCacheKey Capture(const FrameInfo &frameInfo,
                                                     const Renderer &renderer,
                                                     const RenderGraph::RenderGraphResourceCache &) {
                return {
                    static_cast<uint32_t>(frameInfo.frameIndex & 1u),
                    renderer.getCurrentImageIndex()};
            }
        };

        struct CachedFrameGraphBindings {
            FrameGraphBindingCacheKey key{};
            RenderGraph::RenderGraphResourceBindings bindings{};
            bool valid{false};
        };

        struct FrameGraphBindingCacheBucket {
            inline static constexpr uint32_t MaxSwapchainImages = 8u;
            inline static constexpr uint32_t SlotCount = MaxSwapchainImages * 2u;
            uint64_t topologyVersion{0};
            std::array<CachedFrameGraphBindings, SlotCount> slots{};
        };

#ifdef RAY_TRACING
        void QueueDirtyRayTracingEntity(id_t entityId) {
            if (entityId == std::numeric_limits<id_t>::max()) {
                return;
            }
            m_rayTracingRemovedEntities.erase(entityId);
            m_rayTracingDirtyEntities.insert(entityId);
        }

        void QueueRemovedRayTracingEntity(id_t entityId) {
            if (entityId == std::numeric_limits<id_t>::max()) {
                return;
            }
            m_rayTracingDirtyEntities.erase(entityId);
            m_rayTracingRemovedEntities.insert(entityId);
        }
#endif

        void ExecuteFrameGraph(RenderGraph::RenderGraph &graph,
                               Renderer &renderer,
                               FrameInfo &frameInfo,
                               FrameGraphBindingKind bindingKind) {
            auto &blackboard = graph.GetBlackboard();
            blackboard.Set("RenderScene", frameInfo.renderScene);
            blackboard.Set("FrameIndex", frameInfo.frameIndex);
            const auto &bindings = EnsureFrameGraphResourceBindings(bindingKind, graph, renderer, frameInfo);
            renderer.ExecuteGraph(
                graph,
                frameInfo,
                bindings,
                m_resourceManager->GetRenderGraphResourceCache());
        }

        void RenderGizmoDraws(RenderGraph::RenderGraphPassContext &context) {
            auto &frameInfo = context.frameInfo;
            m_gizmosRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
            m_gizmosRenderSystem->Record(context, GizmosType::EdgeDetectionStencil);
            m_gizmosRenderSystem->Record(context, GizmosType::EdgeDetection);
            m_gizmosRenderSystem->Record(context, GizmosType::Axis);
        }

        static RenderGraph::RenderGraphImageBinding ImageBinding(
            const std::shared_ptr<Image> &image,
            VkImageAspectFlags aspectMask,
            std::unordered_map<RenderGraph::RenderGraphResourceState, VkImageLayout> layoutOverrides = {},
            bool enableBarriers = true) {
            RenderGraph::RenderGraphImageBinding binding{};
            binding.image = image == nullptr ? VK_NULL_HANDLE : image->getImage();
            binding.subresourceRange.aspectMask = aspectMask;
            binding.subresourceRange.baseMipLevel = 0;
            binding.subresourceRange.levelCount = 1;
            binding.subresourceRange.baseArrayLayer = 0;
            binding.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            binding.layoutOverrides = std::move(layoutOverrides);
            binding.enableBarriers = enableBarriers;
            return binding;
        }

        static RenderGraph::RenderGraphImageBinding SwapchainBinding(VkImage image) {
            RenderGraph::RenderGraphImageBinding binding{};
            binding.image = image;
            binding.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            binding.subresourceRange.baseMipLevel = 0;
            binding.subresourceRange.levelCount = 1;
            binding.subresourceRange.baseArrayLayer = 0;
            binding.subresourceRange.layerCount = 1;
            binding.enableBarriers = false;
            return binding;
        }

        RenderGraph::RenderGraphResourceBindings BuildFrameGraphResourceBindings(const RenderGraph::RenderGraph &graph,
                                                                                 Renderer &renderer,
                                                                                 const FrameInfo &frameInfo) const {
            RenderGraph::RenderGraphResourceBindings bindings{};
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            const uint32_t imageIndex = static_cast<uint32_t>(frameInfo.frameIndex & 1u);
            bindings.ReserveImages(8);

            const auto bindImage = [&](const std::string &name, RenderGraph::RenderGraphImageBinding binding) {
                const auto handle = graph.TryFindResource(name, RenderGraph::ResourceType::Texture);
                if (!handle.IsValid()) {
                    return;
                }
                bindings.BindImage(handle, name, std::move(binding));
            };

            bindImage("SceneColor", ImageBinding(
                resourceCache.GetSceneColorImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                ShaderReadOnlyLayoutOverrides()));

            bindImage("PickingTarget", ImageBinding(
                resourceCache.GetPickingIdImage(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                PickingLayoutOverrides()));

            bindImage("ShadowMap", ImageBinding(
                resourceCache.GetShadowImage(),
                VK_IMAGE_ASPECT_DEPTH_BIT,
                {
                    {RenderGraph::RenderGraphResourceState::DepthStencilWrite, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                    {RenderGraph::RenderGraphResourceState::ShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                }));

#ifdef RAY_TRACING
            bindImage("RayTracingOutput", ImageBinding(
                resourceCache.GetRayTracingOutputImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindImage("WorldPosition", ImageBinding(
                resourceCache.GetWorldPositionImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindImage("ShadowTerm", ImageBinding(
                resourceCache.GetShadowTermImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindImage("RayTracingGuide", ImageBinding(
                resourceCache.GetRayTracingGuideImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindImage("DenoiseAccumulation", ImageBinding(
                resourceCache.GetDenoiseAccumulationImage(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
#endif

            bindImage("Swapchain", SwapchainBinding(renderer.getSwapChainImage(renderer.getCurrentImageIndex())));
            return bindings;
        }

        FrameGraphBindingCacheBucket &GetFrameGraphBindingCache(FrameGraphBindingKind bindingKind) {
            switch (bindingKind) {
#ifdef RAY_TRACING
                case FrameGraphBindingKind::LayoutInteraction:
                    return m_layoutInteractionBindingCache;
                case FrameGraphBindingKind::RayTracing:
                    return m_rayTracingBindingCache;
#else
                case FrameGraphBindingKind::LayoutInteraction:
                    return m_layoutInteractionBindingCache;
#endif
                case FrameGraphBindingKind::Raster:
                    return m_rasterBindingCache;
            }

            return m_rasterBindingCache;
        }

        static void InvalidateFrameGraphBindingCache(FrameGraphBindingCacheBucket &cacheBucket, uint64_t topologyVersion) {
            cacheBucket.topologyVersion = topologyVersion;
            for (auto &slot: cacheBucket.slots) {
                slot.valid = false;
            }
        }

        static size_t ResolveFrameGraphBindingCacheSlot(const FrameInfo &frameInfo, const Renderer &renderer) {
            const uint32_t frameParity = static_cast<uint32_t>(frameInfo.frameIndex & 1u);
            const uint32_t clampedSwapchainIndex = std::min(
                renderer.getCurrentImageIndex(),
                FrameGraphBindingCacheBucket::MaxSwapchainImages - 1u);
            return static_cast<size_t>(frameParity * FrameGraphBindingCacheBucket::MaxSwapchainImages + clampedSwapchainIndex);
        }

        const RenderGraph::RenderGraphResourceBindings &EnsureFrameGraphResourceBindings(FrameGraphBindingKind bindingKind,
                                                                                         const RenderGraph::RenderGraph &graph,
                                                                                         Renderer &renderer,
                                                                                         const FrameInfo &frameInfo) {
            auto &bindingCache = GetFrameGraphBindingCache(bindingKind);
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            const uint64_t topologyVersion = resourceCache.GetTopologyVersion();
            if (bindingCache.topologyVersion != topologyVersion) {
                InvalidateFrameGraphBindingCache(bindingCache, topologyVersion);
            }

            const size_t slotIndex = ResolveFrameGraphBindingCacheSlot(frameInfo, renderer);
            auto &cacheEntry = bindingCache.slots[slotIndex];
            const auto cacheKey = FrameGraphBindingCacheKey::Capture(frameInfo, renderer, resourceCache);
            if (cacheEntry.valid && cacheEntry.key.Matches(frameInfo, renderer, resourceCache)) {
                return cacheEntry.bindings;
            }

            cacheEntry.key = cacheKey;
            cacheEntry.bindings = BuildFrameGraphResourceBindings(graph, renderer, frameInfo);
            cacheEntry.valid = true;
            return cacheEntry.bindings;
        }

        RenderPipelineContext MakeRenderPipelineContext(Renderer &renderer,
                                                        FrameInfo &frameInfo,
                                                        RenderPipelineBuildMode buildMode) {
            RenderPipelineCallbacks callbacks{};
            callbacks.RecordRasterScene = [this](RenderGraph::RenderGraphPassContext &context) {
                RenderRasterScene(context);
            };
            callbacks.RecordPicking = [this](RenderGraph::RenderGraphPassContext &context) {
                RenderEditorPickingDraws(context);
            };
            callbacks.RecordGizmos = [this](RenderGraph::RenderGraphPassContext &context) {
                RenderGizmoDraws(context);
            };
#ifdef RAY_TRACING
            callbacks.SyncRayTracingScene = [this](RenderGraph::RenderGraphPassContext &context) {
                SyncRayTracingScene(context);
                m_resourceManager->FlushPendingDescriptorRefreshes();
                const bool hasValidRayTracingTlas = m_resourceManager->HasValidRayTracingTlas();
                if (hasValidRayTracingTlas &&
                    !m_dirtyEntityDescSlotsScratch.empty() &&
                    context.frameInfo.pEntityDescBuffer != nullptr &&
                    context.frameInfo.pEntityDescs != nullptr &&
                    !context.frameInfo.pEntityDescs->empty()) {
                    for (const id_t entityDescSlot: m_dirtyEntityDescSlotsScratch) {
                        if (entityDescSlot < 0 || static_cast<size_t>(entityDescSlot) >= context.frameInfo.pEntityDescs->size()) {
                            continue;
                        }
                        context.frameInfo.pEntityDescBuffer->writeToBuffer(
                            &(*context.frameInfo.pEntityDescs)[static_cast<size_t>(entityDescSlot)],
                            sizeof(EntityDesc),
                            sizeof(EntityDesc) * static_cast<VkDeviceSize>(entityDescSlot));
                        context.frameInfo.pEntityDescBuffer->flush(
                            sizeof(EntityDesc),
                            sizeof(EntityDesc) * static_cast<VkDeviceSize>(entityDescSlot));
                    }
                }
                context.blackboard.Set("HasValidRayTracingTlas", hasValidRayTracingTlas);
            };
            callbacks.RecordRayTracing = [this](RenderGraph::RenderGraphPassContext &context) {
                const bool *hasValidTlas = context.blackboard.TryGet<bool>("HasValidRayTracingTlas");
                if (hasValidTlas == nullptr || !*hasValidTlas || m_rayTracingSystem == nullptr) {
                    return;
                }
                m_rayTracingSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                m_rayTracingSystem->Record(context);
            };
            callbacks.RecordRayTracingDenoise = [this](RenderGraph::RenderGraphPassContext &context) {
                const bool *hasValidTlas = context.blackboard.TryGet<bool>("HasValidRayTracingTlas");
                if (hasValidTlas == nullptr || !*hasValidTlas || m_computeSystem == nullptr) {
                    return;
                }
                m_computeSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                m_computeSystem->Record(context);
            };
            callbacks.RecordPost = [this](RenderGraph::RenderGraphPassContext &context) {
                if (m_postSystem == nullptr) {
                    return;
                }
                m_postSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                m_postSystem->Record(context);
                m_lastPresentedSceneImageIndex = context.frameInfo.frameIndex % 2;
            };
            callbacks.RecordLayoutInteractionPost = [this](RenderGraph::RenderGraphPassContext &context) {
                if (m_postSystem == nullptr) {
                    return;
                }
                m_postSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                m_postSystem->RecordWithImageIndex(context, m_lastPresentedSceneImageIndex);
            };
#else
            callbacks.RecordShadow = [this](RenderGraph::RenderGraphPassContext &context) {
                if (m_shadowSystem == nullptr) {
                    return;
                }
                m_shadowSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                m_shadowSystem->Record(context);
            };
#endif

            return {
                frameInfo,
                renderer,
                m_resourceManager->GetRenderGraphResourceCache(),
                std::move(callbacks),
                buildMode};
        }

        RenderGraph::RenderGraph BuildFrameGraphWithPipeline(RenderPipeline &pipeline,
                                                             Renderer &renderer,
                                                             FrameInfo &frameInfo,
                                                             RenderPipelineBuildMode buildMode) {
            RenderGraph::RenderGraph graph{};
            auto context = MakeRenderPipelineContext(renderer, frameInfo, buildMode);
            pipeline.Build(context, graph);
            return graph;
        }

#ifdef RAY_TRACING
        RenderGraph::RenderGraph &EnsureLayoutInteractionFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_layoutInteractionFrameGraphValid || !m_layoutInteractionFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_layoutInteractionFrameGraph = BuildFrameGraphWithPipeline(
                    *m_rayTracingPipeline,
                    renderer,
                    frameInfo,
                    RenderPipelineBuildMode::LayoutInteraction);
                m_layoutInteractionFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_layoutInteractionFrameGraphValid = true;
            }
            return m_layoutInteractionFrameGraph;
        }

        RenderGraph::RenderGraph &EnsureRayTracingFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_rayTracingFrameGraphValid || !m_rayTracingFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_rayTracingFrameGraph = BuildFrameGraphWithPipeline(
                    *m_rayTracingPipeline,
                    renderer,
                    frameInfo,
                    RenderPipelineBuildMode::Main);
                m_rayTracingFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_rayTracingFrameGraphValid = true;
            }
            return m_rayTracingFrameGraph;
        }
#else
        RenderGraph::RenderGraph &EnsureRasterFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_rasterFrameGraphValid || !m_rasterFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_rasterFrameGraph = BuildFrameGraphWithPipeline(
                    *m_rasterPipeline,
                    renderer,
                    frameInfo,
                    RenderPipelineBuildMode::Main);
                m_rasterFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_rasterFrameGraphValid = true;
            }
            return m_rasterFrameGraph;
        }
#endif

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

        void EnsureCachedRenderQueues(const FrameInfo &frameInfo) {
            if (frameInfo.renderScene == nullptr) {
                m_defaultLayerMeshInstancesCache.clear();
                m_sortedRasterRenderQueueCache.clear();
                m_rasterRenderQueueSystemsCache.clear();
                m_renderSceneMeshFilterRevision = 0;
                return;
            }

            const uint64_t meshFilterRevision = frameInfo.renderScene->GetMeshFilterRevision();
            if (m_renderSceneMeshFilterRevision == meshFilterRevision) {
                return;
            }

            m_defaultLayerMeshInstancesCache.clear();
            m_sortedRasterRenderQueueCache.clear();
            const size_t visibleCount = static_cast<size_t>(frameInfo.renderScene->GetStats().visibleMeshInstanceCount);
            m_defaultLayerMeshInstancesCache.reserve(std::max(m_defaultLayerMeshInstancesCache.capacity(), visibleCount));
            m_sortedRasterRenderQueueCache.reserve(std::max(m_sortedRasterRenderQueueCache.capacity(), visibleCount));

            for (const auto &meshInstance: frameInfo.renderScene->GetMeshInstances()) {
                if (!meshInstance.IsDefaultLayerRenderable()) {
                    continue;
                }

                m_defaultLayerMeshInstancesCache.push_back(&meshInstance);
                const auto renderSystemIt = m_renderSystemMap.find(meshInstance.materialId);
                if (renderSystemIt == m_renderSystemMap.end() || renderSystemIt->second == nullptr) {
                    continue;
                }
                m_sortedRasterRenderQueueCache.push_back({renderSystemIt->second.get(), &meshInstance});
            }

            std::sort(m_sortedRasterRenderQueueCache.begin(), m_sortedRasterRenderQueueCache.end(), [](const auto &a, const auto &b) {
                return a.meshInstance->renderQueue < b.meshInstance->renderQueue;
            });

            m_rasterRenderQueueSystemsCache.clear();
            m_rasterRenderQueueSystemsCache.reserve(m_renderSystemMap.size());
            m_renderQueueSystemDedupScratch.clear();
            m_renderQueueSystemDedupScratch.reserve(m_renderSystemMap.size());
            for (const auto &item: m_sortedRasterRenderQueueCache) {
                if (item.renderSystem == nullptr) {
                    continue;
                }
                if (m_renderQueueSystemDedupScratch.insert(item.renderSystem).second) {
                    m_rasterRenderQueueSystemsCache.push_back(item.renderSystem);
                }
            }
            m_renderSceneMeshFilterRevision = meshFilterRevision;
        }

        void SanitizeSelection(FrameInfo &frameInfo) {
            auto &selectionService = m_resourceManager->GetEditorSelectionService();
            if (selectionService.HasSelection() &&
                !EntityLifecycle::IsAlive(frameInfo, selectionService.GetSelectedId())) {
                selectionService.ClearSelection();
            }
            frameInfo.selectedEntityId =
                selectionService.HasSelection() ? selectionService.GetSelectedId() : EditorSelectionService::InvalidEntityId;
        }

        const RenderSceneBuilder::MaterialTraitsCache &EnsureRenderSceneMaterialTraitsCache(const FrameInfo &frameInfo,
                                                                                            bool forceRebuild = false) {
            if (forceRebuild ||
                m_renderSceneMaterialTraitsDirty ||
                m_renderSceneMaterialTraitsCacheMaterialCount != frameInfo.materials.size()) {
                m_renderSceneMaterialTraitsCache = RenderSceneBuilder::BuildMaterialTraitsCache(frameInfo);
                m_renderSceneMaterialTraitsCacheMaterialCount = frameInfo.materials.size();
                m_renderSceneMaterialTraitsDirty = false;
            }
            return m_renderSceneMaterialTraitsCache;
        }

        void BuildRenderScene(FrameInfo &frameInfo, Renderer &renderer) {
            const bool panelRectChanged = !m_renderSceneBuilt ||
                                          !SameViewportRect(m_cachedScenePanelRect, frameInfo.scenePanelRect) ||
                                          !SameViewportRect(m_cachedSceneViewportRect, frameInfo.sceneViewportRect);
            const bool extentChanged = !m_renderSceneBuilt ||
                                       m_cachedSceneRenderExtent.width != frameInfo.sceneRenderExtent.width ||
                                       m_cachedSceneRenderExtent.height != frameInfo.sceneRenderExtent.height;
            auto renderSceneInvalidation = m_resourceManager->ConsumeRenderSceneInvalidation();
            auto dirtyTransformEntities = m_resourceManager->GetTransformService().ConsumeResolvedDirtyEntities();
            const bool hasIncrementalUpdates =
                renderSceneInvalidation.cameraDirty ||
                !renderSceneInvalidation.events.empty() ||
                !dirtyTransformEntities.empty();

            if (m_renderSceneBuilt &&
                !extentChanged &&
                !renderSceneInvalidation.fullRebuild &&
                !panelRectChanged &&
                !hasIncrementalUpdates) {
                frameInfo.renderScene = &m_renderScene;
                return;
            }

            if (!m_renderSceneBuilt || extentChanged || renderSceneInvalidation.fullRebuild) {
                const auto &materialTraitsCache = EnsureRenderSceneMaterialTraitsCache(frameInfo, true);
                m_renderScene = RenderSceneBuilder::Build(frameInfo, renderer, &materialTraitsCache);
                m_renderSceneBuilt = true;
#ifdef RAY_TRACING
                if (!m_rayTracingSyncInitialized || renderSceneInvalidation.fullRebuild) {
                    m_rayTracingFullSyncRequired = true;
                    m_rayTracingDirtyEntities.clear();
                    m_rayTracingRemovedEntities.clear();
                }
#endif
                m_renderSceneMeshFilterRevision = 0;
                m_defaultLayerMeshInstancesCache.clear();
                m_sortedRasterRenderQueueCache.clear();
                m_rasterRenderQueueSystemsCache.clear();
            } else {
                if (panelRectChanged) {
                    m_renderScene.SetView(RenderSceneBuilder::BuildView(frameInfo));
                }

                bool cameraDirty = renderSceneInvalidation.cameraDirty;
                auto &sceneEntityScratch = m_renderSceneEntityScratch;
                sceneEntityScratch.clear();
                sceneEntityScratch.reserve(
                    dirtyTransformEntities.size() +
                    renderSceneInvalidation.events.size());

                using RenderSceneEventType = ResourceManager::RenderSceneInvalidation::EventType;
                auto processTransformEvent = [&](const id_t entityId) {
                    if (frameInfo.sceneRegistry != nullptr) {
                        if (frameInfo.sceneRegistry->HasComponent<CameraComponent>(entityId)) {
                            cameraDirty = true;
                        }
                        if (frameInfo.sceneRegistry->HasComponent<MeshRendererComponent>(entityId)) {
                            sceneEntityScratch.insert(entityId);
                        }
                        if (frameInfo.sceneRegistry->HasComponent<LightComponent>(entityId)) {
                            PatchRenderSceneLightEntity(frameInfo, entityId);
                            if (frameInfo.sceneRegistry->HasComponent<MeshRendererComponent>(entityId)) {
                                sceneEntityScratch.insert(entityId);
                            }
                        }
                    }
                };

                for (const auto &event: renderSceneInvalidation.events) {
                    switch (event.type) {
                        case RenderSceneEventType::FullRebuild:
                            break;
                        case RenderSceneEventType::CameraChanged:
                            cameraDirty = true;
                            break;
                        case RenderSceneEventType::MeshChanged:
                        case RenderSceneEventType::ComponentAdded:
                        case RenderSceneEventType::ComponentRemoved:
                        case RenderSceneEventType::EntityCreated:
                            sceneEntityScratch.insert(event.entityId);
                            break;
                        case RenderSceneEventType::LightChanged:
                            PatchRenderSceneLightEntity(frameInfo, event.entityId);
                            if (frameInfo.sceneRegistry != nullptr &&
                                frameInfo.sceneRegistry->HasComponent<MeshRendererComponent>(event.entityId)) {
                                sceneEntityScratch.insert(event.entityId);
                            }
                            break;
                        case RenderSceneEventType::EntityDeleted:
                            m_renderScene.RemoveMeshInstance(event.entityId);
                            m_renderScene.RemoveLightInstance(event.entityId);
#ifdef RAY_TRACING
                            QueueRemovedRayTracingEntity(event.entityId);
#endif
                            cameraDirty = true;
                            break;
                        case RenderSceneEventType::TransformChanged:
                            processTransformEvent(event.entityId);
                            break;
                    }
                }

                for (const id_t entityId: dirtyTransformEntities) {
                    processTransformEvent(entityId);
                }

                for (const id_t entityId: sceneEntityScratch) {
                    PatchRenderSceneMeshEntity(frameInfo, entityId);
#ifdef RAY_TRACING
                    QueueDirtyRayTracingEntity(entityId);
#endif
                }

                if (cameraDirty) {
                    m_renderScene.SetCamera(RenderSceneBuilder::BuildCameraData(frameInfo, renderer));
                }
            }

            m_cachedScenePanelRect = frameInfo.scenePanelRect;
            m_cachedSceneViewportRect = frameInfo.sceneViewportRect;
            m_cachedSceneRenderExtent = frameInfo.sceneRenderExtent;
            frameInfo.renderScene = &m_renderScene;
        }

        void PatchRenderSceneMeshEntity(const FrameInfo &frameInfo, id_t entityId) {
            auto *materialTraitsCache = &EnsureRenderSceneMaterialTraitsCache(frameInfo);
            if (frameInfo.sceneRegistry != nullptr) {
                MeshRendererComponent *meshRenderer = nullptr;
                if (frameInfo.sceneRegistry->TryGetComponent(entityId, meshRenderer) &&
                    meshRenderer != nullptr &&
                    materialTraitsCache->find(meshRenderer->GetMaterialID()) == materialTraitsCache->end()) {
                    m_renderSceneMaterialTraitsDirty = true;
                    materialTraitsCache = &EnsureRenderSceneMaterialTraitsCache(frameInfo, true);
                }
            }

            if (const auto meshInstance = RenderSceneBuilder::BuildMeshInstance(frameInfo, entityId, *materialTraitsCache); meshInstance.has_value()) {
                m_renderScene.UpdateMeshInstance(*meshInstance);
            } else {
                m_renderScene.RemoveMeshInstance(entityId);
            }
        }

        void PatchRenderSceneLightEntity(const FrameInfo &frameInfo, id_t entityId) {
            if (const auto lightInstance = RenderSceneBuilder::BuildLightInstance(frameInfo, entityId); lightInstance.has_value()) {
                m_renderScene.UpdateLightInstance(*lightInstance);
            } else {
                m_renderScene.RemoveLightInstance(entityId);
            }
        }

        static bool SameViewportRect(const ViewportRect &a, const ViewportRect &b) {
            return a.x == b.x &&
                   a.y == b.y &&
                   a.width == b.width &&
                   a.height == b.height;
        }

#ifdef RAY_TRACING
        static bool SamePbr(const PBR &lhs, const PBR &rhs) {
            return lhs.albedo == rhs.albedo &&
                   lhs.normal == rhs.normal &&
                   lhs.metallic == rhs.metallic &&
                   lhs.roughness == rhs.roughness &&
                   lhs.opacity == rhs.opacity &&
                   lhs.AO == rhs.AO &&
                   lhs.emissive == rhs.emissive;
        }

        static bool SameOptionalPbr(const std::optional<PBR> &lhs, const std::optional<PBR> &rhs) {
            if (lhs.has_value() != rhs.has_value()) {
                return false;
            }
            return !lhs.has_value() || SamePbr(*lhs, *rhs);
        }

        void EnsureEntityDescCapacity(std::vector<EntityDesc> &entityDescs, size_t requiredSize) {
            if (entityDescs.size() >= requiredSize) {
                return;
            }

            size_t newSize = std::max<size_t>(entityDescs.size(), 1);
            while (newSize < requiredSize) {
                newSize *= 2;
            }
            entityDescs.resize(newSize);
        }
#endif

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
                    m_resourceManager->GetRenderGraphResourceCache().GetPickingPipelineTarget(),
                    m_editorPickingMaterial,
                    m_resourceManager->GetRenderCore().GetPipelineLibrary());
        }

        void RenderEditorPickingDraws(RenderGraph::RenderGraphPassContext &context) {
            auto &frameInfo = context.frameInfo;
            if (m_editorPickingRenderSystem == nullptr || frameInfo.renderScene == nullptr) {
                return;
            }

            EnsureCachedRenderQueues(frameInfo);
            m_editorPickingRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
            for (const auto *meshInstance: m_defaultLayerMeshInstancesCache) {
                if (meshInstance == nullptr) {
                    continue;
                }
                m_editorPickingRenderSystem->Record(context, *meshInstance);
            }
        }

        void RenderRasterScene(RenderGraph::RenderGraphPassContext &context) {
            auto &frameInfo = context.frameInfo;
            if (frameInfo.renderScene == nullptr) {
                return;
            }

            EnsureCachedRenderQueues(frameInfo);
            for (auto *renderSystem: m_rasterRenderQueueSystemsCache) {
                if (renderSystem != nullptr) {
                    renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
                }
            }
            for (const auto &item: m_sortedRasterRenderQueueCache) {
                if (item.renderSystem == nullptr || item.meshInstance == nullptr) {
                    continue;
                }
                item.renderSystem->Record(context, *item.meshInstance);
            }
        }
#ifdef RAY_TRACING
        void SyncRayTracingScene(RenderGraph::RenderGraphPassContext &context) {
            auto &frameInfo = context.frameInfo;
            if (frameInfo.renderScene == nullptr || frameInfo.pEntityDescs == nullptr) {
                return;
            }

            auto &entityDescs = *frameInfo.pEntityDescs;
            auto &rayTracingSceneContext = m_resourceManager->GetRayTracingSceneContext();
            auto &renderResourceRegistry = m_resourceManager->GetRenderCore().GetResourceRegistry();
            rayTracingSceneContext.BeginFrameTlasStats();
            rayTracingSceneContext.ProcessDeferredDestroy();
            m_dirtyEntityDescSlotsScratch.clear();
            m_lastRayTracingSyncStats = {};
            bool tlasHandleChanged = false;
            bool entityDescsChanged = false;
            bool instanceStructureChanged = false;
            bool transformOrMaskChanged = false;
            auto markEntityDescDirty = [&](const id_t entityDescSlot) {
                if (entityDescSlot < 0) {
                    return;
                }
                m_dirtyEntityDescSlotsScratch.push_back(entityDescSlot);
                entityDescsChanged = true;
            };

            auto retireEntity = [&](const id_t entityId) {
                const auto tlasIt = m_entityToTlasId.find(entityId);
                if (tlasIt == m_entityToTlasId.end()) {
                    m_rayTracingEntityStateCache.erase(entityId);
                    return;
                }

                const id_t staleTlasId = tlasIt->second;
                if (rayTracingSceneContext.RetireInstance(staleTlasId)) {
                    transformOrMaskChanged = true;
                }
                if (staleTlasId >= 0 && static_cast<size_t>(staleTlasId) < entityDescs.size()) {
                    entityDescs[static_cast<size_t>(staleTlasId)] = EntityDesc{};
                    markEntityDescDirty(staleTlasId);
                }
                m_rayTracingEntityStateCache.erase(entityId);
                m_entityToTlasId.erase(tlasIt);
                frameInfo.sceneUpdated = true;
                ++m_lastRayTracingSyncStats.removedEntityCount;
            };

            auto processMeshInstance = [&](const RenderMeshInstance &meshInstance, const std::shared_ptr<Model> &model) {
                if (model == nullptr) {
                    return;
                }

                const bool isActive = meshInstance.active && meshInstance.visible;
                const uint32_t instanceMask = isActive ? (1u << std::min(meshInstance.renderLayer, 7u)) : 0x00;
                const glm::mat4 currentTransform = meshInstance.worldTransform;
                auto [tlasEntry, inserted] = m_entityToTlasId.try_emplace(meshInstance.entityId, meshInstance.rayTracingInstanceId);
                (void)inserted;
                id_t &tlasId = tlasEntry->second;
                if (tlasId == std::numeric_limits<id_t>::max()) {
                    tlasId = rayTracingSceneContext.AllocateInstanceId();
                }

                if (static_cast<size_t>(tlasId) >= static_cast<size_t>(RuntimeEntityDescCapacity)) {
                    return;
                }

                EnsureEntityDescCapacity(entityDescs, static_cast<size_t>(tlasId) + 1);

                EntityDesc &entityDesc = entityDescs[tlasId];
                const int32_t renderOptions =
                    (meshInstance.castShadow ? EntityRenderOptionCastShadow : 0) |
                    (meshInstance.receiveShadow ? EntityRenderOptionReceiveShadow : 0);
                const int32_t renderLayer = static_cast<int32_t>(std::min(meshInstance.renderLayer, 7u));
                auto [stateEntry, insertedState] = m_rayTracingEntityStateCache.try_emplace(meshInstance.entityId);
                const bool hasCachedState = !insertedState;
                RayTracingEntityState &cachedState = stateEntry->second;
                const bool meshResourceChanged =
                    !hasCachedState || cachedState.meshResource != meshInstance.meshResource;
                const bool entityDescDirty =
                    !hasCachedState ||
                    cachedState.meshResource != meshInstance.meshResource ||
                    cachedState.materialId != meshInstance.materialId ||
                    cachedState.renderOptions != renderOptions ||
                    cachedState.renderLayer != renderLayer ||
                    !SameOptionalPbr(cachedState.pbrOverride, meshInstance.pbrOverride);

                if (entityDescDirty) {
                    EntityDesc nextEntityDesc{};
                    EntityDesc baseEntityDesc{};
                    const bool hasBaseEntityDesc =
                        m_resourceManager->TryGetRayTracingMaterialDesc(meshInstance.materialId, baseEntityDesc);
                    if (hasBaseEntityDesc) {
                        nextEntityDesc = baseEntityDesc;
                    } else if (entityDesc.vertexBufferAddress == 0 || entityDesc.indexBufferAddress == 0) {
                        nextEntityDesc.textureEntry = glm::ivec2{0, 0};
                        nextEntityDesc.pbr.albedo = glm::vec3{0.8f, 0.2f, 0.2f};
                        nextEntityDesc.pbr.normal = glm::vec3{0.0f};
                        nextEntityDesc.pbr.metallic = 0.0f;
                        nextEntityDesc.pbr.roughness = 1.0f;
                        nextEntityDesc.pbr.opacity = 1.0f;
                        nextEntityDesc.pbr.AO = 1.0f;
                        nextEntityDesc.pbr.emissive = glm::vec3{0.0f};
                    } else {
                        nextEntityDesc = entityDesc;
                    }

                    nextEntityDesc.vertexBufferAddress = model->getVertexBuffer()->getDeviceAddress();
                    nextEntityDesc.indexBufferAddress = model->getIndexBuffer()->getDeviceAddress();
                    if (meshInstance.pbrOverride.has_value()) {
                        nextEntityDesc.pbr = *meshInstance.pbrOverride;
                    }
                    nextEntityDesc.renderOptions = renderOptions;
                    nextEntityDesc.renderLayer = renderLayer;
                    entityDesc = nextEntityDesc;
                    markEntityDescDirty(tlasId);
                }

                if (!rayTracingSceneContext.HasBlas(model)) {
                    rayTracingSceneContext.EnsureBlasBuilt(
                        context.frameInfo.commandBuffer,
                        model);
                }

                const bool transformChanged =
                    !cachedState.instanceStateInitialized || cachedState.worldTransform != currentTransform;
                const bool maskChanged =
                    !cachedState.instanceStateInitialized || cachedState.instanceMask != instanceMask;
                const uint32_t shaderOffset = m_resourceManager->GetRayTracingShaderOffset(meshInstance.materialId);

                if (!rayTracingSceneContext.HasInstance(tlasId)) {
                    rayTracingSceneContext.CreateInstance(
                        *model,
                        tlasId,
                        static_cast<id_t>(shaderOffset),
                        currentTransform,
                        instanceMask);
                    instanceStructureChanged = true;
                    frameInfo.sceneUpdated = true;
                } else if (meshResourceChanged) {
                    if (rayTracingSceneContext.UpdateInstanceGeometry(
                            *model,
                            tlasId,
                            static_cast<id_t>(shaderOffset),
                            currentTransform,
                            instanceMask)) {
                        instanceStructureChanged = true;
                        frameInfo.sceneUpdated = true;
                    }
                } else if (maskChanged || transformChanged) {
                    if (rayTracingSceneContext.UpdateInstance(tlasId, currentTransform, instanceMask)) {
                        transformOrMaskChanged = true;
                        frameInfo.sceneUpdated = true;
                    }
                }

                cachedState.meshResource = meshInstance.meshResource;
                cachedState.materialId = meshInstance.materialId;
                cachedState.pbrOverride = meshInstance.pbrOverride;
                cachedState.renderOptions = renderOptions;
                cachedState.renderLayer = renderLayer;
                cachedState.worldTransform = currentTransform;
                cachedState.instanceMask = instanceMask;
                cachedState.instanceStateInitialized = true;
                ++m_lastRayTracingSyncStats.processedEntityCount;
            };

            if (!m_rayTracingSyncInitialized || m_rayTracingFullSyncRequired) {
                m_lastRayTracingSyncStats.fullSync = true;
                m_renderSceneEntityScratch.clear();
                m_renderSceneEntityScratch.reserve(frameInfo.renderScene->GetStats().visibleMeshInstanceCount);
                ForEachRayTracingMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                    m_renderSceneEntityScratch.insert(meshInstance.entityId);
                    const auto *meshResource = renderResourceRegistry.GetMesh(meshInstance.meshResource);
                    if (meshResource == nullptr || meshResource->legacyModel == nullptr) {
                        retireEntity(meshInstance.entityId);
                        return;
                    }
                    processMeshInstance(meshInstance, meshResource->legacyModel);
                });

                m_removedRayTracingEntityScratch.clear();
                m_removedRayTracingEntityScratch.reserve(m_entityToTlasId.size());
                for (const auto &[entityId, tlasId]: m_entityToTlasId) {
                    (void) tlasId;
                    if (m_renderSceneEntityScratch.find(entityId) == m_renderSceneEntityScratch.end()) {
                        m_removedRayTracingEntityScratch.push_back(entityId);
                    }
                }
                for (const id_t entityId: m_removedRayTracingEntityScratch) {
                    retireEntity(entityId);
                }

                m_rayTracingDirtyEntities.clear();
                m_rayTracingRemovedEntities.clear();
                m_rayTracingFullSyncRequired = false;
                m_rayTracingSyncInitialized = true;
            } else {
                m_lastRayTracingSyncStats.fullSync = false;
                for (const id_t entityId: m_rayTracingRemovedEntities) {
                    retireEntity(entityId);
                }

                for (const id_t entityId: m_rayTracingDirtyEntities) {
                    const auto *meshInstance = frameInfo.renderScene->FindMeshInstance(entityId);
                    if (meshInstance == nullptr || !meshInstance->IsRayTracingRenderable()) {
                        retireEntity(entityId);
                        continue;
                    }

                    const auto *meshResource = renderResourceRegistry.GetMesh(meshInstance->meshResource);
                    if (meshResource == nullptr || meshResource->legacyModel == nullptr) {
                        retireEntity(entityId);
                        continue;
                    }
                    processMeshInstance(*meshInstance, meshResource->legacyModel);
                }

                m_rayTracingDirtyEntities.clear();
                m_rayTracingRemovedEntities.clear();
            }

            const bool needsRebuild =
                instanceStructureChanged ||
                rayTracingSceneContext.DidInstanceStructureChange() ||
                rayTracingSceneContext.RequiresRebuild();
            const bool needsUpdate =
                !needsRebuild &&
                (transformOrMaskChanged ||
                 rayTracingSceneContext.DidTransformOrMaskChange() ||
                 rayTracingSceneContext.ShouldUpdate());

            if (needsRebuild) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(
                    context.frameInfo.commandBuffer,
                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                    false);
            } else if (needsUpdate) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(
                    context.frameInfo.commandBuffer,
                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                    true);
            }
            rayTracingSceneContext.ClearBuildFlags();

            if (tlasHandleChanged) {
                m_resourceManager->MarkRayTracingTlasDescriptorDirty();
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
        LightSystem m_lightSystem;
        RenderScene m_renderScene{};
        RenderSceneBuilder::MaterialTraitsCache m_renderSceneMaterialTraitsCache{};
        size_t m_renderSceneMaterialTraitsCacheMaterialCount{0};
        bool m_renderSceneMaterialTraitsDirty{true};
        uint64_t m_renderSceneMeshFilterRevision{0};
        std::vector<const RenderMeshInstance *> m_defaultLayerMeshInstancesCache{};
        std::vector<RasterRenderQueueItem> m_sortedRasterRenderQueueCache{};
        std::vector<RenderSystem *> m_rasterRenderQueueSystemsCache{};
        std::unordered_set<RenderSystem *> m_renderQueueSystemDedupScratch{};
        std::unordered_set<id_t> m_renderSceneEntityScratch{};
        FrameGraphBindingCacheBucket m_layoutInteractionBindingCache{};
        FrameGraphBindingCacheBucket m_rayTracingBindingCache{};
        FrameGraphBindingCacheBucket m_rasterBindingCache{};
        ViewportRect m_cachedScenePanelRect{};
        ViewportRect m_cachedSceneViewportRect{};
        VkExtent2D m_cachedSceneRenderExtent{};
        bool m_renderSceneBuilt{false};

#ifdef RAY_TRACING
        std::unique_ptr<RenderPipeline> m_rayTracingPipeline{};
        std::unique_ptr<RenderPipeline> m_hybridPipeline{};
        std::shared_ptr<RayTracingSystem> m_rayTracingSystem;
        RenderGraph::RenderGraph m_layoutInteractionFrameGraph{};
        RenderGraph::RenderGraph m_rayTracingFrameGraph{};
        FrameGraphCacheKey m_layoutInteractionFrameGraphKey{};
        FrameGraphCacheKey m_rayTracingFrameGraphKey{};
        bool m_layoutInteractionFrameGraphValid{false};
        bool m_rayTracingFrameGraphValid{false};
        std::unordered_map<id_t, RayTracingEntityState> m_rayTracingEntityStateCache{};
        std::unordered_map<id_t, id_t> m_entityToTlasId{};
        std::unordered_set<id_t> m_rayTracingDirtyEntities{};
        std::unordered_set<id_t> m_rayTracingRemovedEntities{};
        std::vector<id_t> m_removedRayTracingEntityScratch{};
        std::vector<id_t> m_dirtyEntityDescSlotsScratch{};
        bool m_rayTracingSyncInitialized{false};
        bool m_rayTracingFullSyncRequired{true};
        RayTracingSyncStats m_lastRayTracingSyncStats{};
        int m_lastPresentedSceneImageIndex = 0;
#else
        std::unique_ptr<RenderPipeline> m_rasterPipeline{};
        std::shared_ptr<ShadowSystem> m_shadowSystem;
        RenderGraph::RenderGraph m_rasterFrameGraph{};
        FrameGraphCacheKey m_rasterFrameGraphKey{};
        bool m_rasterFrameGraphValid{false};
#endif
    };
}

