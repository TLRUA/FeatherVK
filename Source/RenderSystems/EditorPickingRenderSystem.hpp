#pragma once

#include "RenderSystem.h"

namespace FeatherVK {
    struct PickingPushConstantData {
        glm::mat4 modelMatrix{1.f};
        glm::mat4 idCarrier{1.f};
    };

    class EditorPickingRenderSystem : public RenderSystem {
    public:
        EditorPickingRenderSystem(Device &device,
                                  const RenderGraph::RenderGraphGraphicsPipelineTarget &graphicsPipelineTarget,
                                  const std::shared_ptr<Material> &material,
                                  RenderCore::PipelineLibrary &pipelineLibrary)
                : RenderSystem(device, graphicsPipelineTarget, material, pipelineLibrary) {
            Init();
        }

        void Record(RenderGraph::RenderGraphPassContext &context, const RenderMeshInstance &meshInstance) override {
            auto &frameInfo = context.frameInfo;
            if (!meshInstance.IsPickable()) {
                return;
            }

            BindCommonDescriptors(frameInfo);

            PickingPushConstantData push{};
            push.modelMatrix = meshInstance.worldTransform;
            push.idCarrier = glm::mat4(meshInstance.normalMatrix);
            push.idCarrier[3][3] = static_cast<float>(meshInstance.entityId);

            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(PickingPushConstantData), &push);

            if (frameInfo.commandList != nullptr) {
                SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
            }
        }

    protected:
        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PickingPushConstantData);

            RenderCore::MaterialBindingsView materialBindings{*m_material};
            m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "EditorPicking/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});
        }

        void createPipeline() override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            const auto *graphicsPipelineTarget = GetGraphicsPipelineTarget();
            assert(graphicsPipelineTarget != nullptr && "EditorPickingRenderSystem requires a graphics pipeline target");
            pipelineConfigureInfo.vertexBindingDescriptions = Model::Vertex::getBindingDescriptions();
            pipelineConfigureInfo.attributeDescriptions = {
                    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Model::Vertex, position)}
            };
            pipelineConfigureInfo.renderPass = graphicsPipelineTarget->compatibleRenderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;

            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "EditorPicking/Pipeline/" + std::to_string(m_material->getMaterialId()) + "/" + graphicsPipelineTarget->signature.key,
                    pipelineConfigureInfo,
                    m_material);
        }

    private:
        bool ShouldSkipPipeline(FrameInfo &frameInfo, id_t materialId) const {
            const auto materialIt = frameInfo.materials.find(materialId);
            if (materialIt == frameInfo.materials.end()) {
                return false;
            }

            const auto &pipelineCategory = materialIt->second->getPipelineCategory();
            return pipelineCategory == PipelineCategory.SkyBox ||
                   pipelineCategory == PipelineCategory.Gizmos ||
                   pipelineCategory == PipelineCategory.Overlay;
        }

        void BindCommonDescriptors(FrameInfo &frameInfo) {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);
            BindMaterialResources(frameInfo);
        }
    };
}
