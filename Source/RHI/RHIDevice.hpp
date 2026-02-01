#pragma once

#include "RHIResources.hpp"

namespace FeatherVK::RHI {
    class RHICommandContext : public RHIObject {
    public:
        ~RHICommandContext() override = default;

        virtual void CopyBuffer(RHIBuffer &source, RHIBuffer &destination, uint64_t byteSize) = 0;
        virtual void CopyBufferToTexture(RHIBuffer &source,
                                         RHITexture &destination,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t layerCount) = 0;
        virtual void TransitionTexture(RHITexture &texture,
                                       TextureLayout oldLayout,
                                       TextureLayout newLayout,
                                       const TextureSubresourceRange &subresourceRange) = 0;
    };

    class RHIDevice : public RHIObject {
    public:
        ~RHIDevice() override = default;

        virtual const char *GetDeviceName() const = 0;
        virtual RHICommandContext &GetImmediateContext() = 0;
    };
}
