#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {
    class SkyBoxSystem : public RenderSystem {
    public:
        SkyBoxSystem(Device &device,
                     const RenderGraph::RenderGraphGraphicsPipelineTarget &graphicsPipelineTarget,
                     std::shared_ptr<Material> material,
                     RenderCore::PipelineLibrary &pipelineLibrary)
                : RenderSystem(device, graphicsPipelineTarget, material, pipelineLibrary) {};

        void Record(RenderGraph::RenderGraphPassContext &context, const RenderMeshInstance &meshInstance) override {
            auto &frameInfo = context.frameInfo;
            if (frameInfo.commandList == nullptr || !meshInstance.IsRenderable()) {
                return;
            }

            frameInfo.commandList->BindPipeline(*m_pipeline);
            BindMaterialResources(frameInfo);
            SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
        }

    private:
        void createPipelineLayout() override {
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "SkyBox/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts());
        }

        void createPipeline() override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

            const auto *graphicsPipelineTarget = GetGraphicsPipelineTarget();
            assert(graphicsPipelineTarget != nullptr && "SkyBoxSystem requires a graphics pipeline target");
            pipelineConfigureInfo.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
            pipelineConfigureInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
            pipelineConfigureInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
            pipelineConfigureInfo.colorBlendAttachment.blendEnable = VK_FALSE;
            pipelineConfigureInfo.renderPass = graphicsPipelineTarget->compatibleRenderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "SkyBox/Pipeline/" + std::to_string(m_material->getMaterialId()) + "/" + graphicsPipelineTarget->signature.key,
                    pipelineConfigureInfo,
                    m_material);
        };

    };
}



