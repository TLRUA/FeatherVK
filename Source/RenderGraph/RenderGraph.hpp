#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../StructureInfos.h"

namespace FeatherVK {
    class Renderer;
}

namespace FeatherVK::RenderGraph {
    inline constexpr size_t InvalidPassIndex = std::numeric_limits<size_t>::max();

    enum class ResourceType : uint8_t {
        Unknown,
        Texture,
        Buffer
    };

    enum class ResourceAccess : uint8_t {
        Read,
        Write,
        ReadWrite
    };

    enum class PassType : uint8_t {
        Generic,
        Graphics,
        Compute,
        RayTracing,
        Transfer,
        Present
    };

    enum class ResourceUsage : uint32_t {
        None = 0,
        Sampled = 1u << 0,
        Storage = 1u << 1,
        ColorAttachment = 1u << 2,
        DepthStencilAttachment = 1u << 3,
        TransferSource = 1u << 4,
        TransferDestination = 1u << 5,
        Present = 1u << 6,
        AccelerationStructure = 1u << 7,
        UniformBuffer = 1u << 8,
        StorageBuffer = 1u << 9
    };

    [[nodiscard]] constexpr uint32_t ToUsageMask(ResourceUsage usage) {
        return static_cast<uint32_t>(usage);
    }

    [[nodiscard]] constexpr uint32_t operator|(ResourceUsage lhs, ResourceUsage rhs) {
        return ToUsageMask(lhs) | ToUsageMask(rhs);
    }

    [[nodiscard]] constexpr uint32_t operator|(uint32_t lhs, ResourceUsage rhs) {
        return lhs | ToUsageMask(rhs);
    }

    [[nodiscard]] constexpr bool HasUsage(uint32_t usageMask, ResourceUsage usage) {
        return (usageMask & ToUsageMask(usage)) != 0;
    }

    enum class RenderGraphResourceState : uint8_t {
        Unknown,
        Undefined,
        ShaderRead,
        ShaderWrite,
        ShaderReadWrite,
        ColorAttachmentWrite,
        DepthStencilWrite,
        TransferRead,
        TransferWrite,
        Present,
        AccelerationStructureRead,
        AccelerationStructureWrite,
        HostWrite
    };

    struct RenderGraphResourceHandle {
        inline static constexpr uint32_t InvalidId = 0;

        uint32_t id{InvalidId};
        ResourceType type{ResourceType::Unknown};

        [[nodiscard]] bool IsValid() const {
            return id != InvalidId && type != ResourceType::Unknown;
        }

        friend bool operator==(const RenderGraphResourceHandle &lhs, const RenderGraphResourceHandle &rhs) {
            return lhs.id == rhs.id && lhs.type == rhs.type;
        }
    };

    struct RenderGraphTextureDesc {
        VkExtent2D extent{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        uint32_t layers{1};
        uint32_t mipLevels{1};
        uint32_t usageMask{0};
        RenderGraphResourceState initialState{RenderGraphResourceState::Undefined};
        RenderGraphResourceState finalState{RenderGraphResourceState::Unknown};
        bool external{true};
        std::string debugName{};
    };

    struct RenderGraphBufferDesc {
        size_t size{0};
        uint32_t usageMask{0};
        RenderGraphResourceState initialState{RenderGraphResourceState::Unknown};
        RenderGraphResourceState finalState{RenderGraphResourceState::Unknown};
        bool external{true};
        std::string debugName{};
    };

    struct RenderGraphResourceUse {
        RenderGraphResourceHandle handle{};
        ResourceAccess access{ResourceAccess::Read};
        RenderGraphResourceState requiredState{RenderGraphResourceState::Unknown};
        std::string name{};
    };

    struct RenderGraphCompiledPass {
        size_t sourcePassIndex{InvalidPassIndex};
        std::string name{};
        PassType type{PassType::Generic};
    };

