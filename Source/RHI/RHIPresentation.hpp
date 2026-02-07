#pragma once

#include "RHICommands.hpp"

namespace FeatherVK::RHI {
    class RHISwapchain : public RHIObject {
    public:
        ~RHISwapchain() override = default;

        virtual Extent2D GetExtent() const = 0;
        virtual uint32_t GetImageCount() const = 0;
        virtual SwapchainStatus AcquireNextImageRHI(uint32_t *imageIndex) = 0;
        virtual SwapchainStatus SubmitAndPresent(RHICommandList &commandList, uint32_t *imageIndex) = 0;
    };
}
