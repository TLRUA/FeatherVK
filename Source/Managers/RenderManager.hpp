#include <algorithm>
#include <cstring>
#include <limits>
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
#include "../RenderGraph/RenderGraphExecutor.hpp"
#include "../RenderCore/FrameData.hpp"
#include "EntityLifecycleUtils.hpp"
#include "ResourceManager.hpp"

namespace FeatherVK {
    namespace {
        constexpr uint32_t RenderGraphShadowMapResolution = 1024;

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
            m_rayTracingEntityDescsDirty = false;
#endif

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
            uint64_t resourceTopologyVersion{0};
            uint32_t frameParity{0};
            uint32_t swapchainImageIndex{std::numeric_limits<uint32_t>::max()};

            [[nodiscard]] bool Matches(const FrameInfo &frameInfo,
                                       const Renderer &renderer,
                                       const RenderGraph::RenderGraphResourceCache &resourceCache) const {
                return resourceTopologyVersion == resourceCache.GetTopologyVersion() &&
                       frameParity == static_cast<uint32_t>(frameInfo.frameIndex & 1u) &&
                       swapchainImageIndex == renderer.getCurrentImageIndex();
            }

            static FrameGraphBindingCacheKey Capture(const FrameInfo &frameInfo,
                                                     const Renderer &renderer,
                                                     const RenderGraph::RenderGraphResourceCache &resourceCache) {
                return {
                    resourceCache.GetTopologyVersion(),
                    static_cast<uint32_t>(frameInfo.frameIndex & 1u),
                    renderer.getCurrentImageIndex()};
            }
        };

        struct CachedFrameGraphBindings {
            FrameGraphBindingCacheKey key{};
            RenderGraph::RenderGraphResourceBindings bindings{};
            bool valid{false};
        };

