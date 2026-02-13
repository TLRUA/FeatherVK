#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {
    class PostSystem : public RenderSystem {
    public:
        struct PushConstant {
            int rayTracingImageIndex;
            bool firstFrame = true;
            alignas(16)glm::mat4 viewMatrix[2];
        };

        PostSystem(Device &device,
                   const VkRenderPass &renderPass,
                   std::shared_ptr<Material> material,
                   RenderCore::PipelineLibrary &pipelineLibrary) :
                RenderSystem(device, renderPass, material, pipelineLibrary) {};

        PostSystem(const RenderSystem &) = delete;


        void RenderWithImageIndex(FrameInfo &frameInfo, int imageIndex) {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);

            m_pushConstant.rayTracingImageIndex = imageIndex;
            m_pushConstant.viewMatrix[m_pushConstant.rayTracingImageIndex] = frameInfo.globalUbo.viewMatrix;
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Fragment, 0, sizeof(PushConstant), &m_pushConstant);
            BindMaterialResources(frameInfo);
            frameInfo.commandList->Draw(6, 1, 0, 0);

            m_pushConstant.firstFrame = false;
        }

        void render(FrameInfo &frameInfo) override {
            RenderWithImageIndex(frameInfo, frameInfo.frameIndex % 2);
        }

    private:
        PushConstant m_pushConstant{};

        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange = {};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PushConstant);
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "Post/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});
        }

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

            //No input bindings
            pipelineConfigureInfo.vertexBindingDescriptions.clear();
            pipelineConfigureInfo.attributeDescriptions.clear();

            pipelineConfigureInfo.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
            pipelineConfigureInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
            pipelineConfigureInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
            pipelineConfigureInfo.colorBlendAttachment.blendEnable = VK_FALSE;
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "Post/Pipeline/" + std::to_string(m_material->getMaterialId()),
                    pipelineConfigureInfo,
                    m_material);
        }

    };
}



