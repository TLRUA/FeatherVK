#pragma once

#include "../Material.hpp"
#include "../ShaderBuilder.h"
#include "RenderResourceRegistry.hpp"

namespace FeatherVK::RenderCore {
    class ShaderLibrary {
    public:
        explicit ShaderLibrary(Device &device, RenderResourceRegistry *resourceRegistry = nullptr)
            : m_shaderBuilder(device), m_resourceRegistry(resourceRegistry) {}

        [[nodiscard]] std::shared_ptr<RHI::RHIShaderModule> Load(const std::string &shaderPath) {
            return m_shaderBuilder.createShaderModule(shaderPath);
        }

        [[nodiscard]] std::shared_ptr<ShaderModule> LoadStage(
            const std::string &shaderPath,
            const ShaderCategory shaderCategory) {
            auto shaderModule = Load(shaderPath);
            if (m_resourceRegistry != nullptr) {
                m_resourceRegistry->ImportShader(
                    "Shader/" + shaderPath,
                    shaderPath,
                    RenderResourceRegistry::ToRhiShaderStage(shaderCategory),
                    shaderModule);
            }
            return std::make_shared<ShaderModule>(std::move(shaderModule), shaderCategory);
        }

    private:
        ShaderBuilder m_shaderBuilder;
        RenderResourceRegistry *m_resourceRegistry = nullptr;
    };
}