        void ExecuteFrameGraph(RenderGraph::RenderGraph &graph,
                               Renderer &renderer,
                               FrameInfo &frameInfo,
                               FrameGraphBindingKind bindingKind) {
            auto &blackboard = graph.GetBlackboard();
            blackboard.Set("RenderScene", frameInfo.renderScene);
            blackboard.Set("FrameIndex", frameInfo.frameIndex);
            const auto &bindings = EnsureFrameGraphResourceBindings(bindingKind, renderer, frameInfo);
            m_frameGraphExecutor.Execute(
                graph,
                renderer,
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

        static RenderGraph::RenderGraphTextureDesc ExternalTextureDesc(
            VkExtent2D extent,
            uint32_t usageMask,
            RenderGraph::RenderGraphResourceState initialState = RenderGraph::RenderGraphResourceState::Unknown,
            RenderGraph::RenderGraphResourceState finalState = RenderGraph::RenderGraphResourceState::Unknown) {
            RenderGraph::RenderGraphTextureDesc desc{};
            desc.extent = extent;
            desc.usageMask = usageMask;
            desc.initialState = initialState;
            desc.finalState = finalState;
            return desc;
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
            // Swapchain layout transitions are still owned by the legacy render pass until render pass ownership moves into graph.
            binding.enableBarriers = false;
            return binding;
        }

        RenderGraph::RenderGraphResourceBindings BuildFrameGraphResourceBindings(Renderer &renderer, const FrameInfo &frameInfo) const {
            RenderGraph::RenderGraphResourceBindings bindings{};
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            const uint32_t imageIndex = static_cast<uint32_t>(frameInfo.frameIndex & 1u);
            bindings.ReserveImages(8);

            bindings.BindImage("SceneColor", ImageBinding(
                resourceCache.GetSceneColorImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                ShaderReadOnlyLayoutOverrides()));

            bindings.BindImage("PickingTarget", ImageBinding(
                resourceCache.GetPickingIdImage(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                PickingLayoutOverrides()));

            bindings.BindImage("ShadowMap", ImageBinding(
                resourceCache.GetShadowImage(),
                VK_IMAGE_ASPECT_DEPTH_BIT,
                {
                    {RenderGraph::RenderGraphResourceState::DepthStencilWrite, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                    {RenderGraph::RenderGraphResourceState::ShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                }));

#ifdef RAY_TRACING
            bindings.BindImage("RayTracingOutput", ImageBinding(
                resourceCache.GetRayTracingOutputImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindings.BindImage("WorldPosition", ImageBinding(
                resourceCache.GetWorldPositionImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindings.BindImage("ShadowTerm", ImageBinding(
                resourceCache.GetShadowTermImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindings.BindImage("RayTracingGuide", ImageBinding(
                resourceCache.GetRayTracingGuideImage(static_cast<int>(imageIndex)),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
            bindings.BindImage("DenoiseAccumulation", ImageBinding(
                resourceCache.GetDenoiseAccumulationImage(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                GeneralStorageLayoutOverrides()));
#endif

            bindings.BindImage("Swapchain", SwapchainBinding(renderer.getSwapChainImage(renderer.getCurrentImageIndex())));
            return bindings;
        }

        std::vector<CachedFrameGraphBindings> &GetFrameGraphBindingCaches(FrameGraphBindingKind bindingKind) {
            switch (bindingKind) {
#ifdef RAY_TRACING
                case FrameGraphBindingKind::LayoutInteraction:
                    return m_layoutInteractionBindingCaches;
                case FrameGraphBindingKind::RayTracing:
                    return m_rayTracingBindingCaches;
#else
                case FrameGraphBindingKind::LayoutInteraction:
                    return m_layoutInteractionBindingCaches;
#endif
                case FrameGraphBindingKind::Raster:
                    return m_rasterBindingCaches;
            }

            return m_rasterBindingCaches;
        }

        const RenderGraph::RenderGraphResourceBindings &EnsureFrameGraphResourceBindings(FrameGraphBindingKind bindingKind,
                                                                                         Renderer &renderer,
                                                                                         const FrameInfo &frameInfo) {
            auto &bindingCaches = GetFrameGraphBindingCaches(bindingKind);
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            const auto cacheKey = FrameGraphBindingCacheKey::Capture(frameInfo, renderer, resourceCache);

            for (auto &cacheEntry: bindingCaches) {
                if (cacheEntry.valid && cacheEntry.key.Matches(frameInfo, renderer, resourceCache)) {
                    return cacheEntry.bindings;
                }
            }

            auto &cacheEntry = bindingCaches.emplace_back();
            cacheEntry.key = cacheKey;
            cacheEntry.bindings = BuildFrameGraphResourceBindings(renderer, frameInfo);
            cacheEntry.valid = true;
            return cacheEntry.bindings;
        }

        static RenderGraph::RenderGraphBufferDesc ExternalBufferDesc(
            uint32_t usageMask,
            RenderGraph::RenderGraphResourceState initialState = RenderGraph::RenderGraphResourceState::Unknown,
            RenderGraph::RenderGraphResourceState finalState = RenderGraph::RenderGraphResourceState::Unknown) {
            RenderGraph::RenderGraphBufferDesc desc{};
            desc.usageMask = usageMask;
            desc.initialState = initialState;
            desc.finalState = finalState;
            return desc;
        }

        static RenderGraph::RenderGraphGraphicsPassDesc GraphicsPassDesc(
            RenderGraph::GraphicsPassTargetKind target,
            VkExtent2D extent,
            RenderGraph::RenderGraphGraphicsPassSignature signature,
            std::vector<VkClearValue> clearValues,
            bool useSceneViewport = false,
            bool renderImGuiAtEnd = false,
            bool endFrame = false) {
            RenderGraph::RenderGraphGraphicsPassDesc desc{};
            desc.target = target;
            desc.extent = extent;
            desc.signature = std::move(signature);
            desc.clearValues = std::move(clearValues);
            desc.useSceneViewport = useSceneViewport;
            desc.renderImGuiAtEnd = renderImGuiAtEnd;
            desc.endFrame = endFrame;
            return desc;
        }

        static VkClearValue ColorClear(float r, float g, float b, float a) {
            VkClearValue value{};
            value.color.float32[0] = r;
            value.color.float32[1] = g;
            value.color.float32[2] = b;
            value.color.float32[3] = a;
            return value;
        }

        static VkClearValue DepthClear(float depth = 1.0f, uint32_t stencil = 0u) {
            VkClearValue value{};
            value.depthStencil.depth = depth;
            value.depthStencil.stencil = stencil;
            return value;
        }

#ifdef RAY_TRACING
        RenderGraph::RenderGraph &EnsureLayoutInteractionFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_layoutInteractionFrameGraphValid || !m_layoutInteractionFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_layoutInteractionFrameGraph = BuildLayoutInteractionFrameGraph(renderer, frameInfo, frameInfo.frameIndex);
                m_layoutInteractionFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_layoutInteractionFrameGraphValid = true;
            }
            return m_layoutInteractionFrameGraph;
        }

        RenderGraph::RenderGraph &EnsureRayTracingFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_rayTracingFrameGraphValid || !m_rayTracingFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_rayTracingFrameGraph = BuildRayTracingFrameGraph(renderer, frameInfo, frameInfo.frameIndex);
                m_rayTracingFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_rayTracingFrameGraphValid = true;
            }
            return m_rayTracingFrameGraph;
        }

        RenderGraph::RenderGraph BuildLayoutInteractionFrameGraph(Renderer &renderer, FrameInfo &frameInfo, int frameIndex) {
            (void) renderer;
            (void) frameIndex;
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;

            RenderGraph::RenderGraph graph{};
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            graph.ImportTexture("Swapchain", ExternalTextureDesc(
                frameInfo.extent,
                RGUsage::ColorAttachment | RGUsage::Present,
                RGState::Present,
                RGState::Present));

            graph.AddPass(
                "LayoutInteractionPresentPass",
                RenderGraph::PassType::Present,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Swapchain,
                        {},
                        {},
                        {ColorClear(0.01f, 0.01f, 0.01f, 1.0f), DepthClear()},
                        true,
                        true,
                        true));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    if (m_postSystem == nullptr) {
                        return;
                    }
                    m_postSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                    m_postSystem->RecordWithImageIndex(context, m_lastPresentedSceneImageIndex);
                });

            return graph;
        }

        RenderGraph::RenderGraph BuildRayTracingFrameGraph(Renderer &renderer, FrameInfo &frameInfo, int frameIndex) {
            (void) renderer;
            (void) frameIndex;
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;

            RenderGraph::RenderGraph graph{};
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            graph.DeclareTexture("SceneColor", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::ColorAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("RayTracingOutput", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("WorldPosition", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("ShadowTerm", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("RayTracingGuide", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("DenoiseAccumulation", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderReadWrite));
            graph.DeclareTexture("PickingTarget", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::ColorAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.ImportTexture("Swapchain", ExternalTextureDesc(
                frameInfo.extent,
                RGUsage::ColorAttachment | RGUsage::Present,
                RGState::Present,
                RGState::Present));
            graph.ImportBuffer("TLAS", ExternalBufferDesc(
                RenderGraph::ToUsageMask(RGUsage::AccelerationStructure),
                RGState::Unknown,
                RGState::AccelerationStructureRead));
            graph.ImportBuffer("EntityDesc", ExternalBufferDesc(
                RenderGraph::ToUsageMask(RGUsage::StorageBuffer),
                RGState::Unknown,
                RGState::ShaderRead));

            graph.AddPass(
                "RayTracingSceneSync",
                RenderGraph::PassType::Generic,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteBuffer("TLAS", RenderGraph::RenderGraphResourceState::AccelerationStructureWrite);
                    builder.WriteBuffer("EntityDesc", RenderGraph::RenderGraphResourceState::HostWrite);
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    SyncRayTracingScene(context.frameInfo);
                    m_resourceManager->FlushPendingDescriptorRefreshes();
                    const bool hasValidRayTracingTlas = m_resourceManager->HasValidRayTracingTlas();
                    if (hasValidRayTracingTlas &&
                        m_rayTracingEntityDescsDirty &&
                        context.frameInfo.pEntityDescBuffer != nullptr &&
                        !context.frameInfo.pEntityDescs.empty()) {
                        context.frameInfo.pEntityDescBuffer->writeToBuffer(
                            context.frameInfo.pEntityDescs.data(),
                            context.frameInfo.pEntityDescs.size() * sizeof(EntityDesc));
                    }
                    context.blackboard.Set("HasValidRayTracingTlas", hasValidRayTracingTlas);
                });

            graph.AddPass(
                "RasterSceneColorPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("SceneColor", RenderGraph::RenderGraphResourceState::ColorAttachmentWrite);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::SceneColor,
                        frameInfo.sceneRenderExtent,
                        resourceCache.GetSceneColorPassSignature(),
                        {ColorClear(0.0f, 0.0f, 0.0f, 1.0f), DepthClear()}));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderRasterScene(context);
                });

            graph.AddPass(
                "RayTracingPass",
                RenderGraph::PassType::RayTracing,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadBuffer("TLAS", RenderGraph::RenderGraphResourceState::AccelerationStructureRead);
                    builder.ReadBuffer("EntityDesc", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.WriteTexture("RayTracingOutput", RenderGraph::RenderGraphResourceState::ShaderWrite);
                    builder.WriteTexture("WorldPosition", RenderGraph::RenderGraphResourceState::ShaderWrite);
                    builder.WriteTexture("ShadowTerm", RenderGraph::RenderGraphResourceState::ShaderWrite);
                    builder.WriteTexture("RayTracingGuide", RenderGraph::RenderGraphResourceState::ShaderWrite);
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    const bool *hasValidTlas = context.blackboard.TryGet<bool>("HasValidRayTracingTlas");
                    if (hasValidTlas == nullptr || !*hasValidTlas || m_rayTracingSystem == nullptr) {
                        return;
                    }
                    m_rayTracingSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                    m_rayTracingSystem->Record(context);
                });

            graph.AddPass(
                "RayTracingDenoisePass",
                RenderGraph::PassType::Compute,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("RayTracingOutput", RenderGraph::RenderGraphResourceState::ShaderReadWrite);
                    builder.ReadTexture("WorldPosition", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.ReadTexture("ShadowTerm", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.ReadTexture("RayTracingGuide", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.ReadWriteTexture("DenoiseAccumulation", RenderGraph::RenderGraphResourceState::ShaderReadWrite);
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    const bool *hasValidTlas = context.blackboard.TryGet<bool>("HasValidRayTracingTlas");
                    if (hasValidTlas == nullptr || !*hasValidTlas || m_computeSystem == nullptr) {
                        return;
                    }
                    m_computeSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                    m_computeSystem->Record(context);
                });

            graph.AddPass(
                "PickingPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("PickingTarget", RenderGraph::RenderGraphResourceState::ColorAttachmentWrite);
                    builder.SetForceLive();
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Picking,
                        frameInfo.sceneRenderExtent,
                        resourceCache.GetPickingPassSignature(),
                        {ColorClear(0.0f, 0.0f, 0.0f, 0.0f), DepthClear()}));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderEditorPickingDraws(context);
                });

            graph.AddPass(
                "PostPass",
                RenderGraph::PassType::Present,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadTexture("SceneColor", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.ReadTexture("RayTracingOutput", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.WriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Swapchain,
                        {},
                        {},
                        {ColorClear(0.01f, 0.01f, 0.01f, 1.0f), DepthClear()},
                        true,
                        true,
                        false));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    m_postSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                    m_postSystem->Record(context);
                    m_lastPresentedSceneImageIndex = context.frameInfo.frameIndex % 2;
                });

            graph.AddPass(
                "GizmoPass",
                RenderGraph::PassType::Graphics,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Gizmo,
                        {},
                        {},
                        {ColorClear(0.01f, 0.01f, 0.01f, 1.0f), DepthClear()},
                        true,
                        false,
                        true));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderGizmoDraws(context);
                });

            return graph;
        }
#else
        RenderGraph::RenderGraph &EnsureRasterFrameGraph(Renderer &renderer, FrameInfo &frameInfo) {
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            if (!m_rasterFrameGraphValid || !m_rasterFrameGraphKey.Matches(frameInfo, resourceCache)) {
                m_rasterFrameGraph = BuildRasterFrameGraph(renderer, frameInfo, frameInfo.frameIndex);
                m_rasterFrameGraphKey = FrameGraphCacheKey::Capture(frameInfo, resourceCache);
                m_rasterFrameGraphValid = true;
            }
            return m_rasterFrameGraph;
        }

        RenderGraph::RenderGraph BuildRasterFrameGraph(Renderer &renderer, FrameInfo &frameInfo, int frameIndex) {
            (void) renderer;
            (void) frameIndex;
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;

            RenderGraph::RenderGraph graph{};
            const auto &resourceCache = m_resourceManager->GetRenderGraphResourceCache();
            graph.DeclareTexture("ShadowMap", ExternalTextureDesc(
                {RenderGraphShadowMapResolution, RenderGraphShadowMapResolution},
                RGUsage::DepthStencilAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("PickingTarget", ExternalTextureDesc(
                frameInfo.sceneRenderExtent,
                RGUsage::ColorAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.ImportTexture("Swapchain", ExternalTextureDesc(
                frameInfo.extent,
                RGUsage::ColorAttachment | RGUsage::Present,
                RGState::Present,
                RGState::Present));

            graph.AddPass(
                "ShadowPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("ShadowMap", RenderGraph::RenderGraphResourceState::DepthStencilWrite);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Shadow,
                        {RenderGraphShadowMapResolution, RenderGraphShadowMapResolution},
                        resourceCache.GetShadowPassSignature(),
                        {DepthClear()}));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    m_shadowSystem->UpdateGlobalUboBuffer(context.frameInfo.globalUbo, context.frameInfo.frameIndex);
                    m_shadowSystem->Record(context);
                });

            graph.AddPass(
                "PickingPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("PickingTarget", RenderGraph::RenderGraphResourceState::ColorAttachmentWrite);
                    builder.SetForceLive();
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Picking,
                        frameInfo.sceneRenderExtent,
                        resourceCache.GetPickingPassSignature(),
                        {ColorClear(0.0f, 0.0f, 0.0f, 0.0f), DepthClear()}));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderEditorPickingDraws(context);
                });

            graph.AddPass(
                "RasterSwapchainPass",
                RenderGraph::PassType::Graphics,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadTexture("ShadowMap", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.WriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::ColorAttachmentWrite);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Swapchain,
                        {},
                        {},
                        {ColorClear(0.01f, 0.01f, 0.01f, 1.0f), DepthClear()},
                        true,
                        true,
                        false));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderRasterScene(context);
                });

            graph.AddPass(
                "GizmoPass",
                RenderGraph::PassType::Graphics,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Gizmo,
                        {},
                        {},
                        {ColorClear(0.01f, 0.01f, 0.01f, 1.0f), DepthClear()},
                        true,
                        false,
                        true));
                },
                [this](RenderGraph::RenderGraphPassContext &context) {
                    RenderGizmoDraws(context);
                });

            return graph;
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

            if (!m_renderSceneBuilt || extentChanged || renderSceneInvalidation.fullRebuild) {
                const auto &materialTraitsCache = EnsureRenderSceneMaterialTraitsCache(frameInfo, true);
                m_renderScene = RenderSceneBuilder::Build(frameInfo, renderer, &materialTraitsCache);
                m_renderSceneBuilt = true;
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
            m_rayTracingEntityDescsDirty = true;
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

            m_editorPickingRenderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);

            ForEachDefaultLayerMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                m_editorPickingRenderSystem->Record(context, meshInstance);
            });
        }

        void RenderRasterScene(RenderGraph::RenderGraphPassContext &context) {
            auto &frameInfo = context.frameInfo;
            if (frameInfo.renderScene == nullptr) {
                return;
            }

            m_renderQueueScratch.clear();
            m_renderQueueScratch.reserve(std::max(
                m_renderQueueScratch.capacity(),
                static_cast<size_t>(frameInfo.renderScene->GetStats().visibleMeshInstanceCount)));
            ForEachDefaultLayerMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                const auto renderSystemIt = m_renderSystemMap.find(meshInstance.materialId);
                if (renderSystemIt == m_renderSystemMap.end() || renderSystemIt->second == nullptr) {
                    return;
                }
                m_renderQueueScratch.push_back({renderSystemIt->second.get(), &meshInstance});
            });

            std::sort(m_renderQueueScratch.begin(), m_renderQueueScratch.end(), [](const auto &a, const auto &b) {
                return a.meshInstance->renderQueue < b.meshInstance->renderQueue;
            });

            m_updatedRenderSystemsScratch.clear();
            m_updatedRenderSystemsScratch.reserve(m_renderSystemMap.size());
            for (auto &item: m_renderQueueScratch) {
                auto *renderSystem = item.renderSystem;
                if (renderSystem == nullptr) {
                    continue;
                }
                if (m_updatedRenderSystemsScratch.insert(renderSystem).second) {
                    renderSystem->UpdateGlobalUboBuffer(frameInfo.globalUbo, frameInfo.frameIndex);
                }
                const RenderMeshInstance &meshInstance = *item.meshInstance;
                renderSystem->Record(context, meshInstance);
            }
        }
