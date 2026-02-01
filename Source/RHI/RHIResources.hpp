#pragma once

#include "RHITypes.hpp"

namespace FeatherVK::RHI {
    class RHIObject {
    public:
        virtual ~RHIObject() = default;
        virtual BackendType GetBackendType() const = 0;
    };

    class RHIBuffer : public RHIObject {
    public:
        ~RHIBuffer() override = default;
        virtual const BufferDesc &GetDesc() const = 0;
    };

    class RHITexture : public RHIObject {
    public:
        ~RHITexture() override = default;
        virtual const TextureDesc &GetDesc() const = 0;
    };

    class RHISampler : public RHIObject {
    public:
        ~RHISampler() override = default;
        virtual const SamplerDesc &GetDesc() const = 0;
    };

    class RHIPipelineState : public RHIObject {
    public:
        ~RHIPipelineState() override = default;
        virtual const PipelineDesc &GetDesc() const = 0;
    };
}
