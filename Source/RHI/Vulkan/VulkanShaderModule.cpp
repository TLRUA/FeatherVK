#include "VulkanShaderModule.hpp"

namespace FeatherVK::RHI {
    VulkanShaderModule::~VulkanShaderModule() {
        if (m_shaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device.device(), m_shaderModule, nullptr);
            m_shaderModule = VK_NULL_HANDLE;
        }
    }
}
