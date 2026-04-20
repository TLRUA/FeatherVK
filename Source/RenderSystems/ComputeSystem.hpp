#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {

    class ComputeSystem : public RenderSystem {
    public:
        struct PushConstant {
            int rayTracingImageIndex;
            alignas(4) bool firstFrame = true;
            alignas(4) bool sceneUpdated = false;
            alignas(16)glm::mat4 viewMatrix[2];
        };

        ComputeSystem(Device &device,
                      std::shared_ptr<Material> material,
                      RenderCore::PipelineLibrary &pipelineLibrary) :
                RenderSystem(device, std::nullopt, material, pipelineLibrary) {};

        ComputeSystem(const RenderSystem &) = delete;


        void Record(RenderGraph::RenderGraphPassContext &context) override {
            auto &frameInfo = context.frameInfo;
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);

            m_pushConstant.rayTracingImageIndex = frameInfo.frameIndex % 2;
            m_pushConstant.viewMatrix[m_pushConstant.rayTracingImageIndex] = frameInfo.globalUbo.viewMatrix;
            m_pushConstant.sceneUpdated = frameInfo.sceneUpdated;
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Compute, 0, sizeof(PushConstant), &m_pushConstant);
            BindMaterialResources(frameInfo);

            uint32_t groupCountX = (frameInfo.sceneRenderExtent.width + 15) / 16;
            uint32_t groupCountY = (frameInfo.sceneRenderExtent.height + 15) / 16;
            frameInfo.commandList->Dispatch(groupCountX, groupCountY, 1);

            m_pushConstant.firstFrame = false;
        }

    private:
        PushConstant m_pushConstant{};

        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange = {};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PushConstant);
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "Compute/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});
        }

        void createPipeline() override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "Compute/Pipeline/" + std::to_string(m_material->getMaterialId()),
                    pipelineConfigureInfo,
                    m_material);
        };;

    };
}



