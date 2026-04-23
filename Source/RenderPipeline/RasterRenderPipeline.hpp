#pragma once

#include "RenderPipeline.hpp"

namespace FeatherVK {
    class RasterRenderPipeline final : public RenderPipeline {
    public:
        [[nodiscard]] const char *GetName() const override {
            return "Raster";
        }

        void Build(RenderPipelineContext &context, RenderGraph::RenderGraph &graph) override {
            using RGState = RenderGraph::RenderGraphResourceState;
            using RGUsage = RenderGraph::ResourceUsage;
            namespace RPU = RenderPipelineUtils;

            graph.DeclareTexture("ShadowMap", RPU::TextureDesc(
                {RPU::ShadowMapResolution, RPU::ShadowMapResolution},
                RGUsage::DepthStencilAttachment | RGUsage::Sampled,
                RGState::Unknown,
                RGState::ShaderRead));
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

            graph.AddPass(
                "ShadowPass",
                RenderGraph::PassType::Graphics,
                [&](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.WriteTexture("ShadowMap", RGState::DepthStencilWrite);
                    builder.SetGraphicsPass(RPU::GraphicsPassDesc(
                        RenderGraph::GraphicsPassTargetKind::Shadow,
                        {RPU::ShadowMapResolution, RPU::ShadowMapResolution},
                        context.resourceCache.GetShadowPassSignature(),
                        {RPU::DepthClear()}));
                },
                [callbacks = context.callbacks](RenderGraph::RenderGraphPassContext &passContext) {
                    if (callbacks.RecordShadow) {
                        callbacks.RecordShadow(passContext);
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
                "RasterSwapchainPass",
                RenderGraph::PassType::Graphics,
                [](RenderGraph::RenderGraphPassBuilder &builder) {
                    builder.ReadTexture("ShadowMap", RenderGraph::RenderGraphResourceState::ShaderRead);
                    builder.WriteTexture("Swapchain", RenderGraph::RenderGraphResourceState::ColorAttachmentWrite);
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
                    if (callbacks.RecordRasterScene) {
                        callbacks.RecordRasterScene(passContext);
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
