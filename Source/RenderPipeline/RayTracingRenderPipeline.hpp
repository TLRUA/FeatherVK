#pragma once

#include "RenderPipeline.hpp"

namespace FeatherVK {
    class RayTracingRenderPipeline : public RenderPipeline {
    public:
        [[nodiscard]] const char *GetName() const override {
            return "RayTracing";
        }

        void Build(RenderPipelineContext &context, RenderGraph::RenderGraph &graph) override {
            if (context.buildMode == RenderPipelineBuildMode::LayoutInteraction) {
                BuildLayoutInteraction(context, graph);
                return;
            }
            BuildMain(context, graph);
        }

    protected:
        static void BuildLayoutInteraction(RenderPipelineContext &context, RenderGraph::RenderGraph &graph) {
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;
            namespace RPU = RenderPipelineUtils;

            graph.ImportTexture("Swapchain", RPU::TextureDesc(
                context.frameInfo.extent,
                RGUsage::ColorAttachment | RGUsage::Present,
                RGState::Present,
                RGState::Present));

            graph.AddPass(
                "LayoutInteractionPresentPass",
                RenderGraph::PassType::Present,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(RenderPipelineUtils::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Swapchain,
                        {},
                        {},
                        {RenderPipelineUtils::ColorClear(0.01f, 0.01f, 0.01f, 1.0f), RenderPipelineUtils::DepthClear()},
                        true,
                        true,
                        true));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordLayoutInteractionPost) {
                        callbacks.RecordLayoutInteractionPost(passContext);
                    }
                });
        }

        static void BuildMain(RenderPipelineContext &context, RenderGraph::RenderGraph &graph) {
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;
            namespace RPU = RenderPipelineUtils;

            graph.DeclareTexture("SceneColor", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::ColorAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("RayTracingOutput", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("WorldPosition", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("ShadowTerm", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("RayTracingGuide", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.DeclareTexture("DenoiseAccumulation", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::Storage | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderReadWrite));
            graph.DeclareTexture("PickingTarget", RPU::TextureDesc(
                context.frameInfo.sceneRenderExtent,
                RGUsage::ColorAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
            graph.ImportTexture("Swapchain", RPU::TextureDesc(
                context.frameInfo.extent,
                RGUsage::ColorAttachment | RGUsage::Present,
                RGState::Present,
                RGState::Present));
            graph.ImportBuffer("TLAS", RPU::BufferDesc(
                RenderGraph::ToUsageMask(RGUsage::AccelerationStructure),
                RGState::Unknown,
                RGState::AccelerationStructureRead));
            graph.ImportBuffer("EntityDesc", RPU::BufferDesc(
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
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.SyncRayTracingScene) {
                        callbacks.SyncRayTracingScene(passContext);
                    }
                });

            graph.AddPass(
                "RasterSceneColorPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("SceneColor", RGState::ColorAttachmentWrite);
                    builder.SetGraphicsPass(RPU::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::SceneColor,
                        context.frameInfo.sceneRenderExtent,
                        context.resourceCache.GetSceneColorPassSignature(),
                        {RPU::ColorClear(0.0f, 0.0f, 0.0f, 1.0f), RPU::DepthClear()}));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordRasterScene) {
                        callbacks.RecordRasterScene(passContext);
                    }
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
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordRayTracing) {
                        callbacks.RecordRayTracing(passContext);
                    }
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
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordRayTracingDenoise) {
                        callbacks.RecordRayTracingDenoise(passContext);
                    }
                });

            graph.AddPass(
                "PickingPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("PickingTarget", RGState::ColorAttachmentWrite);
                    builder.SetForceLive();
                    builder.SetGraphicsPass(RPU::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Picking,
                        context.frameInfo.sceneRenderExtent,
                        context.resourceCache.GetPickingPassSignature(),
                        {RPU::ColorClear(0.0f, 0.0f, 0.0f, 0.0f), RPU::DepthClear()}));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordPicking) {
                        callbacks.RecordPicking(passContext);
                    }
                });

            graph.AddPass(
                "PostPass",
                RenderGraph::PassType::Present,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadTexture("SceneColor", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.ReadTexture("RayTracingOutput", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.WriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(RenderPipelineUtils::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Swapchain,
                        {},
                        {},
                        {RenderPipelineUtils::ColorClear(0.01f, 0.01f, 0.01f, 1.0f), RenderPipelineUtils::DepthClear()},
                        true,
                        true,
                        false));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordPost) {
                        callbacks.RecordPost(passContext);
                    }
                });

            graph.AddPass(
                "GizmoPass",
                RenderGraph::PassType::Graphics,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadWriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::Present);
                    builder.SetGraphicsPass(RenderPipelineUtils::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Gizmo,
                        {},
                        {},
                        {RenderPipelineUtils::ColorClear(0.01f, 0.01f, 0.01f, 1.0f), RenderPipelineUtils::DepthClear()},
                        true,
                        false,
                        true));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordGizmos) {
                        callbacks.RecordGizmos(passContext);
                    }
                });
        }
    };
}
