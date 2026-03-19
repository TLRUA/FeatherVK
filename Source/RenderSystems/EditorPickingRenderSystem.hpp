#pragma once

#include "RenderSystem.h"

namespace Kaamoo {
    struct PickingPushConstantData {
        glm::mat4 modelMatrix{1.f};
        glm::mat4 idCarrier{1.f};
    };

    class EditorPickingRenderSystem : public RenderSystem {
    public:
        EditorPickingRenderSystem(Device &device, const VkRenderPass &renderPass, const std::shared_ptr<Material> &material)
                : RenderSystem(device, renderPass, material) {
            Init();
        }

        void render(FrameInfo &frameInfo, GameObject *gameObject = nullptr) override {
            if (gameObject == nullptr || !gameObject->IsActive()) {
                return;
            }

            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!gameObject->TryGetComponent(meshRendererComponent) || meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            const auto materialIt = frameInfo.materials.find(meshRendererComponent->GetMaterialID());
            if (materialIt != frameInfo.materials.end()) {
                const auto &pipelineCategory = materialIt->second->getPipelineCategory();
                if (pipelineCategory == PipelineCategory.SkyBox ||
                    pipelineCategory == PipelineCategory.Gizmos ||
                    pipelineCategory == PipelineCategory.Overlay) {
                    return;
                }
            }

            m_pipeline->bind(frameInfo.commandBuffer);

            std::vector<VkDescriptorSet> descriptorSets;
            for (auto &descriptorSetPointer: m_material->getDescriptorSetPointers()) {
                if (descriptorSetPointer != nullptr) {
                    descriptorSets.push_back(*descriptorSetPointer);
                }
            }

            if (!descriptorSets.empty()) {
                vkCmdBindDescriptorSets(
                        frameInfo.commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        m_pipelineLayout,
                        0,
                        static_cast<uint32_t>(descriptorSets.size()),
                        descriptorSets.data(),
                        0,
                        nullptr
                );
            }

            PickingPushConstantData push{};
            push.modelMatrix = gameObject->transform->mat4();
            push.idCarrier = gameObject->transform->normalMatrix();
            push.idCarrier[3][3] = static_cast<float>(gameObject->GetId());

            vkCmdPushConstants(frameInfo.commandBuffer,
                               m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(PickingPushConstantData),
                               &push);

            meshRendererComponent->GetModelPtr()->bind(frameInfo.commandBuffer);
            meshRendererComponent->GetModelPtr()->draw(frameInfo.commandBuffer);
        }

    protected:
        void createPipelineLayout() override {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PickingPushConstantData);

            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            const auto descriptorSetLayouts = m_material->getDescriptorSetLayoutPointers();
            std::vector<VkDescriptorSetLayout> layouts;
            layouts.reserve(descriptorSetLayouts.size());
            for (auto &descriptorSetLayoutPointer: descriptorSetLayouts) {
                layouts.push_back(descriptorSetLayoutPointer->getDescriptorSetLayout());
            }

            pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
            pipelineLayoutCreateInfo.pSetLayouts = layouts.data();
            pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(device.device(), &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
                throw std::runtime_error("failed to create editor picking pipeline layout");
            }
        }

        void createPipeline(VkRenderPass renderPass) override {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = m_pipelineLayout;

            m_pipeline = std::make_unique<Pipeline>(
                    device,
                    pipelineConfigureInfo,
                    m_material
            );
        }
    };
}
