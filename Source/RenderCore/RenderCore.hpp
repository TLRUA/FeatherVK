#pragma once

#include "PipelineLibrary.hpp"
#include "RenderResourceRegistry.hpp"
#include "ShaderLibrary.hpp"

namespace FeatherVK::RenderCore {
    class CoreServices {
    public:
        explicit CoreServices(Device &device)
            : m_shaderLibrary(device, &m_resourceRegistry),
              m_pipelineLibrary(device, &m_resourceRegistry) {}

        ShaderLibrary &GetShaderLibrary() { return m_shaderLibrary; }
        const ShaderLibrary &GetShaderLibrary() const { return m_shaderLibrary; }

        PipelineLibrary &GetPipelineLibrary() { return m_pipelineLibrary; }
        const PipelineLibrary &GetPipelineLibrary() const { return m_pipelineLibrary; }

        RenderResourceRegistry &GetResourceRegistry() { return m_resourceRegistry; }
        const RenderResourceRegistry &GetResourceRegistry() const { return m_resourceRegistry; }

    private:
        RenderResourceRegistry m_resourceRegistry;
        ShaderLibrary m_shaderLibrary;
        PipelineLibrary m_pipelineLibrary;
    };
}
