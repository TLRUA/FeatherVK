#pragma once

#include "../GUI.hpp"
#include "../Renderer.h"
#include "RenderGraph.hpp"
#include "RenderGraphResourceCache.hpp"

namespace FeatherVK::RenderGraph {
    class RenderGraphExecutor {
    public:
        void Execute(
            RenderGraph &graph,
            Renderer &renderer,
            FrameInfo &frameInfo,
            const RenderGraphResourceBindings &bindings,
            RenderGraphResourceCache &resourceCache) {
            auto &blackboard = graph.GetBlackboard();
            resourceCache.ResetPickingData();

            if (!graph.IsCompiled()) {
                graph.Compile();
            }

            for (const auto &compiledPass: graph.GetCompiledPasses()) {
                ApplyBarriers(
                    graph,
                    bindings,
                    frameInfo.commandBuffer,
                    compiledPass.sourcePassIndex,
                    RenderGraphBarrier::Timing::BeforePass);

                auto &pass = const_cast<RenderGraphPass &>(graph.GetPasses()[compiledPass.sourcePassIndex]);
                GraphicsPassRuntime graphicsRuntime{};
                if (pass.graphicsPass.has_value()) {
                    graphicsRuntime = BeginGraphicsPass(renderer, frameInfo, resourceCache, *pass.graphicsPass);
                }

                RenderGraphPassContext context{
                    frameInfo,
                    renderer,
                    blackboard,
                    pass,
                    compiledPass,
                    pass.graphicsPass.has_value() ? &*pass.graphicsPass : nullptr,
                    graphicsRuntime.renderPass,
                    graphicsRuntime.framebuffer,
                    graphicsRuntime.renderExtent};
                pass.execute(context);

                if (pass.graphicsPass.has_value()) {
                    EndGraphicsPass(renderer, frameInfo, resourceCache, *pass.graphicsPass, graphicsRuntime);
                }

                ApplyBarriers(
                    graph,
                    bindings,
                    frameInfo.commandBuffer,
                    compiledPass.sourcePassIndex,
                    RenderGraphBarrier::Timing::AfterPass);
            }
        }

    private:
        struct VulkanState {
            VkPipelineStageFlags stageMask{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
            VkAccessFlags accessMask{0};
            VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        };

        struct GraphicsPassRuntime {
            VkRenderPass renderPass{VK_NULL_HANDLE};
            VkFramebuffer framebuffer{VK_NULL_HANDLE};
            VkExtent2D renderExtent{};
        };

        static VulkanState ToVulkanState(
            RenderGraphResourceState state,
            ResourceAccess access,
            const RenderGraphImageBinding &binding) {
            VulkanState result{};

            switch (state) {
                case RenderGraphResourceState::Undefined:
                    result.stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    result.accessMask = 0;
                    result.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    break;
                case RenderGraphResourceState::ShaderRead:
                    result.stageMask = ShaderStages();
                    result.accessMask = VK_ACCESS_SHADER_READ_BIT;
                    result.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    break;
                case RenderGraphResourceState::ShaderWrite:
                    result.stageMask = ShaderStages();
                    result.accessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_GENERAL;
                    break;
                case RenderGraphResourceState::ShaderReadWrite:
                    result.stageMask = ShaderStages();
                    result.accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_GENERAL;
                    break;
                case RenderGraphResourceState::ColorAttachmentWrite:
                    result.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    result.accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    break;
                case RenderGraphResourceState::DepthStencilWrite:
                    result.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    result.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    break;
                case RenderGraphResourceState::TransferRead:
                    result.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    result.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    result.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    break;
                case RenderGraphResourceState::TransferWrite:
                    result.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    result.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    break;
                case RenderGraphResourceState::Present:
                    result.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    result.accessMask = access == ResourceAccess::Read ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    result.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    break;
                case RenderGraphResourceState::Unknown:
                case RenderGraphResourceState::AccelerationStructureRead:
                case RenderGraphResourceState::AccelerationStructureWrite:
                case RenderGraphResourceState::HostWrite:
                    result.stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    result.accessMask = 0;
                    result.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    break;
            }

            const auto overrideEntry = binding.layoutOverrides.find(state);
            if (overrideEntry != binding.layoutOverrides.end()) {
                result.layout = overrideEntry->second;
            }

            return result;
        }

        static VkPipelineStageFlags ShaderStages() {
            VkPipelineStageFlags stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef RAY_TRACING
            stages |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
#endif
            return stages;
        }

        static void ApplyBarriers(
            const RenderGraph &graph,
            const RenderGraphResourceBindings &bindings,
            VkCommandBuffer commandBuffer,
            size_t passIndex,
            RenderGraphBarrier::Timing timing) {
            const auto &barrierBucket = graph.GetBarrierBucket(passIndex, timing);
            const auto &transitionPlan = graph.GetTransitionPlan();
            for (const size_t barrierIndex: barrierBucket) {
                const auto &barrier = transitionPlan[barrierIndex];

                if (barrier.handle.type != ResourceType::Texture ||
                    barrier.oldState == RenderGraphResourceState::Unknown ||
                    barrier.oldState == RenderGraphResourceState::Undefined ||
                    barrier.newState == RenderGraphResourceState::Unknown) {
                    continue;
                }

                const auto *binding = bindings.FindImage(barrier.handle);
                if (binding == nullptr || !binding->enableBarriers || binding->image == VK_NULL_HANDLE) {
                    continue;
                }

                const VulkanState oldState = ToVulkanState(barrier.oldState, barrier.access, *binding);
                const VulkanState newState = ToVulkanState(barrier.newState, barrier.access, *binding);

                VkImageMemoryBarrier vkBarrier{};
                vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                vkBarrier.srcAccessMask = oldState.accessMask;
                vkBarrier.dstAccessMask = newState.accessMask;
                vkBarrier.oldLayout = oldState.layout;
                vkBarrier.newLayout = newState.layout;
                vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkBarrier.image = binding->image;
                vkBarrier.subresourceRange = binding->subresourceRange;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    oldState.stageMask,
                    newState.stageMask,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &vkBarrier);
            }
        }

