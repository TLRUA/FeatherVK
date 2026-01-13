#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "Device.hpp"
#include "Utils/ProjectPaths.hpp"

namespace FeatherVK {

    class ShaderBuilder {
    public:

        ShaderBuilder(Device &device) : device(device) {};

        std::shared_ptr<VkShaderModule> createShaderModule(const std::string &shaderName);

#ifndef RAY_TRACING

        std::shared_ptr<VkShaderModule> getShaderModulePointer(const std::string &shaderName);

#endif

        std::vector<char> readFile(const std::string &filepath);

    private:
        static std::string GetBaseShaderPath() { return ProjectPaths::ShadersDir(); }

        std::unordered_map<std::string, std::shared_ptr<VkShaderModule>> shaderModuleMap;

        Device &device;
    };

} // FeatherVK