    struct RenderGraphResourceLifetime {
        RenderGraphResourceHandle handle{};
        std::string name{};
        ResourceType type{ResourceType::Unknown};
        bool external{false};
        bool transient{false};
        size_t firstUse{InvalidPassIndex};
        size_t lastUse{InvalidPassIndex};
        size_t firstWriter{InvalidPassIndex};
        size_t lastReader{InvalidPassIndex};
        std::vector<size_t> producers{};
        std::vector<size_t> consumers{};
        RenderGraphResourceState initialState{RenderGraphResourceState::Unknown};
        RenderGraphResourceState finalState{RenderGraphResourceState::Unknown};

        [[nodiscard]] bool HasUse() const {
            return firstUse != InvalidPassIndex;
        }
    };

    struct RenderGraphBarrier {
        enum class Timing : uint8_t {
            BeforePass,
            AfterPass
        };

        size_t passIndex{InvalidPassIndex};
        std::string passName{};
        RenderGraphResourceHandle handle{};
        std::string resourceName{};
        ResourceAccess access{ResourceAccess::Read};
        RenderGraphResourceState oldState{RenderGraphResourceState::Unknown};
        RenderGraphResourceState newState{RenderGraphResourceState::Unknown};
        Timing timing{Timing::BeforePass};
    };

    struct RenderGraphImageBinding {
        VkImage image{VK_NULL_HANDLE};
        VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bool enableBarriers{true};
        std::unordered_map<RenderGraphResourceState, VkImageLayout> layoutOverrides{};
    };

    struct RenderGraphGraphicsPassSignature {
        std::string key{};
        std::vector<VkFormat> colorFormats{};
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        VkSampleCountFlagBits sampleCount{VK_SAMPLE_COUNT_1_BIT};
    };

    struct RenderGraphGraphicsPipelineTarget {
        RenderGraphGraphicsPassSignature signature{};
        VkRenderPass compatibleRenderPass{VK_NULL_HANDLE};
    };

    enum class GraphicsPassTargetKind : uint8_t {
        Swapchain,
        SceneColor,
        Shadow,
        Picking,
        Gizmo
    };

    struct RenderGraphGraphicsPassDesc {
        GraphicsPassTargetKind target{GraphicsPassTargetKind::SceneColor};
        VkExtent2D extent{};
        bool useSceneViewport{false};
        bool endFrame{false};
        bool renderImGuiAtEnd{false};
        std::vector<VkClearValue> clearValues{};
        RenderGraphGraphicsPassSignature signature{};
    };

    class RenderGraphResourceBindings {
    public:
        void ReserveImages(size_t count) {
            m_images.reserve(count);
            m_imageHandleToIndex.reserve(count);
            m_imageNameToIndex.reserve(count);
        }

        void BindImage(RenderGraphResourceHandle handle, std::string name, RenderGraphImageBinding binding) {
            if (!handle.IsValid() || handle.type != ResourceType::Texture) {
                return;
            }

            const auto existing = m_imageHandleToIndex.find(handle.id);
            if (existing != m_imageHandleToIndex.end()) {
                m_images[existing->second] = std::move(binding);
                m_imageNameToIndex[name] = existing->second;
                return;
            }

            const size_t index = m_images.size();
            m_images.push_back(std::move(binding));
            m_imageHandleToIndex.emplace(handle.id, index);
            m_imageNameToIndex.emplace(std::move(name), index);
        }

        [[nodiscard]] const RenderGraphImageBinding *FindImage(const std::string &name) const {
            const auto entry = m_imageNameToIndex.find(name);
            return entry == m_imageNameToIndex.end() ? nullptr : &m_images[entry->second];
        }

        [[nodiscard]] const RenderGraphImageBinding *FindImage(RenderGraphResourceHandle handle) const {
            if (!handle.IsValid() || handle.type != ResourceType::Texture) {
                return nullptr;
            }

            const auto entry = m_imageHandleToIndex.find(handle.id);
            return entry == m_imageHandleToIndex.end() ? nullptr : &m_images[entry->second];
        }

