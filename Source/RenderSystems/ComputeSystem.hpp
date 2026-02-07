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

        ComputeSystem(Device &device,const VkRenderPass& renderPass, std::shared_ptr<Material> material) :
                RenderSystem(device, renderPass, material) {};

        ComputeSystem(const RenderSystem &) = delete;


        void render(FrameInfo &frameInfo) override {
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
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            const auto descriptorSetLayouts = CollectVkDescriptorSetLayouts(m_material->getRHIBindLayoutPointers());
            pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
            pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

            VkPushConstantRange pushConstantRange = {};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PushConstant);
            pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
            if (vkCreatePipelineLayout(device.device(), &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) !=
                VK_SUCCESS) {
                throw std::runtime_error("failed to create m_pipeline layout");
            };
        }

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = std::make_unique<Pipeline>(device, pipelineConfigureInfo, m_material);
        };;

    };
}



