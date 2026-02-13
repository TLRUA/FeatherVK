#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {
    class SkyBoxSystem : public RenderSystem {
    public:
        SkyBoxSystem(Device &device,
                     const VkRenderPass &renderPass,
                     std::shared_ptr<Material> material,
                     RenderCore::PipelineLibrary &pipelineLibrary)
                : RenderSystem(device, renderPass, material, pipelineLibrary) {};

    private:
        void createPipelineLayout() override {
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "SkyBox/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts());
        }

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

            pipelineConfigureInfo.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
            pipelineConfigureInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
            pipelineConfigureInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
            pipelineConfigureInfo.colorBlendAttachment.blendEnable = VK_FALSE;
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "SkyBox/Pipeline/" + std::to_string(m_material->getMaterialId()),
                    pipelineConfigureInfo,
                    m_material);
        };

        void render(FrameInfo &frameInfo, id_t entityId, ECS::SceneRegistry &sceneRegistry) override {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);
            BindMaterialResources(frameInfo);

            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }
            SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
        };

    };
}



