#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {

    const std::string RayGenShaderName = "rayGenShader";
    const std::string RayClosestHitShaderName = "rayClosestHitShader";
    const std::string RayMissShaderName = "rayMissShader";

    class RayTracingSystem : public RenderSystem {
#ifdef RAY_TRACING
    public:
        struct PushConstant {
            int rayTracingImageIndex;
        };

        RayTracingSystem(Device &device,const VkRenderPass& renderPass, std::shared_ptr<Material> material) : RenderSystem(device, nullptr, material) {};

        void rayTrace(FrameInfo &frameInfo) {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);

            m_pushConstant.rayTracingImageIndex = frameInfo.frameIndex % 2;
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::RayGen, 0, sizeof(PushConstant), &m_pushConstant);
            BindMaterialResources(frameInfo);

            Device::pfn_vkCmdTraceRaysKHR(frameInfo.commandBuffer,
                                          &m_pipeline->getGenRegion(),
                                          &m_pipeline->getMissRegion(),
                                          &m_pipeline->getHitRegion(),
                                          &m_pipeline->getCallableRegion(),
                                          frameInfo.sceneRenderExtent.width,
                                          frameInfo.sceneRenderExtent.height,
                                          1
            );
        }

        void render(FrameInfo &frameInfo) override {
            rayTrace(frameInfo);
        }

        void Init() override {
            createPipelineLayout();
            createPipeline(m_renderPass);
        }

    private:
        PushConstant m_pushConstant{};

        void createPipelineLayout() override {
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            const auto descriptorSetLayouts = CollectVkDescriptorSetLayouts(m_material->getRHIBindLayoutPointers());
            pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
            pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

            VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PushConstant)};
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
        };


#endif
    };
}