    private:
        std::vector<RenderGraphImageBinding> m_images{};
        std::unordered_map<uint32_t, size_t> m_imageHandleToIndex{};
        std::unordered_map<std::string, size_t> m_imageNameToIndex{};
    };

    class RenderGraphBlackboard {
    public:
        template<typename T>
        void Set(std::string key, T value) {
            m_values[std::move(key)] = std::move(value);
        }

        template<typename T>
        [[nodiscard]] T *TryGet(const std::string &key) {
            const auto entry = m_values.find(key);
            if (entry == m_values.end()) {
                return nullptr;
            }
            return std::any_cast<T>(&entry->second);
        }

        template<typename T>
        [[nodiscard]] const T *TryGet(const std::string &key) const {
            const auto entry = m_values.find(key);
            if (entry == m_values.end()) {
                return nullptr;
            }
            return std::any_cast<T>(&entry->second);
        }

        void Clear() {
            m_values.clear();
        }

    private:
        std::unordered_map<std::string, std::any> m_values{};
    };

    class RenderGraphPassBuilder;
    struct RenderGraphPass;

    struct RenderGraphPassContext {
        FrameInfo &frameInfo;
        Renderer &renderer;
        RenderGraphBlackboard &blackboard;
        const RenderGraphPass &pass;
        const RenderGraphCompiledPass &compiledPass;
        const RenderGraphGraphicsPassDesc *graphicsPass{nullptr};
        VkRenderPass renderPass{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkExtent2D renderExtent{};
    };

    struct RenderGraphPass {
        using ExecuteCallback = std::function<void(RenderGraphPassContext &)>;

        std::string name{};
        PassType type{PassType::Generic};
        std::vector<RenderGraphResourceUse> resources{};
        std::optional<RenderGraphGraphicsPassDesc> graphicsPass{};
        bool forceLive{false};
        ExecuteCallback execute{};
    };

    class RenderGraph {
    public:
        using SetupCallback = std::function<void(RenderGraphPassBuilder &)>;
        using ExecuteCallback = RenderGraphPass::ExecuteCallback;

        RenderGraphResourceHandle ImportTexture(std::string name, RenderGraphTextureDesc desc = {}) {
            desc.external = true;
            desc.debugName = name;
            if (desc.initialState == RenderGraphResourceState::Undefined) {
                desc.initialState = RenderGraphResourceState::Unknown;
            }
            return GetOrCreateTexture(std::move(name), std::move(desc));
        }

        RenderGraphResourceHandle ImportBuffer(std::string name, RenderGraphBufferDesc desc = {}) {
            desc.external = true;
            desc.debugName = name;
            return GetOrCreateBuffer(std::move(name), std::move(desc));
        }

        RenderGraphResourceHandle DeclareTexture(std::string name, RenderGraphTextureDesc desc = {}) {
            desc.external = false;
            desc.debugName = name;
            return GetOrCreateTexture(std::move(name), std::move(desc));
        }

        RenderGraphResourceHandle DeclareBuffer(std::string name, RenderGraphBufferDesc desc = {}) {
            desc.external = false;
            desc.debugName = name;
            return GetOrCreateBuffer(std::move(name), std::move(desc));
        }

        void AddPass(std::string name, PassType type, SetupCallback setup, ExecuteCallback execute);

        void Compile() {
            ValidatePasses();
            BuildResourceLifetimes();

            const auto livePasses = BuildLivePassMask();
            BuildResourceLifetimes(&livePasses);
            BuildExecutionOrder(livePasses);
            BuildTransitionPlan();
            BuildBarrierBuckets();

            m_compiled = true;
        }

        [[nodiscard]] bool IsCompiled() const {
            return m_compiled;
        }

        [[nodiscard]] const std::vector<RenderGraphPass> &GetPasses() const {
            return m_passes;
        }

