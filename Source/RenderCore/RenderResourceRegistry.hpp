#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "../Material.hpp"
#include "../Model.hpp"
#include "../Pipeline.hpp"
#include "../RHI/RHIResources.hpp"
#include "../RHI/RHIShader.hpp"
#include "../RHI/RHITextureView.hpp"
#include "../RHI/RHITypes.hpp"
#include "../SwapChain.hpp"
#include "RenderResourceHandle.hpp"

namespace FeatherVK::RenderCore {
    enum class RenderBufferUsage : uint32_t {
        Unknown = 0,
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
        Storage = 1 << 3,
        Transfer = 1 << 4
    };

    inline RenderBufferUsage operator|(RenderBufferUsage lhs, RenderBufferUsage rhs) {
        return static_cast<RenderBufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    struct RenderBufferDesc {
        std::string debugName{};
        uint64_t byteSize{0};
        RenderBufferUsage usage{RenderBufferUsage::Unknown};
        bool hostVisible{false};
        bool deviceLocal{false};
    };

    struct RenderBuffer {
        RenderResourceHandle handle{};
        std::string debugName{};
        RenderBufferDesc desc{};
        std::shared_ptr<RHI::RHIBuffer> rhiBuffer{};
        RHI::RHIBuffer *legacyBuffer{nullptr};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && (rhiBuffer != nullptr || legacyBuffer != nullptr);
        }
    };

    struct RenderTextureDesc {
        std::string debugName{};
        RHI::Extent2D extent{};
        RHI::TextureDimension dimension{RHI::TextureDimension::Texture2D};
        bool sampled{false};
        bool storage{false};
        bool renderTarget{false};
        bool srgb{false};
    };

    struct RenderTexture {
        RenderResourceHandle handle{};
        std::string debugName{};
        RenderTextureDesc desc{};
        std::shared_ptr<RHI::RHITexture> texture{};
        std::shared_ptr<RHI::RHITextureView> view{};
        std::shared_ptr<RHI::RHISampler> sampler{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && texture != nullptr;
        }
    };

    struct RenderTarget {
        RenderResourceHandle handle{};
        std::string debugName{};
        RHI::Extent2D extent{};
        std::vector<RenderResourceHandle> colorAttachments{};
        RenderResourceHandle depthAttachment{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && extent.width > 0 && extent.height > 0 && !colorAttachments.empty();
        }
    };

    struct ShaderResource {
        RenderResourceHandle handle{};
        std::string debugName{};
        std::string path{};
        std::string entryPoint{"main"};
        RHI::ShaderStage stage{RHI::ShaderStage::None};
        std::shared_ptr<RHI::RHIShaderModule> shaderModule{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && shaderModule != nullptr;
        }
    };

    struct PipelineResource {
        RenderResourceHandle handle{};
        std::string debugName{};
        RHI::PipelineType type{RHI::PipelineType::Graphics};
        std::string category{};
        VkPipelineLayout legacyLayout{VK_NULL_HANDLE};
        std::shared_ptr<Pipeline> legacyPipeline{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && (legacyPipeline != nullptr || legacyLayout != VK_NULL_HANDLE);
        }
    };

    struct MaterialResource {
        RenderResourceHandle handle{};
        std::string debugName{};
        Material::id_t legacyMaterialId{0};
        std::string pipelineCategory{};
        bool hasDefaultPbr{false};
        PBR defaultPbr{};
        std::vector<RenderResourceHandle> shaders{};
        std::vector<RenderResourceHandle> textures{};
        std::vector<RenderResourceHandle> buffers{};
        std::shared_ptr<Material> legacyMaterial{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && legacyMaterial != nullptr;
        }
    };

    struct MaterialInstance {
        RenderResourceHandle handle{};
        std::string debugName{};
        RenderResourceHandle material{};
        int32_t ownerEntityId{-1};
        bool hasPbrOverride{false};
        PBR pbrOverride{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && material.IsValid();
        }
    };

    struct MeshResource {
        RenderResourceHandle handle{};
        std::string debugName{};
        std::string sourcePath{};
        std::string primitiveType{};
        RenderResourceHandle vertexBuffer{};
        RenderResourceHandle indexBuffer{};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        bool indexed{false};
        glm::vec3 localBoundsCenter{0.0f};
        float maxRadius{0.0f};
        RenderMesh renderMesh{};
        std::shared_ptr<Model> legacyModel{};

