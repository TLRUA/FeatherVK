#pragma once

#include <string>

#include "RHIResources.hpp"

namespace FeatherVK::RHI {
    class RHIShaderModule : public RHIObject {
    public:
        ~RHIShaderModule() override = default;

        virtual const std::string &GetPath() const = 0;
    };
}
