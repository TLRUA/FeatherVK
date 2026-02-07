#include "VulkanCommandList.hpp"

#include <stdexcept>

#include "../../Buffer.h"
#include "../../Descriptor.h"
#include "../../Pipeline.hpp"

namespace FeatherVK::RHI {
    namespace {
        VkShaderStageFlags ToVkShaderStageFlags(const ShaderStage stageMask) {
            VkShaderStageFlags result = 0;
            if (HasStage(stageMask, ShaderStage::Vertex)) result |= VK_SHADER_STAGE_VERTEX_BIT;
            if (HasStage(stageMask, ShaderStage::Fragment)) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (HasStage(stageMask, ShaderStage::TessellationControl)) result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            if (HasStage(stageMask, ShaderStage::TessellationEvaluation)) result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            if (HasStage(stageMask, ShaderStage::Geometry)) result |= VK_SHADER_STAGE_GEOMETRY_BIT;
            if (HasStage(stageMask, ShaderStage::Compute)) result |= VK_SHADER_STAGE_COMPUTE_BIT;
            if (HasStage(stageMask, ShaderStage::RayGen)) result |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            if (HasStage(stageMask, ShaderStage::RayClosestHit)) result |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            if (HasStage(stageMask, ShaderStage::RayMiss)) result |= VK_SHADER_STAGE_MISS_BIT_KHR;
            if (HasStage(stageMask, ShaderStage::RayAnyHit)) result |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            return result;
        }

        FeatherVK::Pipeline &RequireVulkanPipeline(RHIPipelineState &pipelineState) {
            auto *pipeline = dynamic_cast<FeatherVK::Pipeline *>(&pipelineState);
            if (pipeline == nullptr) {
                throw std::runtime_error("RHI pipeline is not backed by the Vulkan backend");
            }
            return *pipeline;
        }

        FeatherVK::DescriptorSetHandle &RequireVulkanBindSet(RHIBindSet &bindSet) {
            auto *descriptorSet = dynamic_cast<FeatherVK::DescriptorSetHandle *>(&bindSet);
            if (descriptorSet == nullptr) {
                throw std::runtime_error("RHI bind set is not backed by the Vulkan backend");
            }
            return *descriptorSet;
        }

        FeatherVK::Buffer &RequireVulkanBuffer(RHIBuffer &buffer) {
            auto *vulkanBuffer = dynamic_cast<FeatherVK::Buffer *>(&buffer);
            if (vulkanBuffer == nullptr) {
                throw std::runtime_error("RHI buffer is not backed by the Vulkan backend");
            }
            return *vulkanBuffer;
        }
    }

    void VulkanCommandList::SetViewport(const Viewport &viewport) {
        VkViewport vkViewport{};
        vkViewport.x = viewport.x;
        vkViewport.y = viewport.y;
        vkViewport.width = viewport.width;
        vkViewport.height = viewport.height;
        vkViewport.minDepth = viewport.minDepth;
        vkViewport.maxDepth = viewport.maxDepth;
        vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
    }

    void VulkanCommandList::SetScissor(const ScissorRect &scissor) {
        VkRect2D vkScissor{};
        vkScissor.offset = {scissor.x, scissor.y};
        vkScissor.extent = {scissor.width, scissor.height};
        vkCmdSetScissor(m_commandBuffer, 0, 1, &vkScissor);
    }

    void VulkanCommandList::BindPipeline(RHIPipelineState &pipelineState) {
        auto &pipeline = RequireVulkanPipeline(pipelineState);
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (pipeline.GetDesc().type == PipelineType::Compute) {
            bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        } else if (pipeline.GetDesc().type == PipelineType::RayTracing) {
            bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        }
        pipeline.bind(m_commandBuffer, bindPoint);
    }

    void VulkanCommandList::BindResources(RHIPipelineState &pipelineState,
                                          uint32_t firstSet,
                                          const std::vector<std::shared_ptr<RHIBindSet>> &bindSets) {
        auto &pipeline = RequireVulkanPipeline(pipelineState);
        std::vector<VkDescriptorSet> descriptorSets{};
        descriptorSets.reserve(bindSets.size());
        for (const auto &bindSet: bindSets) {
            descriptorSets.push_back(RequireVulkanBindSet(*bindSet).GetVkDescriptorSet());
        }

        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (pipeline.GetDesc().type == PipelineType::Compute) {
            bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        } else if (pipeline.GetDesc().type == PipelineType::RayTracing) {
            bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        }

        vkCmdBindDescriptorSets(
            m_commandBuffer,
            bindPoint,
            pipeline.getPipelineLayout(),
            firstSet,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr);
    }

    void VulkanCommandList::BindVertexBuffer(uint32_t slot, RHIBuffer &buffer, uint64_t offset) {
        auto &vulkanBuffer = RequireVulkanBuffer(buffer);
        VkBuffer buffers[] = {vulkanBuffer.getBuffer()};
        VkDeviceSize offsets[] = {offset};
        vkCmdBindVertexBuffers(m_commandBuffer, slot, 1, buffers, offsets);
    }

    void VulkanCommandList::BindIndexBuffer(RHIBuffer &buffer, uint64_t offset) {
        auto &vulkanBuffer = RequireVulkanBuffer(buffer);
        vkCmdBindIndexBuffer(m_commandBuffer, vulkanBuffer.getBuffer(), offset, VK_INDEX_TYPE_UINT32);
    }

    void VulkanCommandList::PushConstants(RHIPipelineState &pipelineState,
                                          ShaderStage stageMask,
                                          uint32_t offset,
                                          uint32_t size,
                                          const void *data) {
        auto &pipeline = RequireVulkanPipeline(pipelineState);
        vkCmdPushConstants(m_commandBuffer, pipeline.getPipelineLayout(), ToVkShaderStageFlags(stageMask), offset, size, data);
    }

    void VulkanCommandList::Draw(uint32_t vertexCount,
                                 uint32_t instanceCount,
                                 uint32_t firstVertex,
                                 uint32_t firstInstance) {
        vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanCommandList::DrawIndexed(uint32_t indexCount,
                                        uint32_t instanceCount,
                                        uint32_t firstIndex,
                                        int32_t vertexOffset,
                                        uint32_t firstInstance) {
        vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
    }
}
