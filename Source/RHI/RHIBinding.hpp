#pragma once

#include <memory>
#include <vector>

#include "RHIResources.hpp"

namespace FeatherVK::RHI {
    class RHIBindLayout : public RHIObject {
    public:
        ~RHIBindLayout() override = default;

        virtual const std::vector<BindLayoutEntry> &GetEntries() const = 0;
    };

    class RHIBindSet : public RHIObject {
    public:
        ~RHIBindSet() override = default;

        virtual const RHIBindLayout &GetLayout() const = 0;
    };
}
