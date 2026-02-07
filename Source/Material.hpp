#pragma once

#include <vulkan/vulkan.h>
#include <utility>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <rapidjson/document.h>
#include "Device.hpp"
#include "Descriptor.h"
#include "Sampler.h"
#include "Image.h"
#include "Buffer.h"
#include "RHI/RHIBinding.hpp"
#include "RHI/RHIResources.hpp"
#include "RHI/RHIShader.hpp"

namespace FeatherVK {

    enum ShaderCategory {
        vertex,
        fragment,
        tessellationControl,
        tessellationEvaluation,
        geometry,
        compute,
        rayGen,
        rayClosestHit,
        rayMiss,
        rayMiss2,
        rayAnyHit,
    };

    struct alignas(16) PBR {
        glm::vec3 albedo;//0~12
        alignas(16) glm::vec3 normal;//16~28
        alignas(4) float metallic; //28~32
        float roughness; //32~36
        float opacity;  //36~40
        float AO;   //40~44
        alignas(16)glm::vec3 emissive; //48~60
    };

    typedef struct ShaderModule {
        std::shared_ptr<RHI::RHIShaderModule> shaderModule;
        ShaderCategory shaderCategory;

        ShaderModule(std::shared_ptr<RHI::RHIShaderModule> shaderModule, ShaderCategory shaderCategory) {
            this->shaderModule = std::move(shaderModule);
            this->shaderCategory = shaderCategory;
        }
    } ShaderModule;

    const int PBRParametersCount = 7;

    class PBRLoader {
    public:
        static PBR loadPBR(const rapidjson::Value &value) {
            PBR pbr;
            if (value.HasMember("albedo")) {
                auto &albedo = value["albedo"];
                pbr.albedo = glm::vec3(albedo[0].GetFloat(), albedo[1].GetFloat(), albedo[2].GetFloat());
            } else {
                pbr.albedo = glm::vec3(-1.f);
            }

            if (value.HasMember("normal")) {
                pbr.normal = glm::vec3(0);
            } else {
                pbr.normal = glm::vec3(-1.f);
            }

            if (value.HasMember("metallic")) {
                pbr.metallic = value["metallic"].GetFloat();
            } else {
                pbr.metallic = -1.f;
            }

            if (value.HasMember("roughness")) {
                pbr.roughness = value["roughness"].GetFloat();
            } else {
                pbr.roughness = -1.f;
            }

            if (value.HasMember("opacity")) {
                pbr.opacity = value["opacity"].GetFloat();
            } else {
                pbr.opacity = -1.f;
            }

            if (value.HasMember("ao")) {
                pbr.AO = 0;
            } else {
                pbr.AO = -1;
            }

            if (value.HasMember("emissive")) {
                auto &emissive = value["emissive"];
                pbr.emissive = glm::vec3(emissive[0].GetFloat(), emissive[1].GetFloat(), emissive[2].GetFloat());
            } else {
                pbr.emissive = glm::vec3(-1, -1, -1);
            }
            return pbr;
        }

        static int getValidPropertyCount(const PBR &pbr) {
            int count = GetValidProperty(pbr).size();
            return count;
        }

        static std::vector<int> GetValidProperty(const PBR &pbr) {
            std::vector<int> validProperty{};
            if (pbr.albedo != glm::vec3(-1.f)) {
                validProperty.push_back(0);
            }
            if (pbr.normal != glm::vec3(-1.f)) {
                validProperty.push_back(1);
            }
            if (pbr.metallic != -1.f) {
                validProperty.push_back(2);
            }
            if (pbr.roughness != -1.f) {
                validProperty.push_back(3);
            }
            if (pbr.opacity != -1.f) {
                validProperty.push_back(4);
            }
            if (pbr.AO != -1.f) {
                validProperty.push_back(5);
            }
            if (pbr.emissive != glm::vec3(-1, -1, -1)) {
                validProperty.push_back(6);
            }
            return validProperty;
        }
    };

    class Material {
    public:
        enum MaterialId {
            post = -1,
            rayTracing = -2,
            compute = -3,
            gizmos = -4,
        };
        using id_t = signed int;
        using Map = std::unordered_map<id_t, std::shared_ptr<Material>>;

        ~Material() = default;

