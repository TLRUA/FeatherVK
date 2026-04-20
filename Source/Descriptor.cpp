#include "Descriptor.h"
// std
#include <cassert>
#include <stdexcept>

namespace FeatherVK {
    namespace {
        const DescriptorSetLayout &RequireVulkanBindLayout(const RHI::RHIBindLayout &layout) {
            auto *vulkanLayout = dynamic_cast<const DescriptorSetLayout *>(&layout);
            if (vulkanLayout == nullptr) {
                throw std::runtime_error("RHI bind layout is not backed by the Vulkan backend");
            }
            return *vulkanLayout;
        }

        RHI::BindResourceType ToRhiBindResourceType(const VkDescriptorType descriptorType) {
            switch (descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return RHI::BindResourceType::UniformBuffer;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return RHI::BindResourceType::StorageBuffer;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    return RHI::BindResourceType::CombinedImageSampler;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    return RHI::BindResourceType::StorageImage;
#ifdef RAY_TRACING
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                    return RHI::BindResourceType::AccelerationStructure;
#endif
                default:
                    return RHI::BindResourceType::UniformBuffer;
            }
        }

        RHI::ShaderStage ToRhiShaderStage(const VkShaderStageFlags stageFlags) {
            RHI::ShaderStage mask = RHI::ShaderStage::None;
            if ((stageFlags & VK_SHADER_STAGE_VERTEX_BIT) != 0) {
                mask = mask | RHI::ShaderStage::Vertex;
            }
            if ((stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0) {
                mask = mask | RHI::ShaderStage::Fragment;
            }
            if ((stageFlags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) != 0) {
                mask = mask | RHI::ShaderStage::TessellationControl;
            }
            if ((stageFlags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) != 0) {
                mask = mask | RHI::ShaderStage::TessellationEvaluation;
            }
            if ((stageFlags & VK_SHADER_STAGE_GEOMETRY_BIT) != 0) {
                mask = mask | RHI::ShaderStage::Geometry;
            }
            if ((stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) != 0) {
                mask = mask | RHI::ShaderStage::Compute;
            }
#ifdef RAY_TRACING
            if ((stageFlags & VK_SHADER_STAGE_RAYGEN_BIT_KHR) != 0) {
                mask = mask | RHI::ShaderStage::RayGen;
            }
            if ((stageFlags & VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) != 0) {
                mask = mask | RHI::ShaderStage::RayClosestHit;
            }
            if ((stageFlags & VK_SHADER_STAGE_MISS_BIT_KHR) != 0) {
                mask = mask | RHI::ShaderStage::RayMiss;
            }
            if ((stageFlags & VK_SHADER_STAGE_ANY_HIT_BIT_KHR) != 0) {
                mask = mask | RHI::ShaderStage::RayAnyHit;
            }
#endif
            return mask;
        }
    }

    DescriptorSetLayout::Builder &DescriptorSetLayout::Builder::addBinding(
            uint32_t binding,
            VkDescriptorType descriptorType,
            VkShaderStageFlags stageFlags,
            uint32_t count) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = descriptorType;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;

        bindings.push_back(layoutBinding);
        return *this;
    }

    std::shared_ptr<DescriptorSetLayout> DescriptorSetLayout::Builder::build() const {
        return std::make_shared<DescriptorSetLayout>(Device, bindings);
    }

    const std::vector<VkDescriptorSetLayoutBinding> &
    DescriptorSetLayout::Builder::getBindings() const {
        return bindings;
    }

// *************** Descriptor Set Layout *********************

