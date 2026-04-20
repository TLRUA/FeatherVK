#pragma once

#include <memory>
#include <glm/gtc/constants.hpp>
#include "../Descriptor.h"
#include "../Pipeline.hpp"
#include "../RenderCore/MaterialBindings.hpp"
#include "../RenderCore/PipelineLibrary.hpp"
#include "../RenderGraph/RenderGraph.hpp"
#include "../RenderScene/RenderScene.hpp"
#include "../Device.hpp"
#include "../Model.hpp"
#include "../StructureInfos.h"

namespace FeatherVK {
    struct SimplePushConstantData {
        //按对角线初始化
        glm::mat4 modelMatrix{1.f};
        glm::mat4 normalMatrix{1.f};
        glm::vec4 baseColorMetallic{0.8f, 0.8f, 0.8f, 0.0f};
        glm::vec4 emissiveRoughnessOpacity{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct PointLightPushConstant {
        glm::vec4 position{};
        glm::vec4 color{};
        float radius;
    };

    class RenderSystem {

    public:
        RenderSystem(Device &device,
                     std::optional<RenderGraph::RenderGraphGraphicsPipelineTarget> graphicsPipelineTarget,
                     const std::shared_ptr<Material> material,
                     RenderCore::PipelineLibrary &pipelineLibrary);

        virtual void Init();

        ~RenderSystem();

        RenderSystem(const RenderSystem &) = delete;

        RenderSystem &operator=(const RenderSystem &) = delete;

        virtual void Record(RenderGraph::RenderGraphPassContext &context) {}

        virtual void Record(RenderGraph::RenderGraphPassContext &context, const RenderMeshInstance &meshInstance);


        template<class T>
        void UpdateGlobalUboBuffer(T &globalUbo, uint32_t frameIndex) {
            m_material->getBufferPointers()[0]->writeToIndex(&globalUbo, frameIndex);
            m_material->getBufferPointers()[0]->flushIndex(frameIndex);
        };

        unsigned int GetRenderQueue() const {
            auto _pipelineCategory = m_material->getPipelineCategory();
            return PipelineRenderQueue.at(_pipelineCategory);
        }

        const std::string &GetPipelineCategory() const {
            return m_material->getPipelineCategory();
        }

    protected:
        virtual void createPipeline();

        virtual void createPipelineLayout();

        [[nodiscard]] const RenderGraph::RenderGraphGraphicsPipelineTarget *GetGraphicsPipelineTarget() const {
            return m_graphicsPipelineTarget ? &(*m_graphicsPipelineTarget) : nullptr;
        }

        void BindMaterialResources(FrameInfo &frameInfo);

        //手动编译Shader，此时读取编译后的文件
        //路径是从可执行文件开始的，并非从根目录
        Device &device;
        std::shared_ptr<Pipeline> m_pipeline;
        VkPipelineLayout m_pipelineLayout;
        std::shared_ptr<Material> m_material;
        std::optional<RenderGraph::RenderGraphGraphicsPipelineTarget> m_graphicsPipelineTarget;
        RenderCore::PipelineLibrary &m_pipelineLibrary;

    };


}
