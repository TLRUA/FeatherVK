#include <cmath>
#include <memory>

#include "Renderer.h"
#include "RenderGraph/RenderGraphExecutor.hpp"
#include "RenderGraph/RenderGraphResourceCache.hpp"
#include "RHI/Vulkan/VulkanCommandList.hpp"

namespace FeatherVK {
    namespace {
        uint32_t ClampExtentDimension(float value) {
            return static_cast<uint32_t>(std::max(1.0f, std::round(value)));
        }

        bool SameRect(const ViewportRect &lhs, const ViewportRect &rhs) {
            return lhs.x == rhs.x &&
                   lhs.y == rhs.y &&
                   lhs.width == rhs.width &&
                   lhs.height == rhs.height;
        }
    }

    Renderer::Renderer(MyWindow &window, Device &device1)
        : myWindow{window}, device{device1} {
        m_scenePanelRect = {
            static_cast<float>(UI_LEFT_WIDTH + UI_LEFT_WIDTH_2),
            0.0f,
            static_cast<float>(SCENE_WIDTH),
            static_cast<float>(SCENE_HEIGHT)};
        m_sceneViewportRect = m_scenePanelRect;
        m_currentCommandList = std::make_unique<RHI::VulkanCommandList>(device);
        m_renderGraphExecutor = std::make_unique<RenderGraph::RenderGraphExecutor>();

        recreateSwapChain();
        createCommandBuffers();
    }

    Renderer::~Renderer() {
        vkDeviceWaitIdle(device.device());
        freeCommandBuffers();
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

        if (auto *vulkanCommandList = dynamic_cast<RHI::VulkanCommandList *>(m_currentCommandList.get());
            vulkanCommandList != nullptr) {
            vulkanCommandList->SetCommandBuffer(commandBuffer);
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
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image");
        }

        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
        if (auto *vulkanCommandList = dynamic_cast<RHI::VulkanCommandList *>(m_currentCommandList.get());
            vulkanCommandList != nullptr) {
            vulkanCommandList->SetCommandBuffer(VK_NULL_HANDLE);
        }
    }

    void Renderer::recreateSwapChain() {
        auto extent = myWindow.getCurrentExtent();
        while (extent.width == 0 || extent.height == 0) {
            extent = myWindow.getCurrentExtent();
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device.device());

        if (swapChain == nullptr) {
            swapChain = std::make_unique<SwapChain>(device, extent);
            return;
        }

        std::shared_ptr<SwapChain> oldSwapChain = std::move(swapChain);
        swapChain = std::make_unique<SwapChain>(device, extent, oldSwapChain);
        if (!oldSwapChain->compareSwapFormats(*swapChain)) {
            throw std::runtime_error("Swap chain's image or depth format has changed");
        }
    }

    bool Renderer::UpdateSceneViewportLayout(const ViewportRect &scenePanelRect, const ViewportRect &sceneViewportRect) {
        const VkExtent2D newSceneExtent{
            ClampExtentDimension(sceneViewportRect.width),
            ClampExtentDimension(sceneViewportRect.height)};
        const bool sceneExtentChanged = newSceneExtent.width != m_sceneRenderExtent.width ||
                                        newSceneExtent.height != m_sceneRenderExtent.height;
        const bool scenePanelChanged = !SameRect(m_scenePanelRect, scenePanelRect);
        const bool sceneViewportChanged = !SameRect(m_sceneViewportRect, sceneViewportRect);

        if (!sceneExtentChanged && !scenePanelChanged && !sceneViewportChanged) {
            return false;
        }

        m_scenePanelRect = scenePanelRect;
        m_sceneViewportRect = sceneViewportRect;

        if (!sceneExtentChanged) {
            return false;
        }

        m_sceneRenderExtent = newSceneExtent;
        return true;
    }

    void Renderer::createCommandBuffers() {
        commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device.getCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device.device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers");
        }
    }

    void Renderer::freeCommandBuffers() {
        if (commandBuffers.empty()) {
            return;
        }

        vkFreeCommandBuffers(
            device.device(),
            device.getCommandPool(),
            static_cast<uint32_t>(commandBuffers.size()),
            commandBuffers.data());
        commandBuffers.clear();
    }

    VkExtent2D Renderer::getPickingExtent() const {
        if (m_renderGraphResourceCache != nullptr) {
            return m_renderGraphResourceCache->GetPickingExtent();
        }
        return m_sceneRenderExtent;
    }

    int32_t Renderer::readPickingObjectId(uint32_t pixelX, uint32_t pixelY) {
        if (m_renderGraphResourceCache == nullptr) {
            return -1;
        }
        return m_renderGraphResourceCache->ReadPickingObjectId(pixelX, pixelY);
    }

    void Renderer::ExecuteGraph(RenderGraph::RenderGraph &graph,
                                FrameInfo &frameInfo,
                                const RenderGraph::RenderGraphResourceBindings &bindings,
                                RenderGraph::RenderGraphResourceCache &resourceCache) {
        assert(m_renderGraphExecutor != nullptr && "RenderGraph executor is not initialized");
        m_renderGraphExecutor->Execute(graph, *this, frameInfo, bindings, resourceCache);
    }
}
