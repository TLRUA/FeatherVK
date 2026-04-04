#include "PipelineLibrary.hpp"

namespace FeatherVK::RenderCore {
    PipelineLibrary::~PipelineLibrary() {
        m_pipelines.clear();
        for (const auto &[_, pipelineLayout]: m_pipelineLayouts) {
            vkDestroyPipelineLayout(m_device.device(), pipelineLayout, nullptr);
        }
    }

    VkPipelineLayout PipelineLibrary::GetOrCreateLayout(
        const std::string &key,
        const std::vector<std::shared_ptr<RHI::RHIBindLayout>> &bindLayouts,
        const std::vector<VkPushConstantRange> &pushConstantRanges) {
        const auto existing = m_pipelineLayouts.find(key);
        if (existing != m_pipelineLayouts.end()) {
            return existing->second;
        }

        const auto descriptorSetLayouts = CollectVkDescriptorSetLayouts(bindLayouts);
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutCreateInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(
                m_device.device(),
                &pipelineLayoutCreateInfo,
                nullptr,
                &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        m_pipelineLayouts.emplace(key, pipelineLayout);
        if (m_resourceRegistry != nullptr) {
            m_resourceRegistry->ImportPipeline(key, nullptr, pipelineLayout);
        }
        return pipelineLayout;
    }

    std::shared_ptr<Pipeline> PipelineLibrary::GetOrCreatePipeline(
        const std::string &key,
        const PipelineConfigureInfo &pipelineConfigureInfo,
        const std::shared_ptr<Material> &material) {
        const auto existing = m_pipelines.find(key);
        if (existing != m_pipelines.end()) {
            return existing->second;
        }

        auto pipeline = std::make_shared<Pipeline>(m_device, pipelineConfigureInfo, material);
        m_pipelines.emplace(key, pipeline);
        if (m_resourceRegistry != nullptr) {
            m_resourceRegistry->ImportPipeline(key, pipeline);
        }
        return pipeline;
    }
}
