#include "RenderSystem.h"

#include <utility>
#include "../StructureInfos.h"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/TransformComponent.hpp"

namespace FeatherVK {

    RenderSystem::RenderSystem(Device &device, const VkRenderPass &renderPass, const std::shared_ptr<Material> material)
            : device{device}, m_material{material}, m_renderPass{renderPass} {

    }

    RenderSystem::~RenderSystem() {
        vkDestroyPipelineLayout(device.device(), m_pipelineLayout, nullptr);
    }

    void RenderSystem::createPipelineLayout() {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        if (m_material->getPipelineCategory() == "Light") {
            pushConstantRange.size = sizeof(PointLightPushConstant);
        } else if (m_material->getPipelineCategory() == "Opaque")
            pushConstantRange.size = sizeof(SimplePushConstantData);
        else if (m_material->getPipelineCategory() == "Overlay") {
            pushConstantRange.size = 0;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        const auto &rhiBindLayouts = m_material->getRHIBindLayoutPointers();
        const auto descriptorSetLayouts = CollectVkDescriptorSetLayouts(rhiBindLayouts);
        pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRange.size == 0 ? 0 : 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create m_pipeline layout");
        }

    }

    void RenderSystem::BindMaterialResources(FrameInfo &frameInfo) {
        if (frameInfo.commandList == nullptr) {
            return;
        }
        frameInfo.commandList->BindResources(*m_pipeline, 0, m_material->getRHIBindSetPointers());
    }

    void RenderSystem::createPipeline(VkRenderPass renderPass) {
        PipelineConfigureInfo pipelineConfigureInfo{};
        Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

        if (m_material->getPipelineCategory() == PipelineCategory.Light) {
            Pipeline::enableAlphaBlending(pipelineConfigureInfo);
            pipelineConfigureInfo.attributeDescriptions.clear();
            pipelineConfigureInfo.vertexBindingDescriptions.clear();
        } else if (m_material->getPipelineCategory() == "Overlay") {
            pipelineConfigureInfo.attributeDescriptions.clear();
            pipelineConfigureInfo.vertexBindingDescriptions.clear();
        } else if (m_material->getPipelineCategory() == PipelineCategory.Transparent) {
            Pipeline::enableAlphaBlending(pipelineConfigureInfo);
        }

        pipelineConfigureInfo.renderPass = renderPass;
        pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;
        m_pipeline = std::make_unique<Pipeline>(
                device,
                pipelineConfigureInfo,
                m_material
        );
    }


    void RenderSystem::render(FrameInfo &frameInfo, id_t entityId, ECS::SceneRegistry &sceneRegistry) {
        if (frameInfo.commandList == nullptr) {
            return;
        }
        frameInfo.commandList->BindPipeline(*m_pipeline);
        BindMaterialResources(frameInfo);

        if (m_material->getPipelineCategory() == "Overlay") {
            frameInfo.commandList->Draw(6, 1, 0, 0);
        } else if (m_material->getPipelineCategory() == "Light") {
            PointLightPushConstant pointLightPushConstant{};

            TransformComponent *transformComponent = nullptr;
            LightComponent *lightComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, lightComponent) || lightComponent == nullptr) {
                return;
            }

            pointLightPushConstant.position = glm::vec4(transformComponent->GetTranslation(), 1.f);
            pointLightPushConstant.color = glm::vec4(lightComponent->GetColor(), lightComponent->GetLightIntensity());
            pointLightPushConstant.radius = transformComponent->GetScale().x;
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(PointLightPushConstant), &pointLightPushConstant);
            frameInfo.commandList->Draw(6, 1, 0, 0);
        } else if (m_material->getPipelineCategory() == "Opaque") {
            TransformComponent *transformComponent = nullptr;
            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                !meshRendererComponent->IsVisible() ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            SimplePushConstantData push{};
            push.modelMatrix = transformComponent->mat4();
            push.normalMatrix = transformComponent->normalMatrix();

            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(SimplePushConstantData), &push);
            SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
        }
    }

    void RenderSystem::Init() {
        createPipelineLayout();
        createPipeline(m_renderPass);
    }


}
