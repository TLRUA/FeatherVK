#include "RenderSystem.h"

#include <utility>
#include "../StructureInfos.h"

namespace FeatherVK {
    namespace {
        [[nodiscard]] id_t ResolveRayTracingInstanceId(const FrameInfo &frameInfo,
                                                      const RenderMeshInstance &meshInstance) {
#ifdef RAY_TRACING
            if (meshInstance.rayTracingInstanceId != std::numeric_limits<id_t>::max()) {
                return meshInstance.rayTracingInstanceId;
            }

            if (frameInfo.rayTracingInstanceIds != nullptr) {
                const auto rayTracingInstanceIt = frameInfo.rayTracingInstanceIds->find(meshInstance.entityId);
                if (rayTracingInstanceIt != frameInfo.rayTracingInstanceIds->end()) {
                    return rayTracingInstanceIt->second;
                }
            }
#else
            (void) frameInfo;
            (void) meshInstance;
#endif
            return std::numeric_limits<id_t>::max();
        }

        void ApplyRayTracingPbrPushConstants(SimplePushConstantData &push,
                                            const FrameInfo &frameInfo,
                                            id_t rayTracingInstanceId) {
#ifdef RAY_TRACING
            if (rayTracingInstanceId != std::numeric_limits<id_t>::max() &&
                static_cast<size_t>(rayTracingInstanceId) < frameInfo.pEntityDescs.size()) {
                const PBR &pbr = frameInfo.pEntityDescs[rayTracingInstanceId].pbr;
                const glm::vec3 baseColor = pbr.albedo.x >= 0.0f ? pbr.albedo : glm::vec3{0.8f};
                const glm::vec3 emissive = pbr.emissive.x >= 0.0f ? pbr.emissive : glm::vec3{0.0f};
                push.baseColorMetallic = glm::vec4(baseColor, pbr.metallic >= 0.0f ? pbr.metallic : 0.0f);
                push.emissiveRoughnessOpacity = glm::vec4(emissive, pbr.opacity >= 0.0f ? pbr.opacity : 1.0f);
            }
#else
            (void) push;
            (void) frameInfo;
            (void) rayTracingInstanceId;
#endif
        }
    }

    RenderSystem::RenderSystem(Device &device,
                               const VkRenderPass &renderPass,
                               const std::shared_ptr<Material> material,
                               RenderCore::PipelineLibrary &pipelineLibrary)
            : device{device},
              m_material{material},
              m_renderPass{renderPass},
              m_pipelineLibrary{pipelineLibrary} {

    }

    RenderSystem::~RenderSystem() = default;

    void RenderSystem::createPipelineLayout() {
        RenderCore::MaterialBindingsView materialBindings{*m_material};
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

        std::vector<VkPushConstantRange> pushConstantRanges{};
        if (pushConstantRange.size > 0) {
            pushConstantRanges.push_back(pushConstantRange);
        }
        m_pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                "RenderSystem/Layout/" + materialBindings.PipelineCategory() + "/" + std::to_string(materialBindings.MaterialId()),
                materialBindings.GetBindLayouts(),
                pushConstantRanges);
    }

    void RenderSystem::BindMaterialResources(FrameInfo &frameInfo) {
        if (frameInfo.commandList == nullptr) {
            return;
        }
        RenderCore::MaterialBindingsView materialBindings{*m_material};
        frameInfo.commandList->BindResources(*m_pipeline, 0, materialBindings.GetBindSets());
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
        m_pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                "RenderSystem/Pipeline/" + m_material->getPipelineCategory() + "/" + std::to_string(m_material->getMaterialId()),
                pipelineConfigureInfo,
                m_material);
    }

    void RenderSystem::render(FrameInfo &frameInfo, const RenderMeshInstance &meshInstance) {
        if (frameInfo.commandList == nullptr) {
            return;
        }

        frameInfo.commandList->BindPipeline(*m_pipeline);
        BindMaterialResources(frameInfo);

        if (m_material->getPipelineCategory() == "Overlay") {
            if (!meshInstance.overlayLike && meshInstance.entityId != RenderMeshInstance::InvalidEntityId) {
                return;
            }
            frameInfo.commandList->Draw(6, 1, 0, 0);
            return;
        }

        if (m_material->getPipelineCategory() == "Light") {
            if (!meshInstance.HasLightProxy()) {
                return;
            }

            const auto &lightProxy = *meshInstance.lightProxy;
            PointLightPushConstant pointLightPushConstant{};
            pointLightPushConstant.position = glm::vec4(meshInstance.worldTransform[3]);
            pointLightPushConstant.color = glm::vec4(lightProxy.color, lightProxy.intensity);
            pointLightPushConstant.radius = lightProxy.radius;
            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(PointLightPushConstant), &pointLightPushConstant);
            frameInfo.commandList->Draw(6, 1, 0, 0);
            return;
        }

        if (m_material->getPipelineCategory() != PipelineCategory.Opaque &&
            m_material->getPipelineCategory() != PipelineCategory.Transparent) {
            return;
        }

        if (!meshInstance.IsRenderable() || !meshInstance.defaultRenderLayer) {
            return;
        }

        SimplePushConstantData push{};
        push.modelMatrix = meshInstance.worldTransform;
        push.normalMatrix = glm::mat4(meshInstance.normalMatrix);
        ApplyRayTracingPbrPushConstants(push, frameInfo, ResolveRayTracingInstanceId(frameInfo, meshInstance));

        frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                             0, sizeof(SimplePushConstantData), &push);
        SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
    }

    void RenderSystem::Init() {
        createPipelineLayout();
        createPipeline(m_renderPass);
    }


}