        [[nodiscard]] bool IsValid() const {
            return handle.IsValid() && vertexCount > 0 && legacyModel != nullptr;
        }
    };

    class RenderResourceRegistry {
    public:
        static constexpr uint64_t DefaultDeferredReleaseLag = SwapChain::MAX_FRAMES_IN_FLIGHT + 1;

        RenderResourceHandle RegisterBuffer(RenderBuffer resource) {
            return RegisterResource(RenderResourceType::Buffer, std::move(resource), m_buffers);
        }

        RenderResourceHandle RegisterTexture(RenderTexture resource) {
            return RegisterResource(RenderResourceType::Texture, std::move(resource), m_textures);
        }

        RenderResourceHandle RegisterRenderTarget(RenderTarget resource) {
            return RegisterResource(RenderResourceType::RenderTarget, std::move(resource), m_renderTargets);
        }

        RenderResourceHandle RegisterShader(ShaderResource resource) {
            return RegisterResource(RenderResourceType::Shader, std::move(resource), m_shaders);
        }

        RenderResourceHandle RegisterPipeline(PipelineResource resource) {
            return RegisterResource(RenderResourceType::Pipeline, std::move(resource), m_pipelines);
        }

        RenderResourceHandle RegisterMaterial(MaterialResource resource) {
            return RegisterResource(RenderResourceType::Material, std::move(resource), m_materials);
        }

        RenderResourceHandle RegisterMaterialInstance(MaterialInstance resource) {
            return RegisterResource(RenderResourceType::MaterialInstance, std::move(resource), m_materialInstances);
        }

        RenderResourceHandle RegisterMesh(MeshResource resource) {
            return RegisterResource(RenderResourceType::Mesh, std::move(resource), m_meshes);
        }

        RenderResourceHandle ImportBuffer(const std::string &debugName, const std::shared_ptr<RHI::RHIBuffer> &buffer) {
            if (buffer == nullptr) {
                return InvalidRenderResourceHandle;
            }

            const auto &desc = buffer->GetDesc();
            RenderBuffer resource{};
            resource.debugName = debugName;
            resource.desc.debugName = debugName;
            resource.desc.byteSize = desc.byteSize;
            resource.desc.hostVisible = desc.hostVisible;
            resource.desc.deviceLocal = desc.deviceLocal;
            resource.rhiBuffer = buffer;
            resource.legacyBuffer = buffer.get();
            return UpsertResource(RenderResourceType::Buffer, debugName, std::move(resource), m_buffers);
        }

        RenderResourceHandle ImportTexture(
            const std::string &debugName,
            const std::shared_ptr<RHI::RHITexture> &texture,
            const std::shared_ptr<RHI::RHITextureView> &view = nullptr,
            const std::shared_ptr<RHI::RHISampler> &sampler = nullptr) {
            if (texture == nullptr) {
                return InvalidRenderResourceHandle;
            }

            const auto &desc = texture->GetDesc();
            RenderTexture resource{};
            resource.debugName = debugName;
            resource.desc.debugName = debugName;
            resource.desc.extent = desc.extent;
            resource.desc.dimension = desc.dimension;
            resource.desc.sampled = desc.sampled;
            resource.desc.storage = desc.storage;
            resource.desc.renderTarget = desc.renderTarget;
            resource.desc.srgb = desc.srgb;
            resource.texture = texture;
            resource.view = view;
            resource.sampler = sampler;
            return UpsertResource(RenderResourceType::Texture, debugName, std::move(resource), m_textures);
        }

        RenderResourceHandle ImportRenderTarget(
            const std::string &debugName,
            RHI::Extent2D extent,
            std::vector<RenderResourceHandle> colorAttachments,
            RenderResourceHandle depthAttachment = {}) {
            RenderTarget resource{};
            resource.debugName = debugName;
            resource.extent = extent;
            resource.colorAttachments = std::move(colorAttachments);
            resource.depthAttachment = depthAttachment;
            return UpsertResource(RenderResourceType::RenderTarget, debugName, std::move(resource), m_renderTargets);
        }

