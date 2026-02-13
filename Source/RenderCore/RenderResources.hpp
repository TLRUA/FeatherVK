#pragma once

#include <memory>

#include "../Buffer.h"
#include "../Image.h"
#include "../Sampler.h"

namespace FeatherVK::RenderCore {
    struct BufferResourceView {
        std::shared_ptr<RHI::RHIBuffer> buffer{};

        [[nodiscard]] bool IsValid() const {
            return buffer != nullptr;
        }
    };

    struct TextureResourceView {
        std::shared_ptr<RHI::RHITexture> texture{};
        std::shared_ptr<RHI::RHITextureView> view{};
        std::shared_ptr<RHI::RHISampler> sampler{};

        [[nodiscard]] bool IsValid() const {
            return texture != nullptr && view != nullptr;
        }
    };

    struct RenderTargetView {
        TextureResourceView resource{};
        VkExtent2D extent{};

        [[nodiscard]] bool IsValid() const {
            return resource.IsValid() && extent.width > 0 && extent.height > 0;
        }
    };

    inline BufferResourceView MakeBufferResourceView(const std::shared_ptr<Buffer> &buffer) {
        return {buffer};
    }

    inline TextureResourceView MakeTextureResourceView(
        const std::shared_ptr<Image> &image,
        const std::shared_ptr<Sampler> &sampler = nullptr) {
        return {image, image, sampler};
    }

    inline RenderTargetView MakeRenderTargetView(
        const std::shared_ptr<Image> &image,
        const VkExtent2D extent,
        const std::shared_ptr<Sampler> &sampler = nullptr) {
        return {MakeTextureResourceView(image, sampler), extent};
    }
}
