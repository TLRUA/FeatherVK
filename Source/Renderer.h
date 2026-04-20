#pragma once

#include <cassert>
#include "MyWindow.hpp"
#include "SwapChain.hpp"
#include "Device.hpp"
#include "Image.h"
#include "Buffer.h"
#include "RenderCore/FrameData.hpp"
#include "RenderCore/RenderResources.hpp"
#include "RHI/RHICommands.hpp"
#include "RHI/RHIPresentation.hpp"
#include "StructureInfos.h"

namespace FeatherVK {
    namespace RenderGraph {
        class RenderGraphResourceCache;
    }

    class Renderer {
    public:
        const float FOV_Y = 50.f;
        const float NEAR_CLIP = 0.1f;
        const float FAR_CLIP = 20.f;

        Renderer(MyWindow &, Device &);

        ~Renderer();

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        VkCommandBuffer beginFrame();

        void endFrame();

        void SetRenderGraphResourceCache(RenderGraph::RenderGraphResourceCache *resourceCache) {
            m_renderGraphResourceCache = resourceCache;
        }

        bool getIsFrameStarted() const { return isFrameStarted; }

        [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const {
            assert(isFrameStarted && "Cannot get command buffer when frame is not in progress");
            return commandBuffers[currentFrameIndex];
        }

        RHI::RHICommandList &getCurrentRHICommandList() const {
            assert(m_currentCommandList != nullptr && "RHI command list is not initialized");
            return *m_currentCommandList;
        }

        RHI::RHISwapchain &getRHISwapchain() const {
            return *swapChain;
        }

        const VkRenderPass &getSwapChainRenderPass() {
            return swapChain->getRenderPass();
        }

        int getFrameIndex() const {
            assert(isFrameStarted && "Cannot get frame index when frame is not in progress");
            return currentFrameIndex;
        }

        float getAspectRatio() const {
            const auto sceneExtent = getSceneRenderExtent();
            return static_cast<float>(sceneExtent.width) / static_cast<float>(sceneExtent.height);
        }

        bool UpdateSceneViewportLayout(const ViewportRect &scenePanelRect, const ViewportRect &sceneViewportRect);

        [[nodiscard]] const ViewportRect &getScenePanelRect() const {
            return m_scenePanelRect;
        }

        [[nodiscard]] const ViewportRect &getSceneViewportRect() const {
            return m_sceneViewportRect;
        }

        [[nodiscard]] VkImage getSwapChainImage(uint32_t index) const {
            return swapChain->getImage(static_cast<int>(index));
        }

        [[nodiscard]] VkFramebuffer getSwapChainFramebuffer(uint32_t index) const {
            return swapChain->getFrameBuffer(static_cast<int>(index));
        }

        [[nodiscard]] VkRenderPass getGizmosRenderPass() const {
            return swapChain->getGizmosRenderPass();
        }

        [[nodiscard]] VkFormat getSwapChainImageFormat() const {
            return swapChain->getSwapChainImageFormat();
        }

        [[nodiscard]] VkExtent2D getSwapChainExtent() const {
            return swapChain->getSwapChainExtent();
        }

        [[nodiscard]] VkFormat getSwapChainDepthFormat() const {
            return swapChain->getSwapChainDepthFormat();
        }

        [[nodiscard]] uint32_t getCurrentImageIndex() const {
            return currentImageIndex;
        }

        [[nodiscard]] VkExtent2D getSceneRenderExtent() const {
            return m_sceneRenderExtent;
        }

        [[nodiscard]] RenderCore::RenderView GetRenderView() const {
            const float aspectRatio = m_sceneRenderExtent.height == 0
                                          ? 1.0f
                                          : static_cast<float>(m_sceneRenderExtent.width) /
                                                static_cast<float>(m_sceneRenderExtent.height);
            return {m_scenePanelRect, m_sceneViewportRect, m_sceneRenderExtent, aspectRatio};
        }

        VkExtent2D getPickingExtent() const;

        int32_t readPickingObjectId(uint32_t pixelX, uint32_t pixelY);

    private:
        void createCommandBuffers();

        void recreateSwapChain();

        void freeCommandBuffers();

        MyWindow &myWindow;
        Device &device;
        std::unique_ptr<SwapChain> swapChain;
        std::unique_ptr<RHI::RHICommandList> m_currentCommandList;
        std::vector<VkCommandBuffer> commandBuffers;

        uint32_t currentImageIndex;
        int currentFrameIndex = 0;
        bool isFrameStarted = false;
        ViewportRect m_scenePanelRect{};
        ViewportRect m_sceneViewportRect{};
        VkExtent2D m_sceneRenderExtent{static_cast<uint32_t>(SCENE_WIDTH), static_cast<uint32_t>(SCENE_HEIGHT)};
        RenderGraph::RenderGraphResourceCache *m_renderGraphResourceCache{nullptr};

    };

}

