#include "RenderSystem.h"

#include <utility>
#include "../StructureInfos.h"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/TransformComponent.hpp"
#ifdef RAY_TRACING
#include "../Components/RayTracingInstanceComponent.hpp"
#endif

namespace FeatherVK {

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


    void RenderSystem::render(FrameInfo &frameInfo, id_t entityId, ECS::SceneRegistry &sceneRegistry) {
        if (m_material->getPipelineCategory() == PipelineCategory.Opaque ||
            m_material->getPipelineCategory() == PipelineCategory.Transparent) {
            TransformComponent *transformComponent = nullptr;
            MeshRendererComponent *meshRendererComponent = nullptr;
            if (!sceneRegistry.TryGetComponent(entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry.TryGetComponent(entityId, meshRendererComponent) || meshRendererComponent == nullptr ||
                !meshRendererComponent->IsVisible() ||
                !meshRendererComponent->IsOnDefaultRenderLayer() ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            RenderMeshInstance meshInstance{};
            meshInstance.entityId = entityId;
            meshInstance.worldTransform = transformComponent->mat4();
            meshInstance.normalMatrix = transformComponent->normalMatrix();
            meshInstance.materialId = meshRendererComponent->GetMaterialID();
            meshInstance.renderMesh = meshRendererComponent->GetModelPtr()->GetRenderMesh();
            meshInstance.active = sceneRegistry.IsEntityActive(entityId);
            meshInstance.visible = meshRendererComponent->IsVisible();
            meshInstance.defaultRenderLayer = meshRendererComponent->IsOnDefaultRenderLayer();
#ifdef RAY_TRACING
            RayTracingInstanceComponent *rayTracingInstanceComponent = nullptr;
            if (sceneRegistry.TryGetComponent(entityId, rayTracingInstanceComponent) &&
                rayTracingInstanceComponent != nullptr &&
                rayTracingInstanceComponent->IsValid()) {
                meshInstance.rayTracingInstanceId = rayTracingInstanceComponent->instanceId;
            }
#endif
            render(frameInfo, meshInstance, &sceneRegistry);
            return;
        }

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
                !meshRendererComponent->IsOnDefaultRenderLayer() ||
                meshRendererComponent->GetModelPtr() == nullptr) {
                return;
            }

            SimplePushConstantData push{};
            push.modelMatrix = transformComponent->mat4();
            push.normalMatrix = transformComponent->normalMatrix();
#ifdef RAY_TRACING
            RayTracingInstanceComponent *rayTracingInstanceComponent = nullptr;
            if (sceneRegistry.TryGetComponent(entityId, rayTracingInstanceComponent) &&
                rayTracingInstanceComponent != nullptr &&
                rayTracingInstanceComponent->IsValid() &&
                static_cast<size_t>(rayTracingInstanceComponent->instanceId) < frameInfo.pEntityDescs.size()) {
                const PBR &pbr = frameInfo.pEntityDescs[rayTracingInstanceComponent->instanceId].pbr;
                const glm::vec3 baseColor = pbr.albedo.x >= 0.0f ? pbr.albedo : glm::vec3{0.8f};
                const glm::vec3 emissive = pbr.emissive.x >= 0.0f ? pbr.emissive : glm::vec3{0.0f};
                push.baseColorMetallic = glm::vec4(baseColor, pbr.metallic >= 0.0f ? pbr.metallic : 0.0f);
                push.emissiveRoughnessOpacity = glm::vec4(emissive, pbr.opacity >= 0.0f ? pbr.opacity : 1.0f);
            }
#endif

            frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                 0, sizeof(SimplePushConstantData), &push);
            SubmitRenderMeshDraw(*frameInfo.commandList, meshRendererComponent->GetModelPtr()->GetRenderMesh());
        }
    }

    void RenderSystem::render(FrameInfo &frameInfo,
                              const RenderMeshInstance &meshInstance,
                              ECS::SceneRegistry *sceneRegistry) {
        if (frameInfo.commandList == nullptr) {
            return;
        }

        frameInfo.commandList->BindPipeline(*m_pipeline);
        BindMaterialResources(frameInfo);

        if (m_material->getPipelineCategory() == "Overlay") {
            frameInfo.commandList->Draw(6, 1, 0, 0);
            return;
        }

        if (m_material->getPipelineCategory() == "Light") {
            if (sceneRegistry == nullptr) {
                return;
            }

            PointLightPushConstant pointLightPushConstant{};
            TransformComponent *transformComponent = nullptr;
            LightComponent *lightComponent = nullptr;
            if (!sceneRegistry->TryGetComponent(meshInstance.entityId, transformComponent) || transformComponent == nullptr ||
                !sceneRegistry->TryGetComponent(meshInstance.entityId, lightComponent) || lightComponent == nullptr) {
                return;
            }

            pointLightPushConstant.position = glm::vec4(transformComponent->GetTranslation(), 1.f);
            pointLightPushConstant.color = glm::vec4(lightComponent->GetColor(), lightComponent->GetLightIntensity());
            pointLightPushConstant.radius = transformComponent->GetScale().x;
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
#ifdef RAY_TRACING
        if (meshInstance.rayTracingInstanceId != std::numeric_limits<id_t>::max() &&
            static_cast<size_t>(meshInstance.rayTracingInstanceId) < frameInfo.pEntityDescs.size()) {
            const PBR &pbr = frameInfo.pEntityDescs[meshInstance.rayTracingInstanceId].pbr;
            const glm::vec3 baseColor = pbr.albedo.x >= 0.0f ? pbr.albedo : glm::vec3{0.8f};
            const glm::vec3 emissive = pbr.emissive.x >= 0.0f ? pbr.emissive : glm::vec3{0.0f};
            push.baseColorMetallic = glm::vec4(baseColor, pbr.metallic >= 0.0f ? pbr.metallic : 0.0f);
            push.emissiveRoughnessOpacity = glm::vec4(emissive, pbr.opacity >= 0.0f ? pbr.opacity : 1.0f);
        }
#endif

        frameInfo.commandList->PushConstants(*m_pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                             0, sizeof(SimplePushConstantData), &push);
        SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
    }

    void RenderSystem::Init() {
        createPipelineLayout();
        createPipeline(m_renderPass);
    }


}