        [[nodiscard]] const std::vector<RenderGraphCompiledPass> &GetCompiledPasses() const {
            return m_compiledPasses;
        }

        [[nodiscard]] const std::vector<RenderGraphResourceLifetime> &GetResourceLifetimes() const {
            return m_resourceLifetimes;
        }

        [[nodiscard]] const std::vector<RenderGraphBarrier> &GetTransitionPlan() const {
            return m_transitionPlan;
        }

        [[nodiscard]] const std::vector<size_t> &GetBarrierBucket(size_t passIndex, RenderGraphBarrier::Timing timing) const {
            static const std::vector<size_t> empty{};
            const auto &buckets = timing == RenderGraphBarrier::Timing::BeforePass
                                      ? m_beforePassBarrierBuckets
                                      : m_afterPassBarrierBuckets;
            if (passIndex >= buckets.size()) {
                return empty;
            }
            return buckets[passIndex];
        }

        RenderGraphBlackboard &GetBlackboard() {
            return m_blackboard;
        }

        [[nodiscard]] const RenderGraphBlackboard &GetBlackboard() const {
            return m_blackboard;
        }

        [[nodiscard]] RenderGraphResourceHandle TryFindResource(const std::string &name, ResourceType expectedType) const {
            return FindResource(name, expectedType);
        }

    private:
        friend class RenderGraphPassBuilder;

        struct TextureResource {
            std::string name{};
            RenderGraphTextureDesc desc{};
        };

        struct BufferResource {
            std::string name{};
            RenderGraphBufferDesc desc{};
        };

        static bool IsRead(ResourceAccess access) {
            return access == ResourceAccess::Read || access == ResourceAccess::ReadWrite;
        }

        static bool IsWrite(ResourceAccess access) {
            return access == ResourceAccess::Write || access == ResourceAccess::ReadWrite;
        }

        RenderGraphResourceHandle GetOrCreateTexture(std::string name, RenderGraphTextureDesc desc) {
            const auto existing = m_resourceNameToHandle.find(name);
            if (existing != m_resourceNameToHandle.end()) {
                if (existing->second.type != ResourceType::Texture) {
                    throw std::runtime_error("RenderGraph resource type mismatch for texture: " + name);
                }
                return existing->second;
            }

            const uint32_t id = m_nextResourceId++;
            RenderGraphResourceHandle handle{id, ResourceType::Texture};
            m_textures.emplace(id, TextureResource{name, std::move(desc)});
            m_resourceNameToHandle.emplace(std::move(name), handle);
            m_compiled = false;
            return handle;
        }

        RenderGraphResourceHandle GetOrCreateBuffer(std::string name, RenderGraphBufferDesc desc) {
            const auto existing = m_resourceNameToHandle.find(name);
            if (existing != m_resourceNameToHandle.end()) {
                if (existing->second.type != ResourceType::Buffer) {
                    throw std::runtime_error("RenderGraph resource type mismatch for buffer: " + name);
                }
                return existing->second;
            }

            const uint32_t id = m_nextResourceId++;
            RenderGraphResourceHandle handle{id, ResourceType::Buffer};
            m_buffers.emplace(id, BufferResource{name, std::move(desc)});
            m_resourceNameToHandle.emplace(std::move(name), handle);
            m_compiled = false;
            return handle;
        }

        RenderGraphResourceHandle FindResource(const std::string &name, ResourceType expectedType) const {
            const auto entry = m_resourceNameToHandle.find(name);
            if (entry == m_resourceNameToHandle.end() || entry->second.type != expectedType) {
                return {};
            }
            return entry->second;
        }

        [[nodiscard]] bool IsExternalResource(RenderGraphResourceHandle handle) const {
            if (handle.type == ResourceType::Texture) {
                const auto entry = m_textures.find(handle.id);
                return entry != m_textures.end() && entry->second.desc.external;
            }
            if (handle.type == ResourceType::Buffer) {
                const auto entry = m_buffers.find(handle.id);
                return entry != m_buffers.end() && entry->second.desc.external;
            }
            return false;
        }

