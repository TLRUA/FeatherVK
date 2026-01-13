#pragma once

#include "../Utils/Utils.hpp"
#include "BLAS.hpp"
#include <cstdlib>
#include <iostream>

#ifdef RAY_TRACING
namespace FeatherVK {
    class TLAS {
    public:
        inline static constexpr int DeferredDestroyFrameCount = 3;

        struct RetiredTlasResource {
            VkAccelerationStructureKHR accelerationStructure{VK_NULL_HANDLE};
            std::unique_ptr<Buffer> buffer{};
            int framesRemaining{DeferredDestroyFrameCount};
        };

        inline static bool shouldUpdate = false;
        inline static VkAccelerationStructureKHR tlas{};

        static void release() {
            DestroyRetiredResources(true);
            if (tlas != VK_NULL_HANDLE) {
                Device::pfn_vkDestroyAccelerationStructureKHR(Device::getDeviceSingleton()->device(), tlas, nullptr);
                tlas = VK_NULL_HANDLE;
            }
            tlasBuffer.reset();
            instances.clear();
            tlasIdToInstanceIndexMap.clear();
            shouldUpdate = false;
        }

        static void ProcessDeferredDestroy() {
            DestroyRetiredResources(false);
        }

        static bool updateTLAS(id_t tlasId, glm::mat4 translation, uint32_t mask = 0xFF){
            const auto mapEntry = tlasIdToInstanceIndexMap.find(tlasId);
            if (mapEntry == tlasIdToInstanceIndexMap.end()) {
                return false;
            }

            id_t instanceIndex = mapEntry->second;
            if (instanceIndex < 0 || static_cast<size_t>(instanceIndex) >= instances.size()) {
                return false;
            }
            instances[instanceIndex].transform = Utils::GlmMatrixToVulkanMatrix(translation);
            instances[instanceIndex].mask = mask;
            shouldUpdate = true;
            return true;
        }

        static bool HasTLASInstance(id_t tlasId) {
            const auto mapEntry = tlasIdToInstanceIndexMap.find(tlasId);
            if (mapEntry == tlasIdToInstanceIndexMap.end()) {
                return false;
            }

            const id_t instanceIndex = mapEntry->second;
            return instanceIndex >= 0 && static_cast<size_t>(instanceIndex) < instances.size();
        }

        static bool TryGetShaderOffset(id_t tlasId, uint32_t &shaderOffset) {
            const auto mapEntry = tlasIdToInstanceIndexMap.find(tlasId);
            if (mapEntry == tlasIdToInstanceIndexMap.end()) {
                return false;
            }

            const id_t instanceIndex = mapEntry->second;
            if (instanceIndex < 0 || static_cast<size_t>(instanceIndex) >= instances.size()) {
                return false;
            }

            shaderOffset = instances[instanceIndex].instanceShaderBindingTableRecordOffset;
            return true;
        }

        static auto createTLAS(Model &model, id_t tlasId, id_t shaderOffset, glm::mat4 translation = glm::mat4{1.f}) {
            if (HasTLASInstance(tlasId)) {
                return;
            }
            const auto blasEntry = BLAS::blasBuildInfoMap.find(model.getIndexReference());
            if (blasEntry == BLAS::blasBuildInfoMap.end() || blasEntry->second == nullptr || blasEntry->second->as == VK_NULL_HANDLE) {
                std::cerr << "[TLAS] missing BLAS for modelIndex=" << model.getIndexReference()
                          << " tlasId=" << tlasId << "\n";
                return;
            }
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = Utils::GlmMatrixToVulkanMatrix(translation);
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.instanceCustomIndex = tlasId;
            instance.accelerationStructureReference = Device::getDeviceSingleton()->getAccelerationStructureAddressKHR(blasEntry->second->as);
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = shaderOffset;
            if (DiagnosticsEnabled()) {
                std::cerr << "[TLAS] create instance tlasId=" << tlasId
                          << " shaderOffset=" << shaderOffset
                          << " blasAddress=" << instance.accelerationStructureReference
                          << " model='" << model.GetName() << "'"
                          << " modelIndex=" << model.getIndexReference() << "\n";
            }
            instances.emplace_back(instance);
            tlasIdToInstanceIndexMap[tlasId] = instances.size() - 1;
        }

        //Motion for motion blur, not be used for now
        static bool buildTLAS(
                VkBuildAccelerationStructureFlagBitsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                bool update = false, bool motion = false) {
            uint32_t instanceCount = static_cast<uint32_t>(instances.size());
            const uint32_t debugInstanceLimit = GetDebugInstanceLimit();
            if (debugInstanceLimit > 0) {
                instanceCount = std::min(instanceCount, debugInstanceLimit);
            }
            if (instanceCount == 0) {
                return false;
            }

            auto device = Device::getDeviceSingleton();

            auto commandBuffer = device->beginSingleTimeCommands();
            const VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;

            auto instanceBuffer = std::make_unique<Buffer>(
                    *device,
                    instanceBufferSize,
                    1,
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            instanceBuffer->map();
            instanceBuffer->writeToBuffer(instances.data(), instanceBufferSize);
            instanceBuffer->flush(instanceBufferSize);

            VkBufferDeviceAddressInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            bufferInfo.buffer = instanceBuffer->getBuffer();
            VkDeviceAddress instanceAddress = vkGetBufferDeviceAddress(device->device(), &bufferInfo);

            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0,
                    1, &barrier,
                    0, nullptr,
                    0, nullptr
            );

            auto scratchBuffer = cmdCreateTLAS(commandBuffer, instanceAddress, instanceCount, flags, update, motion);

            device->endSingleTimeCommands(commandBuffer, update ? "tlas_update" : "tlas_build");
            return s_lastBuildRecreatedHandle;
        }

