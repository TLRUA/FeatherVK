#include <glm/fwd.hpp>
#include <glm/ext/matrix_transform.hpp>
#include "Renderer.h"
#include "Image.h"


namespace Kaamoo {

    Renderer::Renderer(MyWindow &window, Device &device1) : myWindow{window}, device{device1} {

        recreateSwapChain();
        createCommandBuffers();
        loadOffscreenResources();
#ifndef RAY_TRACING
        loadShadow();
#endif
    }


    Renderer::~Renderer() {
        freeCommandBuffers();
        freeShadowResources();
        freePickingResources();
        if (m_pickingRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device(), m_pickingRenderPass, nullptr);
            m_pickingRenderPass = VK_NULL_HANDLE;
        }
        freeOffscreenResources();
    }

    VkCommandBuffer Renderer::beginFrame() {
        assert(!isFrameStarted && "Frame has already started");
        auto result = swapChain->acquireNextImage(&currentImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return nullptr;
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image");
        }

        isFrameStarted = true;

        auto commandBuffer = getCurrentCommandBuffer();

        VkCommandBufferBeginInfo commandBufferBeginInfo{};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin command buffer");
        }
        return commandBuffer;
    }

    void Renderer::endFrame() {
        assert(isFrameStarted && "Can not call endFrame while frame is not in progress");
        auto commandBuffer = getCurrentCommandBuffer();
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }

        auto result = swapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || myWindow.isWindowResized()) {
            myWindow.resetWindowResizedFlag();
            recreateSwapChain();
//            loadOffscreenResources();
//            loadShadow();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image");
        }
        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::recreateSwapChain() {
        auto extent = myWindow.getCurrentExtent();
        while (extent.width == 0 || extent.height == 0) {
            extent = myWindow.getCurrentExtent();
            glfwWaitEvents();
        }
        //�ȴ��߼��豸�е������������ִ�����
        vkDeviceWaitIdle(device.device());

        if (swapChain == nullptr) {
            swapChain = std::make_unique<SwapChain>(device, extent);
        } else {
            std::shared_ptr<SwapChain> oldSwapChain = std::move(swapChain);

            swapChain = std::make_unique<SwapChain>(device, extent, oldSwapChain);

            if (!oldSwapChain->compareSwapFormats(*swapChain)) {
                throw std::runtime_error("Swap chain's image or depth format has changed");
            }

        }

        loadPickingResources();
    }

    void Renderer::freeCommandBuffers() {
        //Ϊʲô��Ҫ��ʽ�Ĵ���size�����ڰ�ȫ�Կ��ǣ�������ʽ��������
        vkFreeCommandBuffers(device.device(), device.getCommandPool(), static_cast<uint32_t>(commandBuffers.size()),
                             commandBuffers.data());
        commandBuffers.clear();
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin renderShadow pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = swapChain->getRenderPass();
        renderPassBeginInfo.framebuffer = swapChain->getFrameBuffer(currentImageIndex);

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();

        VkClearValue clearValues[2];
        clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(2);
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = UI_LEFT_WIDTH + UI_LEFT_WIDTH_2;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(myWindow.getCurrentSceneExtent().width);
        viewport.height = static_cast<float>(myWindow.getCurrentSceneExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChain->getSwapChainExtent();

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::beginGizmosRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginGizmosRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() && "Cannot begin render gizmos pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = swapChain->getGizmosRenderPass();
        renderPassBeginInfo.framebuffer = swapChain->getFrameBuffer(currentImageIndex);

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();

        VkClearValue clearValues[2];
        clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(2);
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = UI_LEFT_WIDTH + UI_LEFT_WIDTH_2;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(myWindow.getCurrentSceneExtent().width);
        viewport.height = static_cast<float>(myWindow.getCurrentSceneExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChain->getSwapChainExtent();

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::beginPickingRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginPickingRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin render picking pass on command buffer from a different frame");

        if (m_pickingRenderPass == VK_NULL_HANDLE || m_pickingFramebuffer == VK_NULL_HANDLE ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return;
        }

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_pickingRenderPass;
        renderPassBeginInfo.framebuffer = m_pickingFramebuffer;
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = m_pickingExtent;

        VkClearValue clearValues[2]{};
        clearValues[0].color.int32[0] = -1;
        clearValues[0].color.int32[1] = 0;
        clearValues[0].color.int32[2] = 0;
        clearValues[0].color.int32[3] = 0;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_pickingExtent.width);
        viewport.height = static_cast<float>(m_pickingExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_pickingExtent;

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }
    void Renderer::beginShadowRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin renderShadow pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = shadowRenderPass;
        renderPassBeginInfo.framebuffer = shadowFrameBuffer;

        renderPassBeginInfo.renderArea.offset = {0, 0};
//        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();
        renderPassBeginInfo.renderArea.extent.width = ShadowMapResolution;
        renderPassBeginInfo.renderArea.extent.height = ShadowMapResolution;

        VkClearValue clearValue{};
        clearValue.depthStencil.depth = 1;
        clearValue.depthStencil.stencil = 0;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
//        viewport.m_windowWidth = static_cast<float>(m_window.getCurrentExtent().m_windowWidth);
//        viewport.m_windowHeight = static_cast<float>(m_window.getCurrentExtent().m_windowHeight);
        viewport.width = static_cast<float>(ShadowMapResolution);
        viewport.height = static_cast<float>(ShadowMapResolution);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
//        scissor.extent = swapChain->getSwapChainExtent();
        scissor.extent.width = ShadowMapResolution;
        scissor.extent.height = ShadowMapResolution;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endPickingRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endPickingRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot end picking render pass on command buffer from a different frame");

        if (m_pickingRenderPass == VK_NULL_HANDLE || m_pickingFramebuffer == VK_NULL_HANDLE ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return;
        }

        vkCmdEndRenderPass(commandBuffer);
        m_hasPickingData = true;
    }
    void Renderer::endShadowRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot end renderShadow pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endSwapChainRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() && "Cannot endSwapChainRenderPass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::endGizmosRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endGizmosRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot endGizmosRenderPass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::createCommandBuffers() {

        VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.pNext = nullptr;
        commandBufferAllocateInfo.commandPool = device.getCommandPool();
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = SwapChain::MAX_FRAMES_IN_FLIGHT;

        commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateCommandBuffers(device.device(), &commandBufferAllocateInfo, commandBuffers.data()) !=
            VK_SUCCESS) {
            throw std::runtime_error("Cannot allocate command buffers");
        }
    }

    void Renderer::freeShadowResources() {
        if (shadowRenderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device.device(), shadowRenderPass, nullptr);
        if (shadowFrameBuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device.device(), shadowFrameBuffer, nullptr);
    }

    void Renderer::loadShadow() {
        freeShadowResources();

        shadowImage = std::make_shared<Image>(device);
        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        VkExtent3D shadowMapExtent{};
//        shadowMapExtent.m_windowHeight = swapChain->getSwapChainExtent().m_windowHeight;
//        shadowMapExtent.m_windowWidth = swapChain->getSwapChainExtent().m_windowWidth;
        shadowMapExtent.height = ShadowMapResolution;
        shadowMapExtent.width = ShadowMapResolution;

        shadowMapExtent.depth = 1;
        if (isCubeMap) {
            imageCreateInfo.arrayLayers = 6;
            imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }
        imageCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        imageCreateInfo.extent = shadowMapExtent;
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        shadowImage->createImage(imageCreateInfo);

        //create shadow image view
        auto imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
        shadowImage->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
        if (isCubeMap) {
            imageViewCreateInfo->subresourceRange.layerCount = 6;
            imageViewCreateInfo->viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        }
        imageViewCreateInfo->format = VK_FORMAT_D32_SFLOAT;
        imageViewCreateInfo->components.r = VK_COMPONENT_SWIZZLE_R;
        imageViewCreateInfo->components.g = VK_COMPONENT_SWIZZLE_G;
        imageViewCreateInfo->components.b = VK_COMPONENT_SWIZZLE_B;
        imageViewCreateInfo->components.a = VK_COMPONENT_SWIZZLE_A;
        imageViewCreateInfo->subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        shadowImage->createImageView(*imageViewCreateInfo);

        //create shadow sampler
        shadowSampler = std::make_shared<Sampler>(device);
        shadowSampler->createTextureSampler();

        //create shadow pass
        VkAttachmentDescription attachmentDescriptions[2];
        attachmentDescriptions[0].format = VK_FORMAT_D32_SFLOAT;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[0].flags = 0;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 0;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription[1];
        subpassDescription[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription[0].flags = 0;
        subpassDescription[0].inputAttachmentCount = 0;
        subpassDescription[0].pInputAttachments = nullptr;
        subpassDescription[0].colorAttachmentCount = 0;
        subpassDescription[0].pColorAttachments = nullptr;
        subpassDescription[0].pResolveAttachments = nullptr;
        subpassDescription[0].pDepthStencilAttachment = &depthAttachmentRef;
        subpassDescription[0].preserveAttachmentCount = 0;
        subpassDescription[0].pPreserveAttachments = nullptr;

        VkRenderPassCreateInfo renderPassCreateInfo{};

        VkRenderPassMultiviewCreateInfo multiviewCreateInfo{};
        multiviewCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
        multiviewCreateInfo.subpassCount = 1;
        //All 6 faces of the cube map
        uint32_t viewMask = 0b00111111;
        multiviewCreateInfo.pViewMasks = &viewMask;
        multiviewCreateInfo.correlationMaskCount = 0;
        multiviewCreateInfo.pCorrelationMasks = nullptr;

        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.pNext = &multiviewCreateInfo;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = attachmentDescriptions;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = subpassDescription;
        renderPassCreateInfo.dependencyCount = 0;
        renderPassCreateInfo.pDependencies = nullptr;
        renderPassCreateInfo.flags = 0;

        if (vkCreateRenderPass(device.device(), &renderPassCreateInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW renderShadow pass");
        }

        //create frame buffer
        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.pNext = nullptr;
        framebufferCreateInfo.renderPass = shadowRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = shadowImage->getImageView();
//        framebufferCreateInfo.m_windowWidth = swapChain->getSwapChainExtent().m_windowWidth;
//        framebufferCreateInfo.m_windowHeight = swapChain->getSwapChainExtent().m_windowHeight;

        framebufferCreateInfo.width = ShadowMapResolution;
        framebufferCreateInfo.height = ShadowMapResolution;

        framebufferCreateInfo.layers = 1;
        framebufferCreateInfo.flags = 0;

        if (vkCreateFramebuffer(device.device(), &framebufferCreateInfo, nullptr, &shadowFrameBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW frame buffer");
        }
    }

    void Renderer::freePickingResources() {
        if (m_pickingFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.device(), m_pickingFramebuffer, nullptr);
            m_pickingFramebuffer = VK_NULL_HANDLE;
        }

        m_pickingIdImage.reset();
        m_pickingDepthImage.reset();
        m_pickingReadbackBuffer.reset();
        m_pickingExtent = {0, 0};
        m_hasPickingData = false;
    }

    void Renderer::loadPickingResources() {
        freePickingResources();

        const auto sceneExtent = myWindow.getCurrentSceneExtent();
        if (sceneExtent.width == 0 || sceneExtent.height == 0) {
            return;
        }

        m_pickingExtent = sceneExtent;
        pickingDepthFormat = device.findSupportedFormat(
                {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT},
                VK_IMAGE_TILING_OPTIMAL,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        imageCreateInfo.extent = {m_pickingExtent.width, m_pickingExtent.height, 1};

        m_pickingIdImage = std::make_shared<Image>(device);
        imageCreateInfo.format = pickingIdFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        m_pickingIdImage->createImage(imageCreateInfo);

        VkImageViewCreateInfo imageViewCreateInfo{};
        m_pickingIdImage->setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.format = pickingIdFormat;
        imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_pickingIdImage->createImageView(imageViewCreateInfo);

        m_pickingDepthImage = std::make_shared<Image>(device);
        imageCreateInfo.format = pickingDepthFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        m_pickingDepthImage->createImage(imageCreateInfo);

        m_pickingDepthImage->setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.format = pickingDepthFormat;
        imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        m_pickingDepthImage->createImageView(imageViewCreateInfo);

        VkAttachmentDescription attachmentDescriptions[2]{};
        attachmentDescriptions[0].format = pickingIdFormat;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachmentDescriptions[1].format = pickingDepthFormat;
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

        if (m_pickingRenderPass == VK_NULL_HANDLE) {
            VkRenderPassCreateInfo renderPassCreateInfo{};
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.attachmentCount = 2;
            renderPassCreateInfo.pAttachments = attachmentDescriptions;
            renderPassCreateInfo.subpassCount = 1;
            renderPassCreateInfo.pSubpasses = &subpassDescription;
            renderPassCreateInfo.dependencyCount = 2;
            renderPassCreateInfo.pDependencies = dependencies;

            if (vkCreateRenderPass(device.device(), &renderPassCreateInfo, nullptr, &m_pickingRenderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create editor picking render pass");
            }
        }

        VkImageView attachments[2] = {*m_pickingIdImage->getImageView(), *m_pickingDepthImage->getImageView()};
        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_pickingRenderPass;
        framebufferCreateInfo.attachmentCount = 2;
        framebufferCreateInfo.pAttachments = attachments;
        framebufferCreateInfo.width = m_pickingExtent.width;
        framebufferCreateInfo.height = m_pickingExtent.height;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(device.device(), &framebufferCreateInfo, nullptr, &m_pickingFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create editor picking framebuffer");
        }

        m_pickingReadbackBuffer = std::make_shared<Buffer>(
                device,
                sizeof(int32_t),
                1,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_pickingReadbackBuffer->map();
    }

    int32_t Renderer::readPickingObjectId(uint32_t pixelX, uint32_t pixelY) {
        if (!m_hasPickingData || m_pickingIdImage == nullptr || m_pickingReadbackBuffer == nullptr ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return -1;
        }

        if (pixelX >= m_pickingExtent.width || pixelY >= m_pickingExtent.height) {
            return -1;
        }

        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

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
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
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

        device.endSingleTimeCommands(commandBuffer);

        const auto *mapped = static_cast<int32_t *>(m_pickingReadbackBuffer->getMappedMemory());
        return mapped == nullptr ? -1 : mapped[0];
    }
    void Renderer::freeOffscreenResources() {
        m_offscreenImageColors.clear();
        m_viewPosImageColors.clear();
        m_worldPosImage.clear();
        m_denoisingAccumulationImage.reset();
        m_offscreenSampler.reset();
        offscreenImageDepth.reset();
    }

    void Renderer::loadOffscreenResources() {
        freeOffscreenResources();
        {
            m_offscreenSampler = std::make_shared<Sampler>(device);
            m_offscreenSampler->createTextureSampler();
            //Color

            VkImageCreateInfo imageCreateInfo{};
            Image::setDefaultImageCreateInfo(imageCreateInfo);
            imageCreateInfo.format = offscreenColorFormat;
            imageCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            VkExtent3D imageExtent{};
            imageExtent.height = SCENE_HEIGHT;
            imageExtent.width = SCENE_WIDTH;
            imageExtent.depth = 1;
            imageCreateInfo.extent = imageExtent;

            m_offscreenImageColors.push_back(std::make_shared<Image>(device));
            m_offscreenImageColors.push_back(std::make_shared<Image>(device));
            m_offscreenImageColors[0]->createImage(imageCreateInfo);
            m_offscreenImageColors[1]->createImage(imageCreateInfo);
            auto imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_offscreenImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_offscreenImageColors[0]->createImageView(*imageViewCreateInfo);
            m_offscreenImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_offscreenImageColors[1]->createImageView(*imageViewCreateInfo);
            m_offscreenImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_offscreenImageColors[1]->sampler = m_offscreenSampler->getSampler();

            m_viewPosImageColors.push_back(std::make_shared<Image>(device));
            m_viewPosImageColors.push_back(std::make_shared<Image>(device));
            m_viewPosImageColors[0]->createImage(imageCreateInfo);
            m_viewPosImageColors[1]->createImage(imageCreateInfo);
            imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_viewPosImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_viewPosImageColors[0]->createImageView(*imageViewCreateInfo);
            m_viewPosImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_viewPosImageColors[1]->createImageView(*imageViewCreateInfo);
            m_viewPosImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_viewPosImageColors[1]->sampler = m_offscreenSampler->getSampler();

            m_worldPosImage.push_back(std::make_shared<Image>(device));
            m_worldPosImage.push_back(std::make_shared<Image>(device));
            imageCreateInfo.format = worldPosColorFormat;
            m_worldPosImage[0]->createImage(imageCreateInfo);
            m_worldPosImage[1]->createImage(imageCreateInfo);
            m_worldPosImage[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_worldPosImage[0]->createImageView(*imageViewCreateInfo);
            m_worldPosImage[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_worldPosImage[1]->createImageView(*imageViewCreateInfo);
            m_worldPosImage[0]->sampler = m_offscreenSampler->getSampler();
            m_worldPosImage[1]->sampler = m_offscreenSampler->getSampler();

            m_denoisingAccumulationImage = std::make_shared<Image>(device);
            imageCreateInfo.format = offscreenColorFormat;
            m_denoisingAccumulationImage->createImage(imageCreateInfo);
            m_denoisingAccumulationImage->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = offscreenColorFormat;
            m_denoisingAccumulationImage->createImageView(*imageViewCreateInfo);
            m_denoisingAccumulationImage->sampler = m_offscreenSampler->getSampler();


            //depth
            offscreenImageDepth = std::make_shared<Image>(device);
            offscreenImageDepth->setDefaultImageCreateInfo(imageCreateInfo);
            imageCreateInfo.format = offscreenDepthFormat;
            imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageCreateInfo.extent = imageExtent;
            offscreenImageDepth->createImage(imageCreateInfo);

            offscreenImageDepth->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            imageViewCreateInfo->subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            offscreenImageDepth->createImageView(*imageViewCreateInfo);
        }

        {
            device.transitionImageLayout(m_offscreenImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_offscreenImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_viewPosImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_viewPosImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_worldPosImage[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_worldPosImage[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            
            device.transitionImageLayout(m_denoisingAccumulationImage->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(offscreenImageDepth->getImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                         {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});
        }
    }

    const std::shared_ptr<Image> &Renderer::getShadowImage() const {
        return shadowImage;
    }

    const std::shared_ptr<Sampler> &Renderer::getShadowSampler() const {
        return shadowSampler;
    }

    void Renderer::setShadowMapSynchronization(VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = getShadowImage()->getImage();

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        srcStage = static_cast<VkPipelineStageFlagBits>(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;


        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

    }

#ifdef RAY_TRACING

    void Renderer::setDenoiseComputeToPostSynchronization(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_offscreenImageColors[imageIndex]->getImage();

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;


        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        barrier.image = m_denoisingAccumulationImage->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    }

    void Renderer::setDenoiseRtxToComputeSynchronization(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        barrier.image = m_offscreenImageColors[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
        
        barrier.image = m_viewPosImageColors[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

    }

#endif


}





