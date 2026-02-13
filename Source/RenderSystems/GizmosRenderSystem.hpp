#pragma  once

#include "RenderSystem.h"
#include "../ECS/SceneRegistry.hpp"
#include "../Managers/ModelRepository.hpp"
#include "../RenderCore/RenderCore.hpp"

namespace FeatherVK {

    const std::string GIZMOS_MODEL_PATH = "Gizmos/";
    const std::string GIZMOS_SHADER_PATH = "Gizmos/";

    const int GIZMOS_AXIS_RADIUS = 240;

    enum GizmosType {
        Axis,
        EdgeDetectionStencil,
        EdgeDetection
    };

    class GizmosRenderSystem : public RenderSystem {
    public:
        ~GizmosRenderSystem() = default;

        GizmosRenderSystem(Device &device,
                           const VkRenderPass& renderPass,
                           std::shared_ptr<Material> material,
                           ModelRepository &modelRepository,
                           RenderCore::CoreServices &renderCore)
                : RenderSystem(device, renderPass, material, renderCore.GetPipelineLibrary()),
                  m_renderCore(renderCore) {
            auto &shaderLibrary = m_renderCore.GetShaderLibrary();
            m_pipelineLayout = VK_NULL_HANDLE;
            // Axis
            {
                std::string axisModelName = "axis.obj";
                std::string axisVertexShaderName = "axis.vert.spv";
                std::string axisFragmentShaderName = "axis.frag.spv";
                m_axisModel = modelRepository.GetOrLoad(axisModelName, GIZMOS_MODEL_PATH + axisModelName);
                constexpr float axisScale = 0.266666f;
                m_axisTransform.SetScale(glm::vec3(axisScale));
                m_axisTransform.SetWorldTransform(glm::vec3{0.0f}, glm::vec3{axisScale}, glm::vec3{0.0f});
                m_axisMaterial = std::make_shared<Material>(*material);
                const std::string vertexShaderPath = GIZMOS_SHADER_PATH + axisVertexShaderName;
                const std::string fragmentShaderPath = GIZMOS_SHADER_PATH + axisFragmentShaderName;
                m_axisMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(vertexShaderPath, ShaderCategory::vertex));
                m_axisMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(fragmentShaderPath, ShaderCategory::fragment));
                createPipelineLayout(GizmosType::Axis);
                createPipeline(renderPass, m_axisPipeline, m_axisMaterial, m_axisPipelineLayout, GizmosType::Axis);
            }

