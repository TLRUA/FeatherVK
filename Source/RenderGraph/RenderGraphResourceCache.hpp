#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../Buffer.h"
#include "../Device.hpp"
#include "../Image.h"
#include "../Sampler.h"
#include "RenderGraph.hpp"

namespace FeatherVK::RenderGraph {
    class RenderGraphResourceCache {
    public:
        explicit RenderGraphResourceCache(Device &device);
        ~RenderGraphResourceCache();

        void SyncSceneResources(VkExtent2D sceneExtent);
        void EnsureShadowResources();

        [[nodiscard]] VkExtent2D GetSceneExtent() const { return m_sceneExtent; }
        [[nodiscard]] VkExtent2D GetPickingExtent() const { return m_pickingExtent; }
        [[nodiscard]] VkExtent2D GetShadowExtent() const { return {ShadowMapResolution, ShadowMapResolution}; }
        [[nodiscard]] uint64_t GetTopologyVersion() const { return m_topologyVersion; }

        [[nodiscard]] const std::shared_ptr<Sampler> &GetSharedSampler() const { return m_sharedSampler; }
        [[nodiscard]] const std::shared_ptr<Sampler> &GetShadowSampler() const { return m_shadowSampler; }

        [[nodiscard]] const std::shared_ptr<Image> &GetSceneColorImage(int index) const { return m_sceneColorImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetRayTracingOutputImage(int index) const { return m_rayTracingOutputImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetWorldPositionImage(int index) const { return m_worldPositionImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetShadowTermImage(int index) const { return m_shadowTermImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetShadowMomentsImage(int index) const { return m_shadowMomentsImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetRayTracingGuideImage(int index) const { return m_rayTracingGuideImages.at(index); }
        [[nodiscard]] const std::shared_ptr<Image> &GetDenoiseAccumulationImage() const { return m_denoiseAccumulationImage; }
        [[nodiscard]] const std::shared_ptr<Image> &GetSceneDepthImage() const { return m_sceneDepthImage; }
        [[nodiscard]] const std::shared_ptr<Image> &GetPickingIdImage() const { return m_pickingIdImage; }
        [[nodiscard]] const std::shared_ptr<Image> &GetPickingDepthImage() const { return m_pickingDepthImage; }
        [[nodiscard]] const std::shared_ptr<Image> &GetShadowImage() const { return m_shadowImage; }

        [[nodiscard]] std::shared_ptr<VkDescriptorImageInfo> GetShadowImageInfo() const;

        [[nodiscard]] VkRenderPass GetSceneColorRenderPass() const { return m_sceneColorRenderPass; }
        [[nodiscard]] VkRenderPass GetPickingRenderPass() const { return m_pickingRenderPass; }
        [[nodiscard]] VkRenderPass GetShadowRenderPass() const { return m_shadowRenderPass; }

        [[nodiscard]] VkFramebuffer GetSceneColorFramebuffer(uint32_t imageIndex) const { return m_sceneColorFramebuffers.at(imageIndex); }
        [[nodiscard]] VkFramebuffer GetPickingFramebuffer() const { return m_pickingFramebuffer; }
        [[nodiscard]] VkFramebuffer GetShadowFramebuffer() const { return m_shadowFramebuffer; }

        [[nodiscard]] const RenderGraphGraphicsPassSignature &GetSceneColorPassSignature() const { return m_sceneColorPassSignature; }
        [[nodiscard]] const RenderGraphGraphicsPassSignature &GetPickingPassSignature() const { return m_pickingPassSignature; }
        [[nodiscard]] const RenderGraphGraphicsPassSignature &GetShadowPassSignature() const { return m_shadowPassSignature; }
        [[nodiscard]] RenderGraphGraphicsPipelineTarget GetSceneColorPipelineTarget() const {
            return {m_sceneColorPassSignature, m_sceneColorRenderPass};
        }
        [[nodiscard]] RenderGraphGraphicsPipelineTarget GetPickingPipelineTarget() const {
            return {m_pickingPassSignature, m_pickingRenderPass};
        }
        [[nodiscard]] RenderGraphGraphicsPipelineTarget GetShadowPipelineTarget() const {
            return {m_shadowPassSignature, m_shadowRenderPass};
        }

        void MarkPickingDataReady() { m_hasPickingData = true; }
        void ResetPickingData() { m_hasPickingData = false; }
        [[nodiscard]] bool HasPickingData() const { return m_hasPickingData; }

        int32_t ReadPickingObjectId(uint32_t pixelX, uint32_t pixelY);

    private:
        static constexpr uint32_t ShadowMapResolution = 1024;

        void UpdatePassSignatures();
        void ReleaseSceneResources();
        void ReleasePickingResources();
        void ReleaseShadowResources();
        void CreateSceneResources();
        void CreatePickingResources();
        void CreateShadowResources();
        void CreateSceneColorRenderPassAndFramebuffers();
        void TransitionSceneImagesToDefaultLayouts();
        void CreateImagePair(std::vector<std::shared_ptr<Image>> &images,
                             const VkImageCreateInfo &imageCreateInfo,
                             VkFormat format,
                             Sampler &sampler);
        void CreateImage(Image &image,
                         const VkImageCreateInfo &imageCreateInfo,
                         VkFormat format,
                         VkImageAspectFlags aspectMask,
                         Sampler &sampler);
        void CreateImage(Image &image,
                         const VkImageCreateInfo &imageCreateInfo,
                         VkFormat format,
                         VkImageAspectFlags aspectMask);

        Device &m_device;
        VkExtent2D m_sceneExtent{};
        VkExtent2D m_pickingExtent{};
        bool m_hasPickingData{false};
        uint64_t m_topologyVersion{1};

        VkFormat m_offscreenColorFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
        VkFormat m_worldPosColorFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
        VkFormat m_offscreenDepthFormat{VK_FORMAT_D32_SFLOAT};
        VkFormat m_pickingIdFormat{VK_FORMAT_R32_SINT};
        VkFormat m_pickingDepthFormat{VK_FORMAT_D32_SFLOAT};

        std::shared_ptr<Sampler> m_sharedSampler{};
        std::shared_ptr<Sampler> m_shadowSampler{};

        std::vector<std::shared_ptr<Image>> m_sceneColorImages{};
        std::vector<std::shared_ptr<Image>> m_rayTracingOutputImages{};
        std::vector<std::shared_ptr<Image>> m_worldPositionImages{};
        std::vector<std::shared_ptr<Image>> m_shadowTermImages{};
        std::vector<std::shared_ptr<Image>> m_shadowMomentsImages{};
        std::vector<std::shared_ptr<Image>> m_rayTracingGuideImages{};
        std::shared_ptr<Image> m_denoiseAccumulationImage{};
        std::shared_ptr<Image> m_sceneDepthImage{};

        std::shared_ptr<Image> m_shadowImage{};
        VkRenderPass m_shadowRenderPass{VK_NULL_HANDLE};
        VkFramebuffer m_shadowFramebuffer{VK_NULL_HANDLE};

        std::shared_ptr<Image> m_pickingIdImage{};
        std::shared_ptr<Image> m_pickingDepthImage{};
        std::shared_ptr<Buffer> m_pickingReadbackBuffer{};
        VkRenderPass m_pickingRenderPass{VK_NULL_HANDLE};
        VkFramebuffer m_pickingFramebuffer{VK_NULL_HANDLE};

        VkRenderPass m_sceneColorRenderPass{VK_NULL_HANDLE};
        std::vector<VkFramebuffer> m_sceneColorFramebuffers{};

        RenderGraphGraphicsPassSignature m_sceneColorPassSignature{};
        RenderGraphGraphicsPassSignature m_pickingPassSignature{};
        RenderGraphGraphicsPassSignature m_shadowPassSignature{};
    };

    inline RenderGraphResourceCache::RenderGraphResourceCache(Device &device)
        : m_device(device) {
        m_pickingDepthFormat = m_device.findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        UpdatePassSignatures();
    }

    inline RenderGraphResourceCache::~RenderGraphResourceCache() {
        ReleaseSceneResources();
        ReleasePickingResources();
        ReleaseShadowResources();
    }

    inline void RenderGraphResourceCache::SyncSceneResources(VkExtent2D sceneExtent) {
        if (sceneExtent.width == 0 || sceneExtent.height == 0) {
            return;
        }
        if (m_sceneExtent.width == sceneExtent.width &&
            m_sceneExtent.height == sceneExtent.height &&
            !m_sceneColorImages.empty() &&
            m_pickingExtent.width == sceneExtent.width &&
            m_pickingExtent.height == sceneExtent.height) {
            return;
        }

        vkDeviceWaitIdle(m_device.device());
        m_sceneExtent = sceneExtent;
        ReleasePickingResources();
        ReleaseSceneResources();
        CreateSceneResources();
        CreatePickingResources();
        UpdatePassSignatures();
        ++m_topologyVersion;
    }

    inline void RenderGraphResourceCache::EnsureShadowResources() {
        if (m_shadowImage != nullptr &&
            m_shadowRenderPass != VK_NULL_HANDLE &&
            m_shadowFramebuffer != VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(m_device.device());
        ReleaseShadowResources();
        CreateShadowResources();
        UpdatePassSignatures();
        ++m_topologyVersion;
    }

    inline std::shared_ptr<VkDescriptorImageInfo> RenderGraphResourceCache::GetShadowImageInfo() const {
        return m_shadowImage == nullptr || m_shadowSampler == nullptr
                   ? nullptr
                   : m_shadowImage->descriptorInfo(*m_shadowSampler);
    }

    inline void RenderGraphResourceCache::UpdatePassSignatures() {
        m_sceneColorPassSignature = {
            "SceneColor/" + std::to_string(m_offscreenColorFormat) + "/" + std::to_string(m_offscreenDepthFormat),
            {m_offscreenColorFormat},
            m_offscreenDepthFormat,
            VK_SAMPLE_COUNT_1_BIT};
        m_pickingPassSignature = {
            "Picking/" + std::to_string(m_pickingIdFormat) + "/" + std::to_string(m_pickingDepthFormat),
            {m_pickingIdFormat},
            m_pickingDepthFormat,
            VK_SAMPLE_COUNT_1_BIT};
        m_shadowPassSignature = {
            "Shadow/" + std::to_string(VK_FORMAT_D32_SFLOAT),
            {},
            VK_FORMAT_D32_SFLOAT,
            VK_SAMPLE_COUNT_1_BIT};
    }

    inline void RenderGraphResourceCache::ReleaseSceneResources() {
        for (auto framebuffer: m_sceneColorFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device.device(), framebuffer, nullptr);
            }
        }
        m_sceneColorFramebuffers.clear();
        if (m_sceneColorRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device.device(), m_sceneColorRenderPass, nullptr);
            m_sceneColorRenderPass = VK_NULL_HANDLE;
        }

        m_rayTracingOutputImages.clear();
        m_sceneColorImages.clear();
        m_shadowTermImages.clear();
        m_shadowMomentsImages.clear();
        m_worldPositionImages.clear();
        m_rayTracingGuideImages.clear();
        m_denoiseAccumulationImage.reset();
        m_sceneDepthImage.reset();
        m_sharedSampler.reset();
    }

    inline void RenderGraphResourceCache::ReleasePickingResources() {
        if (m_pickingFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device.device(), m_pickingFramebuffer, nullptr);
            m_pickingFramebuffer = VK_NULL_HANDLE;
        }
        if (m_pickingRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device.device(), m_pickingRenderPass, nullptr);
            m_pickingRenderPass = VK_NULL_HANDLE;
        }

        m_pickingIdImage.reset();
        m_pickingDepthImage.reset();
        m_pickingReadbackBuffer.reset();
        m_pickingExtent = {0, 0};
        m_hasPickingData = false;
    }