        RenderResourceHandle ImportShader(
            const std::string &debugName,
            const std::string &path,
            RHI::ShaderStage stage,
            const std::shared_ptr<RHI::RHIShaderModule> &shaderModule) {
            if (shaderModule == nullptr) {
                return InvalidRenderResourceHandle;
            }

            ShaderResource resource{};
            resource.debugName = debugName;
            resource.path = path;
            resource.stage = stage;
            resource.shaderModule = shaderModule;
            return UpsertResource(RenderResourceType::Shader, debugName, std::move(resource), m_shaders);
        }

        RenderResourceHandle ImportPipeline(
            const std::string &debugName,
            const std::shared_ptr<Pipeline> &pipeline,
            VkPipelineLayout legacyLayout = VK_NULL_HANDLE) {
            if (pipeline == nullptr && legacyLayout == VK_NULL_HANDLE) {
                return InvalidRenderResourceHandle;
            }

            PipelineResource resource{};
            resource.debugName = debugName;
            resource.legacyPipeline = pipeline;
            resource.legacyLayout = legacyLayout;
            if (pipeline != nullptr) {
                const auto &desc = pipeline->GetDesc();
                resource.type = desc.type;
                resource.category = desc.category;
                resource.legacyLayout = pipeline->getPipelineLayout();
            }
            return UpsertResource(RenderResourceType::Pipeline, debugName, std::move(resource), m_pipelines);
        }

        RenderResourceHandle ImportMaterial(
            const std::string &debugName,
            const std::shared_ptr<Material> &material,
            const std::optional<PBR> &defaultPbr = std::nullopt) {
            if (material == nullptr) {
                return InvalidRenderResourceHandle;
            }

            MaterialResource resource{};
            resource.debugName = debugName;
            resource.legacyMaterialId = material->getMaterialId();
            resource.pipelineCategory = material->getPipelineCategory();
            resource.legacyMaterial = material;
            if (defaultPbr.has_value()) {
                resource.hasDefaultPbr = true;
                resource.defaultPbr = *defaultPbr;
            }

            for (const auto &shaderModule: material->getShaderModulePointers()) {
                if (shaderModule == nullptr || shaderModule->shaderModule == nullptr) {
                    continue;
                }
                const std::string shaderName = debugName + "/Shader/" + shaderModule->shaderModule->GetPath();
                resource.shaders.push_back(ImportShader(
                    shaderName,
                    shaderModule->shaderModule->GetPath(),
                    ToRhiShaderStage(shaderModule->shaderCategory),
                    shaderModule->shaderModule));
            }

            const auto imagePointers = material->getImagePointers();
            const auto samplers = material->getRHISamplerPointers();
            for (size_t index = 0; index < imagePointers.size(); ++index) {
                std::shared_ptr<RHI::RHISampler> sampler{};
                if (index < samplers.size()) {
                    sampler = samplers[index];
                }
                resource.textures.push_back(ImportTexture(
                    debugName + "/Texture/" + std::to_string(index),
                    imagePointers[index],
                    imagePointers[index],
                    sampler));
            }

            const auto &buffers = material->getBufferPointers();
            for (size_t index = 0; index < buffers.size(); ++index) {
                resource.buffers.push_back(ImportBuffer(
                    debugName + "/Buffer/" + std::to_string(index),
                    buffers[index]));
            }

            return UpsertResource(RenderResourceType::Material, debugName, std::move(resource), m_materials);
        }

        RenderResourceHandle ImportMaterialInstance(
            const std::string &debugName,
            RenderResourceHandle material,
            int32_t ownerEntityId,
            const std::optional<PBR> &pbrOverride = std::nullopt) {
            if (!material.IsValid()) {
                return InvalidRenderResourceHandle;
            }

            MaterialInstance resource{};
            resource.debugName = debugName;
            resource.material = material;
            resource.ownerEntityId = ownerEntityId;
            if (pbrOverride.has_value()) {
                resource.hasPbrOverride = true;
                resource.pbrOverride = *pbrOverride;
            }
            return UpsertResource(RenderResourceType::MaterialInstance, debugName, std::move(resource), m_materialInstances);
        }

