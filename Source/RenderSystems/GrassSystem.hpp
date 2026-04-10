#pragma  once

#include "RenderSystem.h"

namespace FeatherVK {
    class GrassSystem : public RenderSystem {
    public:
        GrassSystem(Device &device,
                    const VkRenderPass &renderPass,
                    std::shared_ptr<Material> &material,
                    RenderCore::PipelineLibrary &pipelineLibrary) : RenderSystem(device, renderPass, material, pipelineLibrary) {
        }

        void render(FrameInfo &frameInfo, const RenderMeshInstance &meshInstance) override {
            if (frameInfo.commandList == nullptr || !meshInstance.IsRenderable()) {
                return;
            }

            frameInfo.commandList->BindPipeline(*m_pipeline);
            BindMaterialResources(frameInfo);

            GrassPushConstant push{};
            push.modelMatrix = meshInstance.worldTransform;

            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::AllGraphics,
                                                 0, sizeof(GrassPushConstant), &push);
            SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
        }

    private:
        struct GrassPushConstant {
            glm::mat4 modelMatrix{1.0f};
        };

        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
                    VK_SHADER_STAGE_GEOMETRY_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(GrassPushConstant);

            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "Grass/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});
        }

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

            pipelineConfigureInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            VkPipelineTessellationStateCreateInfo tessellationStateCreateInfo{};

            tessellationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
            tessellationStateCreateInfo.patchControlPoints = 3;
            tessellationStateCreateInfo.flags = 0;
            pipelineConfigureInfo.tessellationStateCreateInfo = tessellationStateCreateInfo;

            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "Grass/Pipeline/" + std::to_string(m_material->getMaterialId()),
                    pipelineConfigureInfo,
                    m_material);
        }

    };
}