        [[nodiscard]] uint32_t GetUsageMask(RenderGraphResourceHandle handle) const {
            if (handle.type == ResourceType::Texture) {
                const auto entry = m_textures.find(handle.id);
                return entry == m_textures.end() ? 0 : entry->second.desc.usageMask;
            }
            if (handle.type == ResourceType::Buffer) {
                const auto entry = m_buffers.find(handle.id);
                return entry == m_buffers.end() ? 0 : entry->second.desc.usageMask;
            }
            return 0;
        }

        [[nodiscard]] std::string GetResourceName(RenderGraphResourceHandle handle) const {
            if (handle.type == ResourceType::Texture) {
                const auto entry = m_textures.find(handle.id);
                return entry == m_textures.end() ? std::string{} : entry->second.name;
            }
            if (handle.type == ResourceType::Buffer) {
                const auto entry = m_buffers.find(handle.id);
                return entry == m_buffers.end() ? std::string{} : entry->second.name;
            }
            return {};
        }

        [[nodiscard]] RenderGraphResourceState GetInitialState(RenderGraphResourceHandle handle) const {
            if (handle.type == ResourceType::Texture) {
                const auto entry = m_textures.find(handle.id);
                return entry == m_textures.end() ? RenderGraphResourceState::Unknown : entry->second.desc.initialState;
            }
            if (handle.type == ResourceType::Buffer) {
                const auto entry = m_buffers.find(handle.id);
                return entry == m_buffers.end() ? RenderGraphResourceState::Unknown : entry->second.desc.initialState;
            }
            return RenderGraphResourceState::Unknown;
        }

        [[nodiscard]] RenderGraphResourceState GetRequestedFinalState(RenderGraphResourceHandle handle) const {
            if (handle.type == ResourceType::Texture) {
                const auto entry = m_textures.find(handle.id);
                return entry == m_textures.end() ? RenderGraphResourceState::Unknown : entry->second.desc.finalState;
            }
            if (handle.type == ResourceType::Buffer) {
                const auto entry = m_buffers.find(handle.id);
                return entry == m_buffers.end() ? RenderGraphResourceState::Unknown : entry->second.desc.finalState;
            }
            return RenderGraphResourceState::Unknown;
        }

        RenderGraphResourceState InferRequiredState(const RenderGraphResourceUse &resourceUse, PassType passType) const {
            if (resourceUse.requiredState != RenderGraphResourceState::Unknown) {
                return resourceUse.requiredState;
            }

            const uint32_t usageMask = GetUsageMask(resourceUse.handle);
            if (resourceUse.handle.type == ResourceType::Texture) {
                if (passType == PassType::Present || HasUsage(usageMask, ResourceUsage::Present)) {
                    return RenderGraphResourceState::Present;
                }
                if (IsWrite(resourceUse.access) && HasUsage(usageMask, ResourceUsage::DepthStencilAttachment)) {
                    return RenderGraphResourceState::DepthStencilWrite;
                }
                if (IsWrite(resourceUse.access) && passType == PassType::Graphics) {
                    return RenderGraphResourceState::ColorAttachmentWrite;
                }
                if (resourceUse.access == ResourceAccess::ReadWrite) {
                    return RenderGraphResourceState::ShaderReadWrite;
                }
                return IsWrite(resourceUse.access) ? RenderGraphResourceState::ShaderWrite : RenderGraphResourceState::ShaderRead;
            }

            if (resourceUse.handle.type == ResourceType::Buffer) {
                if (HasUsage(usageMask, ResourceUsage::AccelerationStructure)) {
                    return IsWrite(resourceUse.access)
                               ? RenderGraphResourceState::AccelerationStructureWrite
                               : RenderGraphResourceState::AccelerationStructureRead;
                }
                if (resourceUse.access == ResourceAccess::ReadWrite) {
                    return RenderGraphResourceState::ShaderReadWrite;
                }
                if (resourceUse.access == ResourceAccess::Write && passType == PassType::Generic) {
                    return RenderGraphResourceState::HostWrite;
                }
                return IsWrite(resourceUse.access) ? RenderGraphResourceState::ShaderWrite : RenderGraphResourceState::ShaderRead;
            }

            return RenderGraphResourceState::Unknown;
        }