    inline void RenderGraphResourceCache::ReleaseShadowResources() {
        if (m_shadowFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device.device(), m_shadowFramebuffer, nullptr);
            m_shadowFramebuffer = VK_NULL_HANDLE;
        }
        if (m_shadowRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device.device(), m_shadowRenderPass, nullptr);
            m_shadowRenderPass = VK_NULL_HANDLE;
        }

        m_shadowImage.reset();
        m_shadowSampler.reset();
    }

    inline void RenderGraphResourceCache::CreateSceneResources() {
        m_sharedSampler = std::make_shared<Sampler>(m_device);
        m_sharedSampler->createTextureSampler();

        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        imageCreateInfo.format = m_offscreenColorFormat;
        imageCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.extent = {m_sceneExtent.width, m_sceneExtent.height, 1};

        m_rayTracingOutputImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_rayTracingOutputImages, imageCreateInfo, m_offscreenColorFormat, *m_sharedSampler);

        VkImageCreateInfo sceneColorCreateInfo = imageCreateInfo;
        sceneColorCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        m_sceneColorImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_sceneColorImages, sceneColorCreateInfo, m_offscreenColorFormat, *m_sharedSampler);

        m_shadowTermImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_shadowTermImages, imageCreateInfo, m_offscreenColorFormat, *m_sharedSampler);

        m_shadowMomentsImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_shadowMomentsImages, imageCreateInfo, m_offscreenColorFormat, *m_sharedSampler);

        VkImageCreateInfo worldPositionCreateInfo = imageCreateInfo;
        worldPositionCreateInfo.format = m_worldPosColorFormat;
        m_worldPositionImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_worldPositionImages, worldPositionCreateInfo, m_worldPosColorFormat, *m_sharedSampler);

        m_rayTracingGuideImages = {std::make_shared<Image>(m_device), std::make_shared<Image>(m_device)};
        CreateImagePair(m_rayTracingGuideImages, worldPositionCreateInfo, m_worldPosColorFormat, *m_sharedSampler);

        m_denoiseAccumulationImage = std::make_shared<Image>(m_device);
        CreateImage(*m_denoiseAccumulationImage, imageCreateInfo, m_offscreenColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, *m_sharedSampler);

        VkImageCreateInfo depthCreateInfo{};
        Image::setDefaultImageCreateInfo(depthCreateInfo);
        depthCreateInfo.format = m_offscreenDepthFormat;
        depthCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthCreateInfo.extent = {m_sceneExtent.width, m_sceneExtent.height, 1};
        m_sceneDepthImage = std::make_shared<Image>(m_device);
        CreateImage(*m_sceneDepthImage, depthCreateInfo, m_offscreenDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        TransitionSceneImagesToDefaultLayouts();
        CreateSceneColorRenderPassAndFramebuffers();
    }

    inline void RenderGraphResourceCache::CreatePickingResources() {
        if (m_sceneExtent.width == 0 || m_sceneExtent.height == 0) {
            return;
        }

        m_pickingExtent = m_sceneExtent;

        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        imageCreateInfo.extent = {m_pickingExtent.width, m_pickingExtent.height, 1};

        m_pickingIdImage = std::make_shared<Image>(m_device);
        imageCreateInfo.format = m_pickingIdFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        CreateImage(*m_pickingIdImage, imageCreateInfo, m_pickingIdFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        m_pickingDepthImage = std::make_shared<Image>(m_device);
        imageCreateInfo.format = m_pickingDepthFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        CreateImage(*m_pickingDepthImage, imageCreateInfo, m_pickingDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        VkAttachmentDescription attachmentDescriptions[2]{};
        attachmentDescriptions[0].format = m_pickingIdFormat;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachmentDescriptions[1].format = m_pickingDepthFormat;
        attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorAttachmentRef;
        subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 2;
        renderPassCreateInfo.pAttachments = attachmentDescriptions;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpassDescription;
        renderPassCreateInfo.dependencyCount = 2;
        renderPassCreateInfo.pDependencies = dependencies;

        if (vkCreateRenderPass(m_device.device(), &renderPassCreateInfo, nullptr, &m_pickingRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create editor picking render pass");
        }

        const std::array<VkImageView, 2> attachments{
            *m_pickingIdImage->getImageView(),
            *m_pickingDepthImage->getImageView()
        };

        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_pickingRenderPass;
        framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferCreateInfo.pAttachments = attachments.data();
        framebufferCreateInfo.width = m_pickingExtent.width;
        framebufferCreateInfo.height = m_pickingExtent.height;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(m_device.device(), &framebufferCreateInfo, nullptr, &m_pickingFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create editor picking framebuffer");
        }

        m_pickingReadbackBuffer = std::make_shared<Buffer>(
            m_device,
            sizeof(int32_t),
            1,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_pickingReadbackBuffer->map();
        m_hasPickingData = false;
    }

    inline void RenderGraphResourceCache::CreateShadowResources() {
        m_shadowImage = std::make_shared<Image>(m_device);
        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        imageCreateInfo.arrayLayers = 6;
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        imageCreateInfo.extent = {ShadowMapResolution, ShadowMapResolution, 1};
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        m_shadowImage->createImage(imageCreateInfo);

        VkImageViewCreateInfo imageViewCreateInfo{};
        m_shadowImage->setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.subresourceRange.layerCount = 6;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        imageViewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        imageViewCreateInfo.components = {
            VK_COMPONENT_SWIZZLE_R,
            VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A};
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        m_shadowImage->createImageView(imageViewCreateInfo);

        m_shadowSampler = std::make_shared<Sampler>(m_device);
        m_shadowSampler->createTextureSampler();

        VkAttachmentDescription attachmentDescription{};
        attachmentDescription.format = VK_FORMAT_D32_SFLOAT;
        attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 0;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;

        VkRenderPassMultiviewCreateInfo multiviewCreateInfo{};
        multiviewCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
        multiviewCreateInfo.subpassCount = 1;
        uint32_t viewMask = 0b00111111;
        multiviewCreateInfo.pViewMasks = &viewMask;

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.pNext = &multiviewCreateInfo;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &attachmentDescription;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpassDescription;

        if (vkCreateRenderPass(m_device.device(), &renderPassCreateInfo, nullptr, &m_shadowRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW render pass");
        }

        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_shadowRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = m_shadowImage->getImageView();
        framebufferCreateInfo.width = ShadowMapResolution;
        framebufferCreateInfo.height = ShadowMapResolution;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(m_device.device(), &framebufferCreateInfo, nullptr, &m_shadowFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW framebuffer");
        }
    }

    inline void RenderGraphResourceCache::CreateSceneColorRenderPassAndFramebuffers() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_offscreenColorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_offscreenDepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        const std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(m_device.device(), &renderPassInfo, nullptr, &m_sceneColorRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene color render pass");
        }

        m_sceneColorFramebuffers.resize(m_sceneColorImages.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < m_sceneColorImages.size(); ++i) {
            const std::array<VkImageView, 2> attachmentsForFramebuffer{
                *m_sceneColorImages[i]->getImageView(),
                *m_sceneDepthImage->getImageView()
            };

            VkFramebufferCreateInfo framebufferCreateInfo{};
            framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCreateInfo.renderPass = m_sceneColorRenderPass;
            framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentsForFramebuffer.size());
            framebufferCreateInfo.pAttachments = attachmentsForFramebuffer.data();
            framebufferCreateInfo.width = m_sceneExtent.width;
            framebufferCreateInfo.height = m_sceneExtent.height;
            framebufferCreateInfo.layers = 1;

            if (vkCreateFramebuffer(m_device.device(), &framebufferCreateInfo, nullptr, &m_sceneColorFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene color framebuffer");
            }
        }
    }

    inline void RenderGraphResourceCache::TransitionSceneImagesToDefaultLayouts() {
        const VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (const auto &image: m_rayTracingOutputImages) {
            m_device.transitionImageLayout(image->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, colorRange);
        }
        for (const auto &image: m_shadowTermImages) {
            m_device.transitionImageLayout(image->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, colorRange);
        }
        for (const auto &image: m_shadowMomentsImages) {
            m_device.transitionImageLayout(image->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, colorRange);
        }
        for (const auto &image: m_worldPositionImages) {
            m_device.transitionImageLayout(image->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, colorRange);
        }
        for (const auto &image: m_rayTracingGuideImages) {
            m_device.transitionImageLayout(image->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, colorRange);
        }
        m_device.transitionImageLayout(
            m_denoiseAccumulationImage->getImage(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            colorRange);

        for (const auto &image: m_sceneColorImages) {
            m_device.transitionImageLayout(
                image->getImage(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                colorRange);
        }

        m_device.transitionImageLayout(
            m_sceneDepthImage->getImage(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});
    }

    inline void RenderGraphResourceCache::CreateImagePair(std::vector<std::shared_ptr<Image>> &images,
                                                          const VkImageCreateInfo &imageCreateInfo,
                                                          VkFormat format,
                                                          Sampler &sampler) {
        for (auto &image: images) {
            CreateImage(*image, imageCreateInfo, format, VK_IMAGE_ASPECT_COLOR_BIT, sampler);
        }
    }

    inline void RenderGraphResourceCache::CreateImage(Image &image,
                                                      const VkImageCreateInfo &imageCreateInfo,
                                                      VkFormat format,
                                                      VkImageAspectFlags aspectMask,
                                                      Sampler &sampler) {
        CreateImage(image, imageCreateInfo, format, aspectMask);
        image.sampler = sampler.getSampler();
    }

    inline void RenderGraphResourceCache::CreateImage(Image &image,
                                                      const VkImageCreateInfo &imageCreateInfo,
                                                      VkFormat format,
                                                      VkImageAspectFlags aspectMask) {
        image.createImage(imageCreateInfo);
        VkImageViewCreateInfo imageViewCreateInfo{};
        image.setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.format = format;
        imageViewCreateInfo.subresourceRange = {aspectMask, 0, 1, 0, 1};
        image.createImageView(imageViewCreateInfo);
    }

    inline int32_t RenderGraphResourceCache::ReadPickingObjectId(uint32_t pixelX, uint32_t pixelY) {
        if (!m_hasPickingData || m_pickingIdImage == nullptr || m_pickingReadbackBuffer == nullptr ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return -1;
        }

        if (pixelX >= m_pickingExtent.width || pixelY >= m_pickingExtent.height) {
            return -1;
        }

        VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

        VkImageMemoryBarrier toTransferBarrier{};
        toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferBarrier.image = m_pickingIdImage->getImage();
        toTransferBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransferBarrier);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {static_cast<int32_t>(pixelX), static_cast<int32_t>(pixelY), 0};
        copyRegion.imageExtent = {1, 1, 1};

        vkCmdCopyImageToBuffer(
            commandBuffer,
            m_pickingIdImage->getImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_pickingReadbackBuffer->getBuffer(),
            1,
            &copyRegion);

        VkImageMemoryBarrier toColorAttachmentBarrier{};
        toColorAttachmentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachmentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toColorAttachmentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toColorAttachmentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toColorAttachmentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachmentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachmentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachmentBarrier.image = m_pickingIdImage->getImage();
        toColorAttachmentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toColorAttachmentBarrier);

        m_device.endSingleTimeCommands(commandBuffer, "read_picking_object_id");

        const auto *mapped = static_cast<int32_t *>(m_pickingReadbackBuffer->getMappedMemory());
        return mapped == nullptr ? -1 : mapped[0];
    }
}
