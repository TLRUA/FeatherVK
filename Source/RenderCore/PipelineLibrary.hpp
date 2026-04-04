#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Descriptor.h"
#include "../Pipeline.hpp"
#include "RenderResourceRegistry.hpp"

namespace FeatherVK::RenderCore {
    class PipelineLibrary {
    public:
        explicit PipelineLibrary(Device &device, RenderResourceRegistry *resourceRegistry = nullptr)
            : m_device(device), m_resourceRegistry(resourceRegistry) {}

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
        RenderResourceRegistry *m_resourceRegistry = nullptr;
        std::unordered_map<std::string, VkPipelineLayout> m_pipelineLayouts{};
        std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_pipelines{};
    };
}