        void ValidatePasses() const {
            for (const auto &pass: m_passes) {
                if (!pass.execute) {
                    throw std::runtime_error("RenderGraph pass has no execute callback: " + pass.name);
                }
                for (const auto &resourceUse: pass.resources) {
                    if (!resourceUse.handle.IsValid()) {
                        throw std::runtime_error("RenderGraph pass uses invalid resource: " + pass.name + " -> " + resourceUse.name);
                    }
                }
            }
        }

        RenderGraphResourceLifetime &EnsureLifetime(RenderGraphResourceHandle handle) {
            const auto existing = m_resourceLifetimeIndexById.find(handle.id);
            if (existing != m_resourceLifetimeIndexById.end()) {
                return m_resourceLifetimes[existing->second];
            }

            RenderGraphResourceLifetime lifetime{};
            lifetime.handle = handle;
            lifetime.name = GetResourceName(handle);
            lifetime.type = handle.type;
            lifetime.external = IsExternalResource(handle);
            lifetime.transient = !lifetime.external;
            lifetime.initialState = GetInitialState(handle);
            lifetime.finalState = GetRequestedFinalState(handle);

            const size_t index = m_resourceLifetimes.size();
            m_resourceLifetimeIndexById.emplace(handle.id, index);
            m_resourceLifetimes.push_back(std::move(lifetime));
            return m_resourceLifetimes.back();
        }

        void BuildResourceLifetimes(const std::vector<bool> *livePasses = nullptr) {
            m_resourceLifetimes.clear();
            m_resourceLifetimeIndexById.clear();

            for (const auto &[id, texture]: m_textures) {
                (void) texture;
                EnsureLifetime({id, ResourceType::Texture});
            }
            for (const auto &[id, buffer]: m_buffers) {
                (void) buffer;
                EnsureLifetime({id, ResourceType::Buffer});
            }

            for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex) {
                if (livePasses != nullptr && !(*livePasses)[passIndex]) {
                    continue;
                }

                for (const auto &resourceUse: m_passes[passIndex].resources) {
                    auto &lifetime = EnsureLifetime(resourceUse.handle);
                    lifetime.firstUse = std::min(lifetime.firstUse, passIndex);
                    lifetime.lastUse = std::max(lifetime.lastUse, passIndex);

                    if (IsWrite(resourceUse.access)) {
                        if (lifetime.firstWriter == InvalidPassIndex) {
                            lifetime.firstWriter = passIndex;
                        }
                        lifetime.producers.push_back(passIndex);
                    }
                    if (IsRead(resourceUse.access)) {
                        lifetime.lastReader = std::max(lifetime.lastReader, passIndex);
                        lifetime.consumers.push_back(passIndex);
                    }
                }
            }
        }

        [[nodiscard]] std::vector<bool> BuildLivePassMask() const {
            std::vector<bool> live(m_passes.size(), false);

            for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex) {
                const auto &pass = m_passes[passIndex];
                bool forceLive = pass.forceLive || pass.type == PassType::Present || pass.resources.empty();
                for (const auto &resourceUse: pass.resources) {
                    if (IsWrite(resourceUse.access) && IsExternalResource(resourceUse.handle)) {
                        forceLive = true;
                        break;
                    }
                }
                live[passIndex] = forceLive;
            }

