#pragma  once

#include <limits>

#include "RenderSystem.h"

namespace FeatherVK {
    class GrassSystem : public RenderSystem {
    public:
        GrassSystem(Device &device,const VkRenderPass& renderPass,std::shared_ptr<Material>& material) : RenderSystem(device, renderPass, material) {
        }

    private:
        struct GrassPushConstant {
            glm::mat4 modelMatrix;
            glm::mat4 vaseModelMatrix;
        };

        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
                    VK_SHADER_STAGE_GEOMETRY_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(GrassPushConstant);

            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            const auto descriptorSetLayouts = CollectVkDescriptorSetLayouts(m_material->getRHIBindLayoutPointers());
            pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
            pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
            pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
            if (vkCreatePipelineLayout(device.device(), &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) !=
                VK_SUCCESS) {
                throw std::runtime_error("failed to create m_pipeline layout");
            }
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
            m_pipeline = std::make_unique<Pipeline>(
                    device,
                    pipelineConfigureInfo,
                    m_material
            );
        }

        void render(FrameInfo &frameInfo, id_t entityId, ECS::SceneRegistry &sceneRegistry) override {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            frameInfo.commandList->BindPipeline(*m_pipeline);
            BindMaterialResources(frameInfo);

            TransformComponent *transformComponent = nullptr;
            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            GrassPushConstant push{};
            if (m_moveEntityId == InvalidEntityId && sceneRegistry.GetEntityName(entityId) == "Vase") {
                m_moveEntityId = entityId;
            }
            if (m_moveEntityId != InvalidEntityId && sceneRegistry.IsAlive(m_moveEntityId)) {
                TransformComponent *moveTransform = nullptr;
                if (sceneRegistry.TryGetComponent(m_moveEntityId, moveTransform) && moveTransform != nullptr) {
                    push.vaseModelMatrix = moveTransform->mat4();
                }
            }
            push.modelMatrix = transformComponent->mat4();
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::AllGraphics,
                                                 0, sizeof(GrassPushConstant), &push);
            SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
        }

        static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();
        id_t m_moveEntityId = InvalidEntityId;
    };
}



