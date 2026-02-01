#include "VulkanCommandContext.hpp"

#include <stdexcept>

#include "../../Buffer.h"
#include "../../Image.h"

namespace FeatherVK::RHI {
    namespace {
        FeatherVK::Buffer &RequireVulkanBuffer(RHIBuffer &buffer) {
            auto *vulkanBuffer = dynamic_cast<FeatherVK::Buffer *>(&buffer);
            if (vulkanBuffer == nullptr) {
                throw std::runtime_error("RHI buffer is not backed by the Vulkan backend");
            }
            return *vulkanBuffer;
        }

        FeatherVK::Image &RequireVulkanTexture(RHITexture &texture) {
            auto *vulkanTexture = dynamic_cast<FeatherVK::Image *>(&texture);
            if (vulkanTexture == nullptr) {
                throw std::runtime_error("RHI texture is not backed by the Vulkan backend");
            }
            return *vulkanTexture;
        }

        VkImageLayout ToVulkanLayout(TextureLayout layout) {
            switch (layout) {
                case TextureLayout::Undefined:
                    return VK_IMAGE_LAYOUT_UNDEFINED;
                case TextureLayout::TransferSrc:
                    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                case TextureLayout::TransferDst:
                    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                case TextureLayout::ShaderReadOnly:
                    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case TextureLayout::ColorAttachment:
                    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                case TextureLayout::DepthStencilAttachment:
                    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                case TextureLayout::General:
                    return VK_IMAGE_LAYOUT_GENERAL;
                case TextureLayout::Present:
                    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }

            return VK_IMAGE_LAYOUT_UNDEFINED;
        }

        VkImageAspectFlags ToVulkanAspectMask(TextureAspect aspectMask) {
            VkImageAspectFlags flags = 0;
            if (HasAspect(aspectMask, TextureAspect::Color)) {
                flags |= VK_IMAGE_ASPECT_COLOR_BIT;
            }
            if (HasAspect(aspectMask, TextureAspect::Depth)) {
                flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            if (HasAspect(aspectMask, TextureAspect::Stencil)) {
                flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            return flags;
        }
    }

    void VulkanCommandContext::CopyBuffer(RHIBuffer &source, RHIBuffer &destination, uint64_t byteSize) {
        auto &sourceBuffer = RequireVulkanBuffer(source);
        auto &destinationBuffer = RequireVulkanBuffer(destination);
        m_device.copyBuffer(sourceBuffer.getBuffer(), destinationBuffer.getBuffer(), static_cast<VkDeviceSize>(byteSize));
    }

    void VulkanCommandContext::CopyBufferToTexture(RHIBuffer &source,
                                                   RHITexture &destination,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t layerCount) {
        auto &sourceBuffer = RequireVulkanBuffer(source);
        auto &destinationTexture = RequireVulkanTexture(destination);
        m_device.copyBufferToImage(sourceBuffer.getBuffer(), destinationTexture.getImage(), width, height, layerCount);
    }

    void VulkanCommandContext::TransitionTexture(RHITexture &texture,
                                                 TextureLayout oldLayout,
                                                 TextureLayout newLayout,
                                                 const TextureSubresourceRange &subresourceRange) {
        auto &vulkanTexture = RequireVulkanTexture(texture);
        VkImageSubresourceRange vkSubresourceRange{};
        vkSubresourceRange.aspectMask = ToVulkanAspectMask(subresourceRange.aspectMask);
        vkSubresourceRange.baseMipLevel = subresourceRange.baseMipLevel;
        vkSubresourceRange.levelCount = subresourceRange.levelCount;
        vkSubresourceRange.baseArrayLayer = subresourceRange.baseArrayLayer;
        vkSubresourceRange.layerCount = subresourceRange.layerCount;
        m_device.transitionImageLayout(
            vulkanTexture.getImage(),
            ToVulkanLayout(oldLayout),
            ToVulkanLayout(newLayout),
            vkSubresourceRange);
    }
}