        Material(Device &device,
                 id_t id,
                 std::vector<std::shared_ptr<ShaderModule>> &shaderModules,
                 std::vector<std::shared_ptr<DescriptorSetLayout>> &descriptorSetLayouts,
                 std::vector<std::shared_ptr<VkDescriptorSet>> &descriptorSets,
                 std::vector<std::shared_ptr<Image>> &imagePointers,
                 std::vector<std::shared_ptr<Sampler>> &samplerPointers,
                 std::vector<std::shared_ptr<Buffer>> &bufferPointers,
                 std::string pipelineCategory) :
                materialId(id),
                shaderModules(std::move(shaderModules)),
                descriptorSets(std::move(descriptorSets)),
                descriptorSetLayoutPointers(std::move(descriptorSetLayouts)),
                imagePointers(std::move(imagePointers)),
                samplerPointers(std::move(samplerPointers)),
                bufferPointers(std::move(bufferPointers)),
                pipelineCategory(std::move(pipelineCategory)) {
            m_rhiBindLayoutPointers.assign(descriptorSetLayoutPointers.begin(), descriptorSetLayoutPointers.end());
            m_rhiBindSetPointers.reserve(this->descriptorSetLayoutPointers.size());
            for (size_t i = 0; i < this->descriptorSetLayoutPointers.size() && i < this->descriptorSets.size(); ++i) {
                if (this->descriptorSetLayoutPointers[i] == nullptr || this->descriptorSets[i] == nullptr) {
                    continue;
                }
                m_rhiBindSetPointers.push_back(std::make_shared<DescriptorSetHandle>(this->descriptorSetLayoutPointers[i], this->descriptorSets[i]));
            }
        };

        [[nodiscard]] std::vector<std::shared_ptr<Image>> getImagePointers() const {
            return imagePointers;
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHITexture>> getRHITexturePointers() const {
            return {imagePointers.begin(), imagePointers.end()};
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHITextureView>> getRHITextureViewPointers() const {
            return {imagePointers.begin(), imagePointers.end()};
        }

        [[nodiscard]] std::vector<std::shared_ptr<ShaderModule>> &getShaderModulePointers() {
            return shaderModules;
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHIShaderModule>> getRHIShaderModules() const {
            std::vector<std::shared_ptr<RHI::RHIShaderModule>> shaderModulePointers{};
            shaderModulePointers.reserve(shaderModules.size());
            for (const auto &shaderModule: shaderModules) {
                shaderModulePointers.push_back(shaderModule->shaderModule);
            }
            return shaderModulePointers;
        }

        [[nodiscard]] std::vector<std::shared_ptr<DescriptorSetLayout>> getDescriptorSetLayoutPointers() const {
            return descriptorSetLayoutPointers;
        }

        [[nodiscard]] std::vector<std::shared_ptr<VkDescriptorSet>> getDescriptorSetPointers() const {
            return descriptorSets;
        }

        [[nodiscard]] const std::vector<std::shared_ptr<RHI::RHIBindLayout>> &getRHIBindLayoutPointers() const {
            return m_rhiBindLayoutPointers;
        }

        [[nodiscard]] const std::vector<std::shared_ptr<RHI::RHIBindSet>> &getRHIBindSetPointers() const {
            return m_rhiBindSetPointers;
        }

        [[nodiscard]] const std::vector<std::shared_ptr<Buffer>> &getBufferPointers() const {
            return bufferPointers;
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHISampler>> getRHISamplerPointers() const {
            return {samplerPointers.begin(), samplerPointers.end()};
        }

        [[nodiscard]] std::vector<std::shared_ptr<RHI::RHIBuffer>> getRHIBufferPointers() const {
            return {bufferPointers.begin(), bufferPointers.end()};
        }

        id_t getMaterialId() const {
            return materialId;
        }

        const std::string &getPipelineCategory() const {
            return pipelineCategory;
        }

    private:
        id_t materialId;
        std::vector<std::shared_ptr<ShaderModule>> shaderModules;
        std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers;
        std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSets;
        std::vector<std::shared_ptr<RHI::RHIBindLayout>> m_rhiBindLayoutPointers;
        std::vector<std::shared_ptr<RHI::RHIBindSet>> m_rhiBindSetPointers;
        std::vector<std::shared_ptr<Image>> imagePointers;
        std::vector<std::shared_ptr<Sampler>> samplerPointers;
        std::vector<std::shared_ptr<Buffer>> bufferPointers;
        std::string pipelineCategory;
    };

}
