#pragma once

#include "../../Device.hpp"
#include "../RHIDevice.hpp"

namespace FeatherVK::RHI {
    class VulkanCommandContext final : public RHICommandContext {
    public:
        explicit VulkanCommandContext(FeatherVK::Device &device)
            : m_device(device) {}

        BackendType GetBackendType() const override {
            return BackendType::Vulkan;
        }

        void CopyBuffer(RHIBuffer &source, RHIBuffer &destination, uint64_t byteSize) override;
        void CopyBufferToTexture(RHIBuffer &source,
                                 RHITexture &destination,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t layerCount) override;
        void TransitionTexture(RHITexture &texture,
                               TextureLayout oldLayout,
                               TextureLayout newLayout,
                               const TextureSubresourceRange &subresourceRange) override;

    private:
        FeatherVK::Device &m_device;
    };
}
