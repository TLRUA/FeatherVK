#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Descriptor.h"
#include "../Pipeline.hpp"

namespace FeatherVK::RenderCore {
    class PipelineLibrary {
    public:
        explicit PipelineLibrary(Device &device) : m_device(device) {}

        ~PipelineLibrary();

        VkPipelineLayout GetOrCreateLayout(
            const std::string &key,
            const std::vector<std::shared_ptr<RHI::RHIBindLayout>> &bindLayouts,
            const std::vector<VkPushConstantRange> &pushConstantRanges = {});

        std::shared_ptr<Pipeline> GetOrCreatePipeline(
            const std::string &key,
            const PipelineConfigureInfo &pipelineConfigureInfo,
            const std::shared_ptr<Material> &material);

    private:
        Device &m_device;
        std::unordered_map<std::string, VkPipelineLayout> m_pipelineLayouts{};
        std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_pipelines{};
    };
}