            bool changed = true;
            while (changed) {
                changed = false;
                for (size_t passIndex = m_passes.size(); passIndex > 0; --passIndex) {
                    const size_t currentPassIndex = passIndex - 1;
                    if (!live[currentPassIndex]) {
                        continue;
                    }

                    for (const auto &resourceUse: m_passes[currentPassIndex].resources) {
                        if (!IsRead(resourceUse.access)) {
                            continue;
                        }

                        const auto lifetimeIt = m_resourceLifetimeIndexById.find(resourceUse.handle.id);
                        if (lifetimeIt == m_resourceLifetimeIndexById.end()) {
                            continue;
                        }

                        const auto &lifetime = m_resourceLifetimes[lifetimeIt->second];
                        for (const size_t producerPassIndex: lifetime.producers) {
                            if (producerPassIndex >= currentPassIndex || live[producerPassIndex]) {
                                continue;
                            }
                            live[producerPassIndex] = true;
                            changed = true;
                        }
                    }
                }
            }

            return live;
        }

        void BuildExecutionOrder(const std::vector<bool> &livePasses) {
            m_executionOrder.clear();
            m_compiledPasses.clear();
            m_executionOrder.reserve(m_passes.size());
            m_compiledPasses.reserve(m_passes.size());

            for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex) {
                if (!livePasses[passIndex]) {
                    continue;
                }
                m_executionOrder.push_back(passIndex);
                m_compiledPasses.push_back({passIndex, m_passes[passIndex].name, m_passes[passIndex].type});
            }
        }

        void BuildTransitionPlan() {
            m_transitionPlan.clear();
            std::unordered_map<uint32_t, RenderGraphResourceState> currentStates{};
            currentStates.reserve(m_resourceLifetimes.size());

            for (const auto &lifetime: m_resourceLifetimes) {
                currentStates[lifetime.handle.id] = lifetime.initialState;
            }

            for (const size_t passIndex: m_executionOrder) {
                const auto &pass = m_passes[passIndex];
                for (const auto &resourceUse: pass.resources) {
                    const RenderGraphResourceState desiredState = InferRequiredState(resourceUse, pass.type);
                    const RenderGraphResourceState oldState = currentStates[resourceUse.handle.id];
                    if (desiredState != RenderGraphResourceState::Unknown && oldState != desiredState) {
                        m_transitionPlan.push_back({
                            passIndex,
                            pass.name,
                            resourceUse.handle,
                            resourceUse.name,
                            resourceUse.access,
                            oldState,
                            desiredState,
                            RenderGraphBarrier::Timing::BeforePass
                        });
                        currentStates[resourceUse.handle.id] = desiredState;
                    }
                }
            }

            for (auto &lifetime: m_resourceLifetimes) {
                if (!lifetime.HasUse()) {
                    continue;
                }

                const RenderGraphResourceState currentState = currentStates[lifetime.handle.id];
                const RenderGraphResourceState requestedFinalState = GetRequestedFinalState(lifetime.handle);
                lifetime.finalState = requestedFinalState == RenderGraphResourceState::Unknown
                                          ? currentState
                                          : requestedFinalState;

                if (requestedFinalState != RenderGraphResourceState::Unknown && currentState != requestedFinalState) {
                    m_transitionPlan.push_back({
                        lifetime.lastUse,
                        lifetime.lastUse == InvalidPassIndex ? std::string{} : m_passes[lifetime.lastUse].name,
                        lifetime.handle,
                        lifetime.name,
                        ResourceAccess::ReadWrite,
                        currentState,
                        requestedFinalState,
                        RenderGraphBarrier::Timing::AfterPass
                    });
                    currentStates[lifetime.handle.id] = requestedFinalState;
                }
            }
        }

