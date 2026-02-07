#pragma once

#include <memory>
#include <vector>

#include "RHIBinding.hpp"
#include "RHIResources.hpp"

namespace FeatherVK::RHI {
    class RHICommandList : public RHIObject {
    public:
        ~RHICommandList() override = default;

        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const ScissorRect &scissor) = 0;
        virtual void BindPipeline(RHIPipelineState &pipelineState) = 0;
        virtual void BindResources(RHIPipelineState &pipelineState,
                                   uint32_t firstSet,
                                   const std::vector<std::shared_ptr<RHIBindSet>> &bindSets) = 0;
        virtual void BindVertexBuffer(uint32_t slot,
                                      RHIBuffer &buffer,
                                      uint64_t offset = 0) = 0;
        virtual void BindIndexBuffer(RHIBuffer &buffer,
                                     uint64_t offset = 0) = 0;
        virtual void PushConstants(RHIPipelineState &pipelineState,
                                   ShaderStage stageMask,
                                   uint32_t offset,
                                   uint32_t size,
                                   const void *data) = 0;
        virtual void Draw(uint32_t vertexCount,
                          uint32_t instanceCount = 1,
                          uint32_t firstVertex = 0,
                          uint32_t firstInstance = 0) = 0;
        virtual void DrawIndexed(uint32_t indexCount,
                                 uint32_t instanceCount = 1,
                                 uint32_t firstIndex = 0,
                                 int32_t vertexOffset = 0,
                                 uint32_t firstInstance = 0) = 0;
        virtual void Dispatch(uint32_t groupCountX,
                              uint32_t groupCountY = 1,
                              uint32_t groupCountZ = 1) = 0;
    };
}
