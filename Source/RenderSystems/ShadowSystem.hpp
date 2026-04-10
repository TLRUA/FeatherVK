#pragma once

#include <memory>
#include <glm/gtc/constants.hpp>
#include "../Pipeline.hpp"
#include "../Device.hpp"
#include "../Model.hpp"
#include "../StructureInfos.h"
#include "../RenderCore/MaterialBindings.hpp"
#include "../RenderCore/PipelineLibrary.hpp"
#include "../RenderScene/RenderScene.hpp"
namespace FeatherVK {
    class ShadowSystem {
    public:
        ShadowSystem(Device &device,
                     const VkRenderPass &renderPass,
                     const std::shared_ptr<Material> &material,
                     RenderCore::PipelineLibrary &pipelineLibrary) :
                     device{device}, material{material}, m_pipelineLibrary{pipelineLibrary} {
            createPipelineLayout();
            createPipeline(renderPass);
        }

        ~ShadowSystem() = default;

        ShadowSystem(const ShadowSystem &) = delete;

        ShadowSystem &operator=(const ShadowSystem &) = delete;

        void renderShadow(FrameInfo &frameInfo) {
            if (frameInfo.commandList == nullptr || frameInfo.renderScene == nullptr) {
                return;
            }

            frameInfo.commandList->BindPipeline(*pipeline);
            RenderCore::MaterialBindingsView materialBindings{*material};
            frameInfo.commandList->BindResources(*pipeline, 0, materialBindings.GetBindSets());

            for (const auto &meshInstance: frameInfo.renderScene->GetMeshInstances()) {
                if (!meshInstance.IsShadowCaster()) {
                    continue;
                }

                ShadowPushConstant push{};
                push.modelMatrix = meshInstance.worldTransform;
                frameInfo.commandList->PushConstants(*pipeline, RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                                     0, sizeof(ShadowPushConstant), &push);
                SubmitRenderMeshDraw(*frameInfo.commandList, meshInstance.renderMesh);
            }
        }


        template<class T>
        void UpdateGlobalUboBuffer(T &globalUbo, uint32_t frameIndex) {
            if (material->getBufferPointers().empty()) {
                return;
            }
            material->getBufferPointers()[0]->writeToIndex(&globalUbo, frameIndex);
            material->getBufferPointers()[0]->flushIndex(frameIndex);
        };

        ///@param[in]rotation:defined in DEGREE instead of RADIANT
        glm::mat4 calculateViewMatrixForRotation(glm::vec3 position, glm::vec3 rotation) {
            glm::mat4 mat{1.0};
            float rx = glm::radians(rotation.x);
            float ry = glm::radians(rotation.y);
            float rz = glm::radians(rotation.z);
            mat = glm::rotate(mat, -rx, glm::vec3(1, 0, 0));
            mat = glm::rotate(mat, -ry, glm::vec3(0, 1, 0));
            mat = glm::rotate(mat, -rz, glm::vec3(0, 0, 1));
            mat = glm::translate(mat, -position);
            return mat;
        }

    private:
        struct ShadowPushConstant {
            glm::mat4 modelMatrix{};
        };

        void createPipelineLayout() {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(ShadowPushConstant);

            RenderCore::MaterialBindingsView materialBindings{*material};
            pipelineLayout = m_pipelineLibrary.GetOrCreateLayout(
                    "Shadow/Layout/" + std::to_string(materialBindings.MaterialId()),
                    materialBindings.GetBindLayouts(),
                    {pushConstantRange});

        }

        void createPipeline(VkRenderPass renderPass) {
            PipelineConfigureInfo pipelineConfigureInfo{};
            Pipeline::setDefaultPipelineConfigureInfo(pipelineConfigureInfo);

            VkVertexInputBindingDescription bindingDescription[1];
            VkVertexInputAttributeDescription attributeDescription[1];
            bindingDescription[0].binding = 0;
            bindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            bindingDescription[0].stride = 2 * sizeof(glm::vec3);

            attributeDescription[0].binding = 0;
            attributeDescription[0].location = 0;
            attributeDescription[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescription[0].location = 0;

//        pipelineConfigureInfo.attributeDescriptions.clear();
//        pipelineConfigureInfo.attributeDescriptions.push_back(attributeDescription[0]);
//        pipelineConfigureInfo.vertexBindingDescriptions.clear();
//        pipelineConfigureInfo.vertexBindingDescriptions.push_back(bindingDescription[0]);
            pipelineConfigureInfo.renderPass = renderPass;
            pipelineConfigureInfo.pipelineLayout = pipelineLayout;
            pipeline = m_pipelineLibrary.GetOrCreatePipeline(
                    "Shadow/Pipeline/" + std::to_string(material->getMaterialId()),
                    pipelineConfigureInfo,
                    material);
        }

        //手动编译Shader，此时读取编译后的文件
        //路径是从可执行文件开始的，并非从根目录
        Device &device;
        std::shared_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
        std::shared_ptr<Material> material;
        RenderCore::PipelineLibrary &m_pipelineLibrary;
    };


}
