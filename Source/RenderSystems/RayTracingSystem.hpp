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

        RayTracingSystem(Device &device,
                         std::shared_ptr<Material> material,
                         RenderCore::PipelineLibrary &pipelineLibrary) : RenderSystem(device, std::nullopt, material, pipelineLibrary) {};

        void Record(RenderGraph::RenderGraphPassContext &context) override {
            auto &frameInfo = context.frameInfo;
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

        void Init() override {
            createPipelineLayout();
            createPipeline();
        }

    private:
        PushConstant m_pushConstant{};

        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PushConstant)};
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "RayTracing/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});
        }

        void createPipeline() override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "RayTracing/Pipeline/" + std::to_string(m_material->getMaterialId()),
                    pipelineConfigureInfo,
                    m_material);
        };


#endif
    };
}


