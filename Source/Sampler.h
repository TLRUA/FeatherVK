#pragma once

#include <vulkan/vulkan.h>
#include "Device.hpp"
#include "RHI/RHIResources.hpp"

namespace FeatherVK {
    class Sampler : public RHI::RHISampler {
    public:
        //Todo: Make a sampler pool
        Sampler(Device &device) : device{device} {};

        ~Sampler();

        void createTextureSampler();

        void createTextureSampler(VkSamplerCreateInfo createInfo);

        void setDefaultSamplerCreateInfo(VkSamplerCreateInfo &createInfo) const;

        VkSampler getSampler() const {
            return sampler;
        }

        RHI::BackendType GetBackendType() const override { return RHI::BackendType::Vulkan; }

        const RHI::SamplerDesc &GetDesc() const override { return m_rhiDesc; }

    private:
        VkSampler sampler = VK_NULL_HANDLE;
        Device &device;
        RHI::SamplerDesc m_rhiDesc{};
    };

}
