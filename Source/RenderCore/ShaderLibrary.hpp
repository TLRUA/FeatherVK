#pragma once

#include "../Material.hpp"
#include "../ShaderBuilder.h"

namespace FeatherVK::RenderCore {
    class ShaderLibrary {
    public:
        explicit ShaderLibrary(Device &device) : m_shaderBuilder(device) {}

        [[nodiscard]] std::shared_ptr<RHI::RHIShaderModule> Load(const std::string &shaderPath) {
            return m_shaderBuilder.createShaderModule(shaderPath);
        }

        [[nodiscard]] std::shared_ptr<ShaderModule> LoadStage(
            const std::string &shaderPath,
            const ShaderCategory shaderCategory) {
            return std::make_shared<ShaderModule>(Load(shaderPath), shaderCategory);
        }

    private:
        ShaderBuilder m_shaderBuilder;
    };
}