#ifdef RAY_TRACING
        void SyncRayTracingScene(FrameInfo &frameInfo) {
            if (frameInfo.renderScene == nullptr) {
                return;
            }

            auto &rayTracingSceneContext = m_resourceManager->GetRayTracingSceneContext();
            auto &renderResourceRegistry = m_resourceManager->GetRenderCore().GetResourceRegistry();
            rayTracingSceneContext.ProcessDeferredDestroy();
            auto &currentRayTracingEntities = m_currentRayTracingEntitiesScratch;
            currentRayTracingEntities.clear();
            currentRayTracingEntities.reserve(static_cast<size_t>(frameInfo.renderScene->GetStats().visibleMeshInstanceCount));
            bool tlasHandleChanged = false;
            bool entityDescsChanged = false;
            bool instanceStructureChanged = false;
            bool transformOrMaskChanged = false;

            auto processMeshInstance = [&](const RenderMeshInstance &meshInstance, const std::shared_ptr<Model> &model) {
                if (model == nullptr) {
                    return;
                }

                const bool isActive = meshInstance.active && meshInstance.visible;
                const uint32_t instanceMask = isActive ? (1u << std::min(meshInstance.renderLayer, 7u)) : 0x00;
                const glm::mat4 currentTransform = meshInstance.worldTransform;
                auto [tlasEntry, inserted] = m_entityToTlasId.try_emplace(meshInstance.entityId, meshInstance.rayTracingInstanceId);
                id_t &tlasId = tlasEntry->second;
                if (inserted && tlasId == std::numeric_limits<id_t>::max()) {
                    tlasId = rayTracingSceneContext.AllocateInstanceId();
                }

                if (static_cast<size_t>(tlasId) >= static_cast<size_t>(RuntimeEntityDescCapacity)) {
                    return;
                }

                EnsureEntityDescCapacity(frameInfo.pEntityDescs, static_cast<size_t>(tlasId) + 1);

                EntityDesc &entityDesc = frameInfo.pEntityDescs[tlasId];
                const int32_t renderOptions =
                    (meshInstance.castShadow ? EntityRenderOptionCastShadow : 0) |
                    (meshInstance.receiveShadow ? EntityRenderOptionReceiveShadow : 0);
                const int32_t renderLayer = static_cast<int32_t>(std::min(meshInstance.renderLayer, 7u));
                auto stateEntry = m_rayTracingEntityStateCache.find(meshInstance.entityId);
                const bool hasCachedState = stateEntry != m_rayTracingEntityStateCache.end();
                RayTracingEntityState &cachedState =
                    hasCachedState ? stateEntry->second : m_rayTracingEntityStateCache[meshInstance.entityId];
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
                    entityDescsChanged = true;
                }

                if (!rayTracingSceneContext.HasBlas(model)) {
                    rayTracingSceneContext.EnsureBlasBuilt(model);
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
            };

            ForEachRayTracingMeshInstance(frameInfo, [&](const RenderMeshInstance &meshInstance) {
                const auto *meshResource = renderResourceRegistry.GetMesh(meshInstance.meshResource);
                if (meshResource == nullptr || meshResource->legacyModel == nullptr) {
                    return;
                }
                currentRayTracingEntities.insert(meshInstance.entityId);
                processMeshInstance(meshInstance, meshResource->legacyModel);
            });

            for (auto it = m_entityToTlasId.begin(); it != m_entityToTlasId.end();) {
                if (currentRayTracingEntities.find(it->first) != currentRayTracingEntities.end()) {
                    ++it;
                    continue;
                }

                const id_t staleTlasId = it->second;
                if (rayTracingSceneContext.RetireInstance(staleTlasId)) {
                    transformOrMaskChanged = true;
                }
                if (staleTlasId >= 0 && static_cast<size_t>(staleTlasId) < frameInfo.pEntityDescs.size()) {
                    frameInfo.pEntityDescs[staleTlasId] = EntityDesc{};
                    entityDescsChanged = true;
                }
                m_rayTracingEntityStateCache.erase(it->first);
                frameInfo.sceneUpdated = true;
                it = m_entityToTlasId.erase(it);
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
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, false);
            } else if (needsUpdate) {
                tlasHandleChanged = rayTracingSceneContext.BuildTopLevel(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, true);
            }
            rayTracingSceneContext.ClearBuildFlags();

            if (tlasHandleChanged) {
                m_resourceManager->MarkRayTracingTlasDescriptorDirty();
            }

            if (entityDescsChanged || m_rayTracingEntityDescsDirty) {
                m_rayTracingEntityDescsDirty = true;
                m_resourceManager->GetEntityDescs() = frameInfo.pEntityDescs;
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
        RenderGraph::RenderGraphExecutor m_frameGraphExecutor{};
        RenderScene m_renderScene{};
        RenderSceneBuilder::MaterialTraitsCache m_renderSceneMaterialTraitsCache{};
        size_t m_renderSceneMaterialTraitsCacheMaterialCount{0};
        bool m_renderSceneMaterialTraitsDirty{true};
        std::vector<RasterRenderQueueItem> m_renderQueueScratch{};
        std::unordered_set<RenderSystem *> m_updatedRenderSystemsScratch{};
        std::unordered_set<id_t> m_renderSceneEntityScratch{};
        std::vector<CachedFrameGraphBindings> m_layoutInteractionBindingCaches{};
        std::vector<CachedFrameGraphBindings> m_rayTracingBindingCaches{};
        std::vector<CachedFrameGraphBindings> m_rasterBindingCaches{};
        ViewportRect m_cachedScenePanelRect{};
        ViewportRect m_cachedSceneViewportRect{};
        VkExtent2D m_cachedSceneRenderExtent{};
        bool m_renderSceneBuilt{false};

#ifdef RAY_TRACING
        std::shared_ptr<RayTracingSystem> m_rayTracingSystem;
        RenderGraph::RenderGraph m_layoutInteractionFrameGraph{};
        RenderGraph::RenderGraph m_rayTracingFrameGraph{};
        FrameGraphCacheKey m_layoutInteractionFrameGraphKey{};
        FrameGraphCacheKey m_rayTracingFrameGraphKey{};
        bool m_layoutInteractionFrameGraphValid{false};
        bool m_rayTracingFrameGraphValid{false};
        std::unordered_set<id_t> m_currentRayTracingEntitiesScratch{};
        std::unordered_map<id_t, RayTracingEntityState> m_rayTracingEntityStateCache{};
        std::unordered_map<id_t, id_t> m_entityToTlasId{};
        bool m_rayTracingEntityDescsDirty{false};
        int m_lastPresentedSceneImageIndex = 0;
#else
        std::shared_ptr<ShadowSystem> m_shadowSystem;
        RenderGraph::RenderGraph m_rasterFrameGraph{};
        FrameGraphCacheKey m_rasterFrameGraphKey{};
        bool m_rasterFrameGraphValid{false};
#endif
    };
}