        RenderResourceHandle ImportMesh(
            const std::string &debugName,
            const std::shared_ptr<Model> &model,
            const std::string &sourcePath = {},
            const std::string &primitiveType = {}) {
            if (model == nullptr) {
                return InvalidRenderResourceHandle;
            }

            MeshResource resource{};
            resource.debugName = debugName;
            resource.sourcePath = sourcePath;
            resource.primitiveType = primitiveType;
            resource.vertexCount = model->getVertexCount();
            resource.indexCount = model->getIndexCount();
            resource.indexed = model->getIndexCount() > 0;
            resource.localBoundsCenter = model->GetLocalBoundsCenter();
            resource.maxRadius = model->GetMaxRadius();
            resource.renderMesh = model->GetRenderMesh();
            resource.legacyModel = model;

            if (model->getVertexBuffer() != nullptr) {
                resource.vertexBuffer = ImportBuffer(debugName + "/VertexBuffer", std::shared_ptr<RHI::RHIBuffer>(model, model->getVertexBuffer().get()));
            }
            if (model->getIndexBuffer() != nullptr) {
                resource.indexBuffer = ImportBuffer(debugName + "/IndexBuffer", std::shared_ptr<RHI::RHIBuffer>(model, model->getIndexBuffer().get()));
            }

            return UpsertResource(RenderResourceType::Mesh, debugName, std::move(resource), m_meshes);
        }

        RenderBuffer *GetBuffer(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Buffer, m_buffers);
        }