    private:
        inline static std::vector<VkAccelerationStructureInstanceKHR> instances{};
        inline static std::unordered_map<id_t, id_t> tlasIdToInstanceIndexMap{};
        inline static std::unique_ptr<Buffer> tlasBuffer{};
        inline static std::vector<RetiredTlasResource> retiredTlasResources{};
        inline static bool s_lastBuildRecreatedHandle = false;

        static uint32_t GetDebugInstanceLimit() {
            static const uint32_t limit = []() -> uint32_t {
                if (const char *envValue = std::getenv("FEATHERVK_TLAS_INSTANCE_LIMIT")) {
                    return static_cast<uint32_t>(std::max(0, std::atoi(envValue)));
                }
                return 0;
            }();
            return limit;
        }

        static bool DiagnosticsEnabled() {
            static const bool enabled = []() {
                if (const char *envValue = std::getenv("FEATHERVK_RT_DIAGNOSTICS")) {
                    return envValue[0] == '1';
                }
                return false;
            }();
            return enabled;
        }

        static void DestroyRetiredResources(bool forceAll) {
            auto &device = *Device::getDeviceSingleton();
            for (auto it = retiredTlasResources.begin(); it != retiredTlasResources.end();) {
                if (!forceAll && --it->framesRemaining > 0) {
                    ++it;
                    continue;
                }

                if (it->accelerationStructure != VK_NULL_HANDLE) {
                    Device::pfn_vkDestroyAccelerationStructureKHR(device.device(), it->accelerationStructure, nullptr);
                }
                it = retiredTlasResources.erase(it);
            }
        }

        static std::unique_ptr<Buffer> cmdCreateTLAS(VkCommandBuffer &commandBuffer, VkDeviceAddress instanceBufferDeviceAddress,
                                  uint32_t instanceCount,
                                  VkBuildAccelerationStructureFlagsKHR flags, bool update, bool motion) {
            VkAccelerationStructureGeometryInstancesDataKHR instancesData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
            instancesData.arrayOfPointers = VK_FALSE;
            instancesData.data.deviceAddress = instanceBufferDeviceAddress;

            VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geometry.geometry.instances = instancesData;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            buildInfo.flags = flags | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            Device::pfn_vkGetAccelerationStructureBuildSizesKHR(
                    Device::getDeviceSingleton()->device(),
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                    &buildInfo,
                    &instanceCount,
                    &sizeInfo
            );
            if (DiagnosticsEnabled()) {
                std::cerr << "[TLAS] build instances=" << instanceCount
                          << " update=" << (update ? 1 : 0)
                          << " limit=" << GetDebugInstanceLimit()
                          << " asSize=" << sizeInfo.accelerationStructureSize
                          << " scratchSize=" << sizeInfo.buildScratchSize
                          << " instanceBufferAddress=" << instanceBufferDeviceAddress << "\n";
            }

            const bool shouldBuildFresh = !update || tlas == VK_NULL_HANDLE || tlasBuffer == nullptr;
            const bool canUseUpdatePath = update && !shouldBuildFresh;
            buildInfo.mode = canUseUpdatePath ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                              : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

            s_lastBuildRecreatedHandle = false;
            if (shouldBuildFresh) {
                VkAccelerationStructureKHR previousTlas = tlas;
                std::unique_ptr<Buffer> previousTlasBuffer = std::move(tlasBuffer);

                VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                createInfo.size = sizeInfo.accelerationStructureSize;
                auto newTlasBuffer = std::make_unique<Buffer>(
                        *Device::getDeviceSingleton(),
                        sizeInfo.accelerationStructureSize, 1,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                );
                createInfo.buffer = newTlasBuffer->getBuffer();
                VkAccelerationStructureKHR newTlas = VK_NULL_HANDLE;
                Device::pfn_vkCreateAccelerationStructureKHR(
                        Device::getDeviceSingleton()->device(),
                        &createInfo,
                        nullptr,
                        &newTlas
                );

                tlas = newTlas;
                tlasBuffer = std::move(newTlasBuffer);
                s_lastBuildRecreatedHandle = true;

                if (previousTlas != VK_NULL_HANDLE || previousTlasBuffer != nullptr) {
                    retiredTlasResources.emplace_back(RetiredTlasResource{
                            previousTlas,
                            std::move(previousTlasBuffer),
                            DeferredDestroyFrameCount});
                }
            }

            auto scratchBuffer = std::make_unique<Buffer>(
                    *Device::getDeviceSingleton(),
                    sizeInfo.buildScratchSize, 1,
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            VkBufferDeviceAddressInfo bufferDeviceAddressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            bufferDeviceAddressInfo.buffer = scratchBuffer->getBuffer();
            VkDeviceAddress scratchBufferDeviceAddress = vkGetBufferDeviceAddress(Device::getDeviceSingleton()->device(), &bufferDeviceAddressInfo);

            if (canUseUpdatePath) {
                buildInfo.srcAccelerationStructure = tlas;
            } else {
                buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
            }
            buildInfo.dstAccelerationStructure = tlas;
            buildInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{instanceCount, 0, 0, 0};
            VkAccelerationStructureBuildRangeInfoKHR *pBuildRangeInfo = &buildRangeInfo;

            Device::pfn_vkCmdBuildAccelerationStructuresKHR(
                    commandBuffer,
                    1,
                    &buildInfo,
                    &pBuildRangeInfo
            );
            return scratchBuffer;
        }
    };


}

#endif