    DescriptorSetLayout::DescriptorSetLayout(
            class Device &Device, const std::vector<VkDescriptorSetLayoutBinding> &bindings)
            : Device{Device}, bindings{bindings} {
        m_rhiEntries.reserve(bindings.size());
        for (const auto &binding: bindings) {
            m_rhiEntries.push_back({
                binding.binding,
                ToRhiBindResourceType(binding.descriptorType),
                ToRhiShaderStage(binding.stageFlags),
                binding.descriptorCount});
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
        descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetLayoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(
                Device.device(),
                &descriptorSetLayoutInfo,
                nullptr,
                &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    DescriptorSetLayout::~DescriptorSetLayout() {
        vkDestroyDescriptorSetLayout(Device.device(), descriptorSetLayout, nullptr);
    }

// *************** Descriptor Pool Builder *********************

    DescriptorPool::Builder &DescriptorPool::Builder::addPoolSize(
            VkDescriptorType descriptorType, uint32_t count) {
        poolSizes.push_back(VkDescriptorPoolSize{descriptorType, count});
        return *this;
    }

    DescriptorPool::Builder &DescriptorPool::Builder::setPoolFlags(
            VkDescriptorPoolCreateFlags flags) {
        poolFlags = flags;
        return *this;
    }

    DescriptorPool::Builder &DescriptorPool::Builder::setMaxSets(uint32_t count) {
        maxSets = count;
        return *this;
    }

    std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
        return std::make_unique<DescriptorPool>(Device, maxSets, poolFlags, poolSizes);
    }

// *************** Descriptor Pool *********************

    DescriptorPool::DescriptorPool(
            class Device &Device,
            uint32_t maxSets,
            VkDescriptorPoolCreateFlags poolFlags,
            const std::vector<VkDescriptorPoolSize> &poolSizes)
            : Device{Device} {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = maxSets;
        descriptorPoolInfo.flags = poolFlags;

        if (vkCreateDescriptorPool(Device.device(), &descriptorPoolInfo, nullptr, &descriptorPool) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    DescriptorPool::~DescriptorPool() {
        vkDestroyDescriptorPool(Device.device(), descriptorPool, nullptr);
    }

    bool DescriptorPool::allocateDescriptor(
            VkDescriptorSetLayout descriptorSetLayout, std::shared_ptr<VkDescriptorSet> &descriptorPtr) const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;

        if (vkAllocateDescriptorSets(Device.device(), &allocInfo, descriptorPtr.get()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor set");
            return false;
        }
        return true;
    }

    void DescriptorPool::freeDescriptors(std::vector<VkDescriptorSet> &descriptors) const {
        vkFreeDescriptorSets(
                Device.device(),
                descriptorPool,
                static_cast<uint32_t>(descriptors.size()),
                descriptors.data());
    }

    void DescriptorPool::resetPool() {
        vkResetDescriptorPool(Device.device(), descriptorPool, 0);
    }

// *************** Descriptor Update Plan *********************

    DescriptorUpdatePlan::DescriptorUpdatePlan(std::shared_ptr<DescriptorSetLayout> setLayout)
        : m_setLayout(std::move(setLayout)) {}

    void DescriptorUpdatePlan::Reset(std::shared_ptr<DescriptorSetLayout> setLayout) {
        m_setLayout = std::move(setLayout);
        m_bufferInfoStorage.clear();
        m_bufferArrayStorage.clear();
        m_imageInfoStorage.clear();
        m_imageArrayStorage.clear();
#ifdef RAY_TRACING
        m_tlasInfoStorage.clear();
#endif
        m_writes.clear();
    }

    const VkDescriptorSetLayoutBinding &DescriptorUpdatePlan::GetBinding(uint32_t binding) const {
        if (m_setLayout == nullptr || binding >= m_setLayout->bindings.size()) {
            throw std::runtime_error("Descriptor write binding is not present in the descriptor set layout");
        }
        return m_setLayout->bindings[binding];
    }

    void DescriptorUpdatePlan::WriteBuffer(uint32_t binding, const VkDescriptorBufferInfo &bufferInfo) {
        const auto &bindingDescription = GetBinding(binding);
        m_bufferInfoStorage.push_back(bufferInfo);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pBufferInfo = &m_bufferInfoStorage.back();
        write.descriptorCount = 1;
        m_writes.push_back(write);
    }

    void DescriptorUpdatePlan::WriteBuffers(uint32_t binding, const std::vector<VkDescriptorBufferInfo> &bufferInfos) {
        const auto &bindingDescription = GetBinding(binding);
        m_bufferArrayStorage.push_back(bufferInfos);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pBufferInfo = m_bufferArrayStorage.back().data();
        write.descriptorCount = static_cast<uint32_t>(m_bufferArrayStorage.back().size());
        m_writes.push_back(write);
    }

    void DescriptorUpdatePlan::WriteImage(uint32_t binding, const VkDescriptorImageInfo &imageInfo) {
        const auto &bindingDescription = GetBinding(binding);
        m_imageInfoStorage.push_back(imageInfo);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pImageInfo = &m_imageInfoStorage.back();
        write.descriptorCount = 1;
        m_writes.push_back(write);
    }

    void DescriptorUpdatePlan::WriteImages(uint32_t binding, const std::vector<VkDescriptorImageInfo> &imageInfos) {
        const auto &bindingDescription = GetBinding(binding);
        m_imageArrayStorage.push_back(imageInfos);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pImageInfo = m_imageArrayStorage.back().data();
        write.descriptorCount = static_cast<uint32_t>(m_imageArrayStorage.back().size());
        m_writes.push_back(write);
    }

#ifdef RAY_TRACING

    void DescriptorUpdatePlan::WriteTLAS(
        uint32_t binding,
        const VkWriteDescriptorSetAccelerationStructureKHR &accelerationStructureInfo) {
        const auto &bindingDescription = GetBinding(binding);
        m_tlasInfoStorage.push_back(accelerationStructureInfo);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.pNext = &m_tlasInfoStorage.back();
        m_writes.push_back(write);
    }

#endif

    void DescriptorUpdatePlan::Update(Device &device, VkDescriptorSet set) {
        if (m_writes.empty()) {
            return;
        }

        for (auto &write: m_writes) {
            write.dstSet = set;
        }
        vkUpdateDescriptorSets(
            device.device(),
            static_cast<uint32_t>(m_writes.size()),
            m_writes.data(),
            0,
            nullptr);
    }

// *************** Descriptor Writer *********************

    DescriptorWriter::DescriptorWriter(std::shared_ptr<DescriptorSetLayout> setLayout, DescriptorPool &pool)
            : setLayout{std::move(setLayout)}, pool{pool}, m_updatePlan{this->setLayout} {}

    DescriptorWriter &DescriptorWriter::writeBuffer(
            uint32_t binding, std::shared_ptr<VkDescriptorBufferInfo> bufferInfo) {
        if (bufferInfo != nullptr) {
            m_updatePlan.WriteBuffer(binding, *bufferInfo);
        }
        return *this;
    }

    DescriptorWriter &DescriptorWriter::writeBuffers(uint32_t binding, std::vector<VkDescriptorBufferInfo> &bufferInfos) {
        m_updatePlan.WriteBuffers(binding, bufferInfos);
        return *this;
    }

    DescriptorWriter &DescriptorWriter::writeImage(
            uint32_t binding, const std::shared_ptr<VkDescriptorImageInfo> &imageInfo) {
        if (imageInfo != nullptr) {
            m_updatePlan.WriteImage(binding, *imageInfo);
        }
        return *this;
    }

    DescriptorWriter &DescriptorWriter::writeImages(uint32_t binding, std::vector<VkDescriptorImageInfo> &imageInfos) {
        m_updatePlan.WriteImages(binding, imageInfos);
        return *this;
    }

#ifdef RAY_TRACING

    DescriptorWriter &DescriptorWriter::writeTLAS(uint32_t binding,
                                                  std::shared_ptr<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfo) {
        if (accelerationStructureInfo != nullptr) {
            m_updatePlan.WriteTLAS(binding, *accelerationStructureInfo);
        }
        return *this;
    }

#endif

    bool DescriptorWriter::build(std::shared_ptr<VkDescriptorSet> &setPtr) {
        bool success = pool.allocateDescriptor(setLayout->getDescriptorSetLayout(), setPtr);
        if (!success) {
            return false;
        }
        overwrite(*setPtr);
        return true;
    }

    bool DescriptorWriter::build(std::shared_ptr<DescriptorSetHandle> &setPtr) {
        auto rawSet = std::make_shared<VkDescriptorSet>();
        if (!build(rawSet)) {
            return false;
        }
        setPtr = std::make_shared<DescriptorSetHandle>(setLayout, rawSet);
        return true;
    }

    void DescriptorWriter::overwrite(VkDescriptorSet &set) {
        m_updatePlan.Update(pool.Device, set);
    }

    void DescriptorWriter::overwrite(DescriptorSetHandle &set) {
        auto descriptorSet = set.GetVkDescriptorSet();
        overwrite(descriptorSet);
    }

    VkDescriptorSetLayout GetVkDescriptorSetLayout(const RHI::RHIBindLayout &layout) {
        return RequireVulkanBindLayout(layout).getDescriptorSetLayout();
    }

    std::vector<VkDescriptorSetLayout> CollectVkDescriptorSetLayouts(const std::vector<std::shared_ptr<RHI::RHIBindLayout>> &layouts) {
        std::vector<VkDescriptorSetLayout> vkLayouts{};
        vkLayouts.reserve(layouts.size());
        for (const auto &layout: layouts) {
            if (layout != nullptr) {
                vkLayouts.push_back(GetVkDescriptorSetLayout(*layout));
            }
        }
        return vkLayouts;
    }

} 

