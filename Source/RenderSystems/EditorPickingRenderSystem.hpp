#pragma once

#include "RenderSystem.h"
#include "../ECS/SceneRegistry.hpp"

namespace FeatherVK {
    struct PickingPushConstantData {
        glm::mat4 modelMatrix{1.f};
        glm::mat4 idCarrier{1.f};
    };

    class EditorPickingRenderSystem : public RenderSystem {
    public:
        EditorPickingRenderSystem(Device &device,
                                  const VkRenderPass &renderPass,
                                  const std::shared_ptr<Material> &material,
                                  RenderCore::PipelineLibrary &pipelineLibrary)
                : RenderSystem(device, renderPass, material, pipelineLibrary) {
            Init();
        }

        void render(FrameInfo &frameInfo, id_t entityId, ECS::SceneRegistry &sceneRegistry) override {
            if (!sceneRegistry.IsAlive(entityId) || !sceneRegistry.IsEntityActive(entityId)) {
                return;
            }

            TransformComponent *transformComponent = nullptr;
            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                !meshRendererComponent->IsVisible() ||
                !meshRendererComponent->IsOnDefaultRenderLayer() ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            if (ShouldSkipPipeline(frameInfo, meshRendererComponent->GetMaterialID())) {
                return;
            }

            BindCommonDescriptors(frameInfo);

            PickingPushConstantData push{};
            push.modelMatrix = transformComponent->mat4();
            push.idCarrier = transformComponent->normalMatrix();
            push.idCarrier[3][3] = static_cast<float>(entityId);

            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(PickingPushConstantData), &push);

            if (frameInfo.commandList != nullptr) {
                SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
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

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            pipelineConfigureInfo.vertexBindingDescriptions = Model::Vertex::getBindingDescriptions();
            pipelineConfigureInfo.attributeDescriptions = {
                    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Model::Vertex, position)}
            };
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;

            m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "EditorPicking/Pipeline/" + std::to_string(m_material->getMaterialId()),
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
