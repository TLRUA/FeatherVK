#pragma once

#include "../../Device.hpp"
#include "../RHICommands.hpp"

namespace FeatherVK::RHI {
    class VulkanCommandList final : public RHICommandList {
    public:
        explicit VulkanCommandList(FeatherVK::Device &device)
            : m_device(device) {}

        BackendType GetBackendType() const override {
            return BackendType::Vulkan;
        }

        void SetCommandBuffer(VkCommandBuffer commandBuffer) {
            m_commandBuffer = commandBuffer;
        }

        VkCommandBuffer GetVkCommandBuffer() const {
            return m_commandBuffer;
        }

        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const ScissorRect &scissor) override;
        void BindPipeline(RHIPipelineState &pipelineState) override;
        void BindResources(RHIPipelineState &pipelineState,
                           uint32_t firstSet,
                           const std::vector<std::shared_ptr<RHIBindSet>> &bindSets) override;
        void BindVertexBuffer(uint32_t slot,
                              RHIBuffer &buffer,
                              uint64_t offset = 0) override;
        void BindIndexBuffer(RHIBuffer &buffer,
                             uint64_t offset = 0) override;
        void PushConstants(RHIPipelineState &pipelineState,
                           ShaderStage stageMask,
                           uint32_t offset,
                           uint32_t size,
                           const void *data) override;
        void Draw(uint32_t vertexCount,
                  uint32_t instanceCount = 1,
                  uint32_t firstVertex = 0,
                  uint32_t firstInstance = 0) override;
        void DrawIndexed(uint32_t indexCount,
                         uint32_t instanceCount = 1,
                         uint32_t firstIndex = 0,
                         int32_t vertexOffset = 0,
                         uint32_t firstInstance = 0) override;
        void Dispatch(uint32_t groupCountX,
                      uint32_t groupCountY = 1,
                      uint32_t groupCountZ = 1) override;

    private:
        FeatherVK::Device &m_device;
        VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};
    };
}
