#pragma once

#include <string>
#include <vector>

#include "../Material.hpp"
#include "RenderResources.hpp"

namespace FeatherVK::RenderCore {
    class MaterialBindingsView {
    public:
        explicit MaterialBindingsView(const Material &material) : m_material(material) {}

        [[nodiscard]] Material::id_t MaterialId() const {
            return m_material.getMaterialId();
        }

        [[nodiscard]] const std::string &PipelineCategory() const {
            return m_material.getPipelineCategory();
        }

        [[nodiscard]] const std::vector<std::shared_ptr<RHI::RHIBindLayout>> &GetBindLayouts() const {
            return m_material.getRHIBindLayoutPointers();
        }

        [[nodiscard]] const std::vector<std::shared_ptr<RHI::RHIBindSet>> &GetBindSets() const {
            return m_material.getRHIBindSetPointers();
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHIShaderModule>> GetShaderModules() const {
            return m_material.getRHIShaderModules();
        }

        [[nodiscard]] std::vector<BufferResourceView> GetBuffers() const {
            std::vector<BufferResourceView> buffers{};
            for (const auto &buffer: m_material.getBufferPointers()) {
                buffers.push_back(MakeBufferResourceView(buffer));
            }
            return buffers;
        }

        [[nodiscard]] std::vector<TextureResourceView> GetTextures() const {
            std::vector<TextureResourceView> textures{};
            const auto imagePointers = m_material.getImagePointers();
            const auto samplers = m_material.getRHISamplerPointers();
            textures.reserve(imagePointers.size());
            for (size_t index = 0; index < imagePointers.size(); ++index) {
                std::shared_ptr<RHI::RHISampler> sampler{};
                if (index < samplers.size()) {
                    sampler = samplers[index];
                }
                textures.push_back({imagePointers[index], imagePointers[index], sampler});
            }
            return textures;
        }

    private:
        const Material &m_material;
    };
}