        void BuildBarrierBuckets() {
            m_beforePassBarrierBuckets.clear();
            m_afterPassBarrierBuckets.clear();
            m_beforePassBarrierBuckets.resize(m_passes.size());
            m_afterPassBarrierBuckets.resize(m_passes.size());

            for (size_t barrierIndex = 0; barrierIndex < m_transitionPlan.size(); ++barrierIndex) {
                const auto &barrier = m_transitionPlan[barrierIndex];
                if (barrier.passIndex == InvalidPassIndex || barrier.passIndex >= m_passes.size()) {
                    continue;
                }

                auto &bucket = barrier.timing == RenderGraphBarrier::Timing::BeforePass
                                   ? m_beforePassBarrierBuckets[barrier.passIndex]
                                   : m_afterPassBarrierBuckets[barrier.passIndex];
                bucket.push_back(barrierIndex);
            }
        }

        std::vector<RenderGraphPass> m_passes{};
        std::vector<size_t> m_executionOrder{};
        std::vector<RenderGraphCompiledPass> m_compiledPasses{};
        std::vector<RenderGraphResourceLifetime> m_resourceLifetimes{};
        std::unordered_map<uint32_t, size_t> m_resourceLifetimeIndexById{};
        std::vector<RenderGraphBarrier> m_transitionPlan{};
        std::vector<std::vector<size_t>> m_beforePassBarrierBuckets{};
        std::vector<std::vector<size_t>> m_afterPassBarrierBuckets{};
        RenderGraphBlackboard m_blackboard{};
        std::unordered_map<uint32_t, TextureResource> m_textures{};
        std::unordered_map<uint32_t, BufferResource> m_buffers{};
        std::unordered_map<std::string, RenderGraphResourceHandle> m_resourceNameToHandle{};
        uint32_t m_nextResourceId{1};
        bool m_compiled{false};
    };

    class RenderGraphPassBuilder {
    public:
        RenderGraphPassBuilder(RenderGraph &graph, RenderGraphPass &pass)
            : m_graph(graph), m_pass(pass) {}

        RenderGraphResourceHandle ReadTexture(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Texture, ResourceAccess::Read, requiredState);
        }

        RenderGraphResourceHandle WriteTexture(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Texture, ResourceAccess::Write, requiredState);
        }

        RenderGraphResourceHandle ReadWriteTexture(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Texture, ResourceAccess::ReadWrite, requiredState);
        }

        RenderGraphResourceHandle ReadBuffer(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Buffer, ResourceAccess::Read, requiredState);
        }

        RenderGraphResourceHandle WriteBuffer(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Buffer, ResourceAccess::Write, requiredState);
        }

        RenderGraphResourceHandle ReadWriteBuffer(
            const std::string &name,
            RenderGraphResourceState requiredState = RenderGraphResourceState::Unknown) {
            return UseResource(name, ResourceType::Buffer, ResourceAccess::ReadWrite, requiredState);
        }

        void SetGraphicsPass(RenderGraphGraphicsPassDesc graphicsPass) {
            m_pass.graphicsPass = std::move(graphicsPass);
        }

        void SetForceLive(bool enabled = true) {
            m_pass.forceLive = enabled;
        }

    private:
        RenderGraphResourceHandle UseResource(
            const std::string &name,
            ResourceType type,
            ResourceAccess access,
            RenderGraphResourceState requiredState) {
            RenderGraphResourceHandle handle = m_graph.FindResource(name, type);
            if (!handle.IsValid()) {
                handle = type == ResourceType::Texture
                             ? m_graph.DeclareTexture(name)
                             : m_graph.DeclareBuffer(name);
            }
            m_pass.resources.push_back({handle, access, requiredState, name});
            return handle;
        }

        RenderGraph &m_graph;
        RenderGraphPass &m_pass;
    };

    inline void RenderGraph::AddPass(std::string name, PassType type, SetupCallback setup, ExecuteCallback execute) {
        RenderGraphPass pass{};
        pass.name = std::move(name);
        pass.type = type;
        pass.execute = std::move(execute);

        RenderGraphPassBuilder builder{*this, pass};
        if (setup) {
            setup(builder);
        }

        m_passes.push_back(std::move(pass));
        m_compiled = false;
    }
}
