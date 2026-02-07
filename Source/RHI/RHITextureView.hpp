#pragma once

#include "RHIResources.hpp"

namespace FeatherVK::RHI {
    class RHITextureView {
    public:
        virtual ~RHITextureView() = default;

        virtual BackendType GetBackendType() const = 0;
        virtual const TextureViewDesc &GetViewDesc() const = 0;
        virtual const RHITexture &GetTexture() const = 0;
    };
}