        const RenderBuffer *GetBuffer(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Buffer, m_buffers);
        }

        RenderTexture *GetTexture(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Texture, m_textures);
        }

        const RenderTexture *GetTexture(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Texture, m_textures);
        }

        RenderTarget *GetRenderTarget(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::RenderTarget, m_renderTargets);
        }

        const RenderTarget *GetRenderTarget(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::RenderTarget, m_renderTargets);
        }

        ShaderResource *GetShader(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Shader, m_shaders);
        }

        const ShaderResource *GetShader(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Shader, m_shaders);
        }

        PipelineResource *GetPipeline(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Pipeline, m_pipelines);
        }

        const PipelineResource *GetPipeline(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Pipeline, m_pipelines);
        }

        MaterialResource *GetMaterial(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Material, m_materials);
        }

        const MaterialResource *GetMaterial(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Material, m_materials);
        }

        MaterialInstance *GetMaterialInstance(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::MaterialInstance, m_materialInstances);
        }

        const MaterialInstance *GetMaterialInstance(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::MaterialInstance, m_materialInstances);
        }

        MeshResource *GetMesh(RenderResourceHandle handle) {
            return GetResource(handle, RenderResourceType::Mesh, m_meshes);
        }

        const MeshResource *GetMesh(RenderResourceHandle handle) const {
            return GetResource(handle, RenderResourceType::Mesh, m_meshes);
        }

        RenderResourceHandle FindByName(RenderResourceType type, const std::string &debugName) const {
            switch (type) {
                case RenderResourceType::Buffer:
                    return FindByNameInSlots(debugName, m_buffers);
                case RenderResourceType::Texture:
                    return FindByNameInSlots(debugName, m_textures);
                case RenderResourceType::RenderTarget:
                    return FindByNameInSlots(debugName, m_renderTargets);
                case RenderResourceType::Shader:
                    return FindByNameInSlots(debugName, m_shaders);
                case RenderResourceType::Pipeline:
                    return FindByNameInSlots(debugName, m_pipelines);
                case RenderResourceType::Material:
                    return FindByNameInSlots(debugName, m_materials);
                case RenderResourceType::MaterialInstance:
                    return FindByNameInSlots(debugName, m_materialInstances);
                case RenderResourceType::Mesh:
                    return FindByNameInSlots(debugName, m_meshes);
                default:
                    return InvalidRenderResourceHandle;
            }
        }

        bool Destroy(RenderResourceHandle handle) {
            switch (handle.type) {
                case RenderResourceType::Buffer:
                    return DestroyResource(handle, m_buffers);
                case RenderResourceType::Texture:
                    return DestroyResource(handle, m_textures);
                case RenderResourceType::RenderTarget:
                    return DestroyResource(handle, m_renderTargets);
                case RenderResourceType::Shader:
                    return DestroyResource(handle, m_shaders);
                case RenderResourceType::Pipeline:
                    return DestroyResource(handle, m_pipelines);
                case RenderResourceType::Material:
                    return DestroyResource(handle, m_materials);
                case RenderResourceType::MaterialInstance:
                    return DestroyResource(handle, m_materialInstances);
                case RenderResourceType::Mesh:
                    return DestroyResource(handle, m_meshes);
                default:
                    return false;
            }
        }

        void AdvanceFrame() {
            ++m_frameSerial;
            ProcessDeferredReleases(false);
        }

        [[nodiscard]] uint64_t GetFrameSerial() const {
            return m_frameSerial;
        }

        void Clear() {
            RetireAllResources(m_buffers);
            RetireAllResources(m_textures);
            RetireAllResources(m_renderTargets);
            RetireAllResources(m_shaders);
            RetireAllResources(m_pipelines);
            RetireAllResources(m_materials);
            RetireAllResources(m_materialInstances);
            RetireAllResources(m_meshes);
        }

        [[nodiscard]] size_t Count(RenderResourceType type) const {
            switch (type) {
                case RenderResourceType::Buffer:
                    return CountAlive(m_buffers);
                case RenderResourceType::Texture:
                    return CountAlive(m_textures);
                case RenderResourceType::RenderTarget:
                    return CountAlive(m_renderTargets);
                case RenderResourceType::Shader:
                    return CountAlive(m_shaders);
                case RenderResourceType::Pipeline:
                    return CountAlive(m_pipelines);
                case RenderResourceType::Material:
                    return CountAlive(m_materials);
                case RenderResourceType::MaterialInstance:
                    return CountAlive(m_materialInstances);
                case RenderResourceType::Mesh:
                    return CountAlive(m_meshes);
                default:
                    return 0;
            }
        }

        static RHI::ShaderStage ToRhiShaderStage(ShaderCategory shaderCategory) {
            switch (shaderCategory) {
                case ShaderCategory::vertex:
                    return RHI::ShaderStage::Vertex;
                case ShaderCategory::fragment:
                    return RHI::ShaderStage::Fragment;
                case ShaderCategory::tessellationControl:
                    return RHI::ShaderStage::TessellationControl;
                case ShaderCategory::tessellationEvaluation:
                    return RHI::ShaderStage::TessellationEvaluation;
                case ShaderCategory::geometry:
                    return RHI::ShaderStage::Geometry;
                case ShaderCategory::compute:
                    return RHI::ShaderStage::Compute;
                case ShaderCategory::rayGen:
                    return RHI::ShaderStage::RayGen;
                case ShaderCategory::rayClosestHit:
                    return RHI::ShaderStage::RayClosestHit;
                case ShaderCategory::rayMiss:
                case ShaderCategory::rayMiss2:
                    return RHI::ShaderStage::RayMiss;
                case ShaderCategory::rayAnyHit:
                    return RHI::ShaderStage::RayAnyHit;
                default:
                    return RHI::ShaderStage::None;
            }
        }

    private:
        using DeferredResource = std::variant<
            RenderBuffer,
            RenderTexture,
            RenderTarget,
            ShaderResource,
            PipelineResource,
            MaterialResource,
            MaterialInstance,
            MeshResource>;

        struct DeferredReleaseEntry {
            uint64_t releaseFrame{0};
            DeferredResource resource{};
        };

        template<typename T>
        struct ResourceSlot {
            uint32_t generation{1};
            bool alive{false};
            T resource{};
        };

        template<typename T>
        RenderResourceHandle RegisterResource(RenderResourceType type, T resource, std::vector<ResourceSlot<T>> &slots) {
            ResourceSlot<T> slot{};
            slot.alive = true;
            slot.resource = std::move(resource);
            const uint32_t id = static_cast<uint32_t>(slots.size() + 1);
            slot.resource.handle = {type, id, slot.generation};
            slots.push_back(std::move(slot));
            return slots.back().resource.handle;
        }

        template<typename T>
        RenderResourceHandle UpsertResource(
            RenderResourceType type,
            const std::string &debugName,
            T resource,
            std::vector<ResourceSlot<T>> &slots) {
            const RenderResourceHandle existing = FindByNameInSlots(debugName, slots);
            if (existing.IsValid()) {
                if (auto *existingResource = GetResource(existing, type, slots); existingResource != nullptr) {
                    RetireResource(std::move(*existingResource));
                    const RenderResourceHandle stableHandle = existingResource->handle;
                    *existingResource = std::move(resource);
                    existingResource->handle = stableHandle;
                    return stableHandle;
                }
            }
            return RegisterResource(type, std::move(resource), slots);
        }

        template<typename T>
        static T *GetResource(RenderResourceHandle handle, RenderResourceType expectedType, std::vector<ResourceSlot<T>> &slots) {
            if (!handle.IsValid() || handle.type != expectedType || handle.id == 0) {
                return nullptr;
            }
            const size_t index = static_cast<size_t>(handle.id - 1);
            if (index >= slots.size()) {
                return nullptr;
            }
            auto &slot = slots[index];
            if (!slot.alive || slot.generation != handle.generation) {
                return nullptr;
            }
            return &slot.resource;
        }

        template<typename T>
        static const T *GetResource(RenderResourceHandle handle, RenderResourceType expectedType, const std::vector<ResourceSlot<T>> &slots) {
            if (!handle.IsValid() || handle.type != expectedType || handle.id == 0) {
                return nullptr;
            }
            const size_t index = static_cast<size_t>(handle.id - 1);
            if (index >= slots.size()) {
                return nullptr;
            }
            const auto &slot = slots[index];
            if (!slot.alive || slot.generation != handle.generation) {
                return nullptr;
            }
            return &slot.resource;
        }

        template<typename T>
        bool DestroyResource(RenderResourceHandle handle, std::vector<ResourceSlot<T>> &slots) {
            if (!handle.IsValid() || handle.id == 0) {
                return false;
            }
            const size_t index = static_cast<size_t>(handle.id - 1);
            if (index >= slots.size()) {
                return false;
            }
            auto &slot = slots[index];
            if (!slot.alive || slot.generation != handle.generation) {
                return false;
            }
            slot.alive = false;
            ++slot.generation;
            RetireResource(std::move(slot.resource));
            slot.resource = {};
            return true;
        }

        template<typename T>
        void RetireAllResources(std::vector<ResourceSlot<T>> &slots) {
            for (auto &slot: slots) {
                if (!slot.alive) {
                    continue;
                }
                slot.alive = false;
                ++slot.generation;
                RetireResource(std::move(slot.resource));
                slot.resource = {};
            }
            slots.clear();
        }

        template<typename T>
        void RetireResource(T resource) {
            if (!resource.handle.IsValid() && resource.debugName.empty()) {
                return;
            }

            DeferredReleaseEntry entry{};
            entry.releaseFrame = m_frameSerial + DefaultDeferredReleaseLag;
            entry.resource = std::move(resource);
            m_deferredReleases.emplace_back(std::move(entry));
        }

        void ProcessDeferredReleases(bool forceAll) {
            for (auto it = m_deferredReleases.begin(); it != m_deferredReleases.end();) {
                if (!forceAll && it->releaseFrame > m_frameSerial) {
                    ++it;
                    continue;
                }
                it = m_deferredReleases.erase(it);
            }
        }

        template<typename T>
        static RenderResourceHandle FindByNameInSlots(const std::string &debugName, const std::vector<ResourceSlot<T>> &slots) {
            const auto entry = std::find_if(slots.begin(), slots.end(), [&](const ResourceSlot<T> &slot) {
                return slot.alive && slot.resource.debugName == debugName;
            });
            return entry == slots.end() ? InvalidRenderResourceHandle : entry->resource.handle;
        }

        template<typename T>
        static size_t CountAlive(const std::vector<ResourceSlot<T>> &slots) {
            return static_cast<size_t>(std::count_if(slots.begin(), slots.end(), [](const ResourceSlot<T> &slot) {
                return slot.alive;
            }));
        }

        std::vector<ResourceSlot<RenderBuffer>> m_buffers{};
        std::vector<ResourceSlot<RenderTexture>> m_textures{};
        std::vector<ResourceSlot<RenderTarget>> m_renderTargets{};
        std::vector<ResourceSlot<ShaderResource>> m_shaders{};
        std::vector<ResourceSlot<PipelineResource>> m_pipelines{};
        std::vector<ResourceSlot<MaterialResource>> m_materials{};
        std::vector<ResourceSlot<MaterialInstance>> m_materialInstances{};
        std::vector<ResourceSlot<MeshResource>> m_meshes{};
        std::deque<DeferredReleaseEntry> m_deferredReleases{};
        uint64_t m_frameSerial{0};
    };
}