            //Edge detection
            {
                std::string vertexShaderName = "edgeDetection.vert.spv";
//                std::string geometryShaderName = "edgeDetection.geom.spv";
                std::string fragmentShaderName = "edgeDetection.frag.spv";
                m_edgeDetectionMaterial = std::make_shared<Material>(*material);
                m_edgeDetectionStencilMaterial = std::make_shared<Material>(*material);
                const std::string vertexShaderPath = GIZMOS_SHADER_PATH + vertexShaderName;
//                const std::string geometryShaderPath = GIZMOS_SHADER_PATH + geometryShaderName;
                const std::string fragmentShaderPath = GIZMOS_SHADER_PATH + fragmentShaderName;
                m_edgeDetectionMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(vertexShaderPath, ShaderCategory::vertex));
//                m_edgeDetectionMaterial->getShaderModulePointers().push_back(std::make_shared<ShaderModule>(shaderBuilder.createShaderModule(geometryShaderPath), ShaderCategory::geometry));
                m_edgeDetectionMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(fragmentShaderPath, ShaderCategory::fragment));

                std::string stencilVertexShaderName = "MyShader.vert.spv";
                const std::string stencilVertexShaderPath = stencilVertexShaderName;
                m_edgeDetectionStencilMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(stencilVertexShaderPath, ShaderCategory::vertex));
                m_edgeDetectionStencilMaterial->getShaderModulePointers().push_back(shaderLibrary.LoadStage(fragmentShaderPath, ShaderCategory::fragment));

                createPipelineLayout(GizmosType::EdgeDetection);

                createPipeline(renderPass, m_edgeDetectionPipeline, m_edgeDetectionMaterial, m_edgeDetectionPipelineLayout, GizmosType::EdgeDetection);
                createPipeline(renderPass, m_edgeDetectionStencilPipeline, m_edgeDetectionStencilMaterial, m_edgeDetectionPipelineLayout, GizmosType::EdgeDetectionStencil);
            }
        };

        void createPipelineLayout(GizmosType gizmosType) {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.size = sizeof(SimplePushConstantData);
            RenderCore::MaterialBindingsView materialBindings{*m_material};
            switch (gizmosType) {
                case GizmosType::Axis:
                    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                    m_axisPipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                            "Gizmos/Axis/Layout/" + std::to_string(materialBindings.MaterialId()),
                            materialBindings.GetBindLayouts(),
                            {pushConstantRange});
                    break;
                case GizmosType::EdgeDetectionStencil:
                case GizmosType::EdgeDetection:
                    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
                    pushConstantRange.size = sizeof(SimplePushConstantData);
                    m_edgeDetectionPipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                            "Gizmos/Edge/Layout/" + std::to_string(materialBindings.MaterialId()),
                            materialBindings.GetBindLayouts(),
                            {pushConstantRange});
                    break;
            }
        }

        void createPipeline(VkRenderPass renderPass, std::shared_ptr<Pipeline> &pipeline, std::shared_ptr<Material> material, VkPipelineLayout &pipelineLayout, GizmosType gizmosType) {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);
            switch (gizmosType) {
                case GizmosType::Axis:
                    break;
                case GizmosType::EdgeDetection: {
                    VkStencilOpState front{};
                    front.failOp = VK_STENCIL_OP_KEEP;
                    front.passOp = VK_STENCIL_OP_KEEP;
                    front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
                    front.compareMask = edgeDetectionStencilMask;
                    front.writeMask = edgeDetectionStencilMask;
                    front.reference = 1;
                    pipelineConfigureInfo.depthStencilInfo.front = front;
                    pipelineConfigureInfo.depthStencilInfo.back = front;
                    pipelineConfigureInfo.depthStencilInfo.stencilTestEnable = VK_TRUE;
                    pipelineConfigureInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
                    pipelineConfigureInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
//                    pipelineConfigureInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
                    break;
                }
                case GizmosType::EdgeDetectionStencil: {
                    pipelineConfigureInfo.colorBlendAttachment.colorWriteMask = 0;
                    VkStencilOpState front{};
                    front.failOp = VK_STENCIL_OP_ZERO;
                    front.passOp = VK_STENCIL_OP_REPLACE;
                    front.compareOp = VK_COMPARE_OP_ALWAYS;
                    front.compareMask = edgeDetectionStencilMask;
                    front.writeMask = edgeDetectionStencilMask;
                    front.reference = 1;
                    pipelineConfigureInfo.depthStencilInfo.front = front;
                    pipelineConfigureInfo.depthStencilInfo.back = front;
                    pipelineConfigureInfo.depthStencilInfo.stencilTestEnable = VK_TRUE;
                    pipelineConfigureInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
                    pipelineConfigureInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
                    break;
                }
            }
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = pipelineLayout;
            const std::string pipelineKey = gizmosType == GizmosType::Axis
                                                ? "Gizmos/Axis/Pipeline"
                                                : gizmosType == GizmosType::EdgeDetectionStencil
                                                      ? "Gizmos/EdgeStencil/Pipeline"
                                                      : "Gizmos/Edge/Pipeline";
            pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    pipelineKey,
                    pipelineConfigureInfo,
                    material);
        };

        void render(FrameInfo &frameInfo, GizmosType gizmosType) {
            if (frameInfo.commandList == nullptr) {
                return;
            }
            switch (gizmosType) {
                case GizmosType::Axis: {
                    frameInfo.commandList->BindPipeline(*m_axisPipeline);
                    frameInfo.commandList->BindResources(*m_axisPipeline, 0, m_axisMaterial->getRHIBindSetPointers());
                    if (m_axisModel != nullptr) {
                        SimplePushConstantData push{};
                        push.modelMatrix = m_axisTransform.mat4();
                        push.normalMatrix = m_axisTransform.normalMatrix();

                        frameInfo.commandList->PushConstants(*m_axisPipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                             0, sizeof(SimplePushConstantData), &push);
                        const float axisSize = std::min(
                            static_cast<float>(GIZMOS_AXIS_RADIUS),
                            frameInfo.sceneViewportRect.width);
                        VkViewport viewport{};
                        viewport.x = frameInfo.sceneViewportRect.x + frameInfo.sceneViewportRect.width - axisSize;
                        viewport.y = frameInfo.sceneViewportRect.y;
                        viewport.width = axisSize > 0.0f ? axisSize : 1.0f;
                        viewport.height = axisSize > 0.0f ? axisSize : 1.0f;
                        viewport.minDepth = 0.0f;
                        viewport.maxDepth = 1.0f;
                        frameInfo.commandList->SetViewport({viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth});
                        SubmitRenderMeshDraw(*frameInfo.commandList, m_axisModel->GetRenderMesh());
                    };
                    break;
                }

                case GizmosType::EdgeDetectionStencil: {
                    frameInfo.commandList->BindPipeline(*m_edgeDetectionStencilPipeline);
                    frameInfo.commandList->BindResources(*m_edgeDetectionStencilPipeline, 0, m_edgeDetectionStencilMaterial->getRHIBindSetPointers());
                    TransformComponent *selectedTransform = nullptr;
                    MeshRendererComponent *meshRendererComponent = nullptr;
                    if (TryGetSelectedRenderData(frameInfo, selectedTransform, meshRendererComponent)) {
                        SimplePushConstantData push{};
                        push.modelMatrix = selectedTransform->mat4();
                        push.normalMatrix = selectedTransform->normalMatrix();

                        frameInfo.commandList->PushConstants(*m_edgeDetectionStencilPipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Geometry,
                                                             0, sizeof(SimplePushConstantData), &push);
                        SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
                    }
                    break;
                }

                case GizmosType::EdgeDetection: {
                    frameInfo.commandList->BindPipeline(*m_edgeDetectionPipeline);
                    frameInfo.commandList->BindResources(*m_edgeDetectionPipeline, 0, m_edgeDetectionMaterial->getRHIBindSetPointers());
                    TransformComponent *selectedTransform = nullptr;
                    MeshRendererComponent *meshRendererComponent = nullptr;
                    if (TryGetSelectedRenderData(frameInfo, selectedTransform, meshRendererComponent)) {
                        SimplePushConstantData push{};
                        push.modelMatrix = selectedTransform->mat4();
                        push.normalMatrix = selectedTransform->normalMatrix();
                        frameInfo.commandList->PushConstants(*m_edgeDetectionPipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Geometry,
                                                             0, sizeof(SimplePushConstantData), &push);
                    SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
                    }
                    break;
                }
            }


        };


        bool TryGetSelectedRenderData(FrameInfo &frameInfo,
                                      TransformComponent *&selectedTransform,
                                      MeshRendererComponent *&selectedMeshRenderer) {
            selectedTransform = nullptr;
            selectedMeshRenderer = nullptr;

            if (frameInfo.sceneRegistry == nullptr) {
                return false;
            }

            const id_t selectedEntityId = frameInfo.selectedEntityId;
            if (frameInfo.sceneRegistry->IsAlive(selectedEntityId) &&
                frameInfo.sceneRegistry->IsEntityActive(selectedEntityId) &&
                frameInfo.sceneRegistry->TryGetComponent(selectedEntityId, selectedTransform) &&
                frameInfo.sceneRegistry->TryGetComponent(selectedEntityId, selectedMeshRenderer) &&
                selectedTransform != nullptr &&
                selectedMeshRenderer != nullptr &&
                selectedMeshRenderer->GetModelPtr() != nullptr) {
                return true;
            }

            return false;
        }
    private:

        std::shared_ptr<Model> m_axisModel;
        TransformComponent m_axisTransform;
        VkPipelineLayout m_axisPipelineLayout;
        std::shared_ptr<Pipeline> m_axisPipeline;
        std::shared_ptr<Material> m_axisMaterial;

        VkPipelineLayout m_edgeDetectionPipelineLayout;
        uint32_t edgeDetectionStencilMask = 0x01;
        std::shared_ptr<Pipeline> m_edgeDetectionPipeline;
        std::shared_ptr<Pipeline> m_edgeDetectionStencilPipeline;
        std::shared_ptr<Material> m_edgeDetectionMaterial;
        std::shared_ptr<Material> m_edgeDetectionStencilMaterial;
        const float m_scaleFactor = 1.1f;
        RenderCore::CoreServices &m_renderCore;
    };
}



