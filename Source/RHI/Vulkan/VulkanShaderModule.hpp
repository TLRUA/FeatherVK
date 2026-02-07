#pragma once

#include "../../Device.hpp"
#include "../RHIShader.hpp"

namespace FeatherVK::RHI {
    class VulkanShaderModule final : public RHIShaderModule {
    public:
        VulkanShaderModule(FeatherVK::Device &device, std::string path, VkShaderModule shaderModule)
            : m_device(device), m_path(std::move(path)), m_shaderModule(shaderModule) {}

        ~VulkanShaderModule() override;

        BackendType GetBackendType() const override {
            return BackendType::Vulkan;
        }

        const std::string &GetPath() const override {
            return m_path;
        }

        VkShaderModule GetVkShaderModule() const {
            return m_shaderModule;
        }

    private:
        FeatherVK::Device &m_device;
        std::string m_path;
        VkShaderModule m_shaderModule{VK_NULL_HANDLE};
    };
}