        static GraphicsPassRuntime BeginGraphicsPass(
            Renderer &renderer,
            FrameInfo &frameInfo,
            RenderGraphResourceCache &resourceCache,
            const RenderGraphGraphicsPassDesc &graphicsPass) {
            GraphicsPassRuntime runtime{};
            switch (graphicsPass.target) {
                case GraphicsPassTargetKind::SceneColor:
                    runtime.renderPass = resourceCache.GetSceneColorRenderPass();
                    runtime.framebuffer = resourceCache.GetSceneColorFramebuffer(
                        static_cast<uint32_t>(frameInfo.frameIndex % SwapChain::MAX_FRAMES_IN_FLIGHT));
                    runtime.renderExtent = graphicsPass.extent;
                    break;
                case GraphicsPassTargetKind::Shadow:
                    runtime.renderPass = resourceCache.GetShadowRenderPass();
                    runtime.framebuffer = resourceCache.GetShadowFramebuffer();
                    runtime.renderExtent = graphicsPass.extent;
                    break;
                case GraphicsPassTargetKind::Picking:
                    runtime.renderPass = resourceCache.GetPickingRenderPass();
                    runtime.framebuffer = resourceCache.GetPickingFramebuffer();
                    runtime.renderExtent = graphicsPass.extent;
                    break;
                case GraphicsPassTargetKind::Swapchain:
                    runtime.renderPass = renderer.getSwapChainRenderPass();
                    runtime.framebuffer = renderer.getSwapChainFramebuffer(renderer.getCurrentImageIndex());
                    runtime.renderExtent = renderer.getSwapChainExtent();
                    break;
                case GraphicsPassTargetKind::Gizmo:
                    runtime.renderPass = renderer.getGizmosRenderPass();
                    runtime.framebuffer = renderer.getSwapChainFramebuffer(renderer.getCurrentImageIndex());
                    runtime.renderExtent = renderer.getSwapChainExtent();
                    break;
            }

            if (runtime.renderPass == VK_NULL_HANDLE || runtime.framebuffer == VK_NULL_HANDLE) {
                return runtime;
            }

            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = runtime.renderPass;
            renderPassBeginInfo.framebuffer = runtime.framebuffer;
            renderPassBeginInfo.renderArea.offset = {0, 0};
            renderPassBeginInfo.renderArea.extent = runtime.renderExtent;
            renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(graphicsPass.clearValues.size());
            renderPassBeginInfo.pClearValues = graphicsPass.clearValues.empty() ? nullptr : graphicsPass.clearValues.data();
            vkCmdBeginRenderPass(frameInfo.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            VkRect2D scissor{};
            if (graphicsPass.useSceneViewport) {
                viewport.x = frameInfo.sceneViewportRect.x;
                viewport.y = frameInfo.sceneViewportRect.y;
                viewport.width = frameInfo.sceneViewportRect.width;
                viewport.height = frameInfo.sceneViewportRect.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                scissor.offset = {
                    static_cast<int32_t>(std::round(frameInfo.sceneViewportRect.x)),
                    static_cast<int32_t>(std::round(frameInfo.sceneViewportRect.y))};
                scissor.extent = {
                    static_cast<uint32_t>(std::max(1.0f, std::round(frameInfo.sceneViewportRect.width))),
                    static_cast<uint32_t>(std::max(1.0f, std::round(frameInfo.sceneViewportRect.height)))};
            } else {
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(runtime.renderExtent.width);
                viewport.height = static_cast<float>(runtime.renderExtent.height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                scissor.offset = {0, 0};
                scissor.extent = runtime.renderExtent;
            }

            vkCmdSetViewport(frameInfo.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frameInfo.commandBuffer, 0, 1, &scissor);
            return runtime;
        }

        static void EndGraphicsPass(
            Renderer &renderer,
            FrameInfo &frameInfo,
            RenderGraphResourceCache &resourceCache,
            const RenderGraphGraphicsPassDesc &graphicsPass,
            const GraphicsPassRuntime &runtime) {
            if (runtime.renderPass == VK_NULL_HANDLE || runtime.framebuffer == VK_NULL_HANDLE) {
                return;
            }

            if (graphicsPass.renderImGuiAtEnd) {
                GUI::EndFrame(frameInfo.commandBuffer);
            }
            vkCmdEndRenderPass(frameInfo.commandBuffer);

            if (graphicsPass.target == GraphicsPassTargetKind::Picking) {
                resourceCache.MarkPickingDataReady();
            }

            if (graphicsPass.endFrame) {
                renderer.endFrame();
            }
        }
    };
}
