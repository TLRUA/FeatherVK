#pragma once

#ifdef RAY_TRACING

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Buffer.h"
#include "../Descriptor.h"
#include "../Device.hpp"
#include "../Model.hpp"
#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class RayTracingSceneContext {
    public:
        inline static constexpr int DeferredDestroyFrameCount = 3;
        inline static constexpr bool EnableCompaction = false;

        struct RetiredTlasResource {
            VkAccelerationStructureKHR accelerationStructure{VK_NULL_HANDLE};
            std::unique_ptr<Buffer> buffer{};
            int framesRemaining{DeferredDestroyFrameCount};
        };

        explicit RayTracingSceneContext(Device &device) : m_device(device) {}

        ~RayTracingSceneContext() {
            Release();
        }

        RayTracingSceneContext(const RayTracingSceneContext &) = delete;
        RayTracingSceneContext &operator=(const RayTracingSceneContext &) = delete;

        id_t AllocateInstanceId() {
            return m_nextInstanceId++;
        }

        void Release() {
            DestroyRetiredTlasResources(true);

            if (m_tlas != VK_NULL_HANDLE) {
                Device::pfn_vkDestroyAccelerationStructureKHR(m_device.device(), m_tlas, nullptr);
                m_tlas = VK_NULL_HANDLE;
            }

            for (auto &blas: m_retiredBlases) {
                if (blas != VK_NULL_HANDLE) {
                    Device::pfn_vkDestroyAccelerationStructureKHR(m_device.device(), blas, nullptr);
                }
            }
            m_retiredBlases.clear();

            for (auto &entry: m_blasBuildInfoMap) {
                if (entry.second != nullptr && entry.second->accelerationStructure != VK_NULL_HANDLE) {
                    Device::pfn_vkDestroyAccelerationStructureKHR(m_device.device(), entry.second->accelerationStructure, nullptr);
                }
            }

            m_tlasBuffer.reset();
            m_blasBuffers.clear();
            m_blasBuildInfoMap.clear();
            m_pendingBlasInputs.clear();
            m_pendingModelIndexReferences.clear();
            m_instances.clear();
            m_instanceIdToIndexMap.clear();
            m_shouldUpdate = false;
            m_lastBuildRecreatedHandle = false;
            m_nextInstanceId = 0;
        }

        void ProcessDeferredDestroy() {
            DestroyRetiredTlasResources(false);
        }

        bool HasBlas(const std::shared_ptr<Model> &model) const {
            return model != nullptr && m_blasBuildInfoMap.find(model->getIndexReference()) != m_blasBuildInfoMap.end();
        }

        void QueueBlasBuild(const std::shared_ptr<Model> &model) {
            if (model == nullptr) {
                return;
            }

            const uint32_t modelIndexReference = model->getIndexReference();
            if (m_blasBuildInfoMap.find(modelIndexReference) != m_blasBuildInfoMap.end() ||
                m_pendingModelIndexReferences.find(modelIndexReference) != m_pendingModelIndexReferences.end()) {
                return;
            }

            VkAccelerationStructureGeometryTrianglesDataKHR triangleData{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
            triangleData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            triangleData.vertexData.deviceAddress = model->getVertexBuffer()->getDeviceAddress();
            triangleData.vertexStride = sizeof(Model::Vertex);
            triangleData.indexType = VK_INDEX_TYPE_UINT32;
            triangleData.indexData.deviceAddress = model->getIndexBuffer()->getDeviceAddress();
            triangleData.maxVertex = model->getVertexCount() - 1;

            VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles = triangleData;
            geometry.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
            buildRangeInfo.primitiveCount = model->getPrimitiveCount();

            PendingBlasInput input{};
            input.geometryArray = {geometry};
            input.buildRangeInfoArray = {buildRangeInfo};
            input.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            input.modelIndexReference = modelIndexReference;

            if (DiagnosticsEnabled()) {
                std::cerr << "[BLAS] enqueue model='" << model->GetName()
                          << "' modelIndex=" << modelIndexReference
                          << " vertices=" << model->getVertexCount()
                          << " indices=" << model->getIndexCount()
                          << " primitives=" << model->getPrimitiveCount()
                          << " vertexAddress=" << triangleData.vertexData.deviceAddress
                          << " indexAddress=" << triangleData.indexData.deviceAddress << "\n";
            }

            m_pendingModelIndexReferences.insert(modelIndexReference);
            m_pendingBlasInputs.emplace_back(std::move(input));
        }

        void EnsureBlasBuilt(const std::shared_ptr<Model> &model,
                             VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                                                                         VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR) {
            QueueBlasBuild(model);
            BuildPendingBlas(flags);
        }

        void BuildPendingBlas(VkBuildAccelerationStructureFlagsKHR flags) {
            if (m_pendingBlasInputs.empty()) {
                return;
            }

            const uint32_t blasCount = static_cast<uint32_t>(m_pendingBlasInputs.size());
            uint32_t compactionCount = 0;
            VkDeviceSize maxScratchSize = 0;

            std::vector<BlasBuildInfo *> buildInfos(blasCount, nullptr);
            for (uint32_t i = 0; i < blasCount; ++i) {
                auto info = std::make_unique<BlasBuildInfo>();
                BlasBuildInfo *infoPtr = info.get();
                buildInfos[i] = infoPtr;
                m_blasBuildInfoMap[m_pendingBlasInputs[i].modelIndexReference] = std::move(info);

                infoPtr->buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
                infoPtr->buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                infoPtr->buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                infoPtr->buildGeometryInfo.flags =
                    (flags & ~VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) |
                    (m_pendingBlasInputs[i].flags & ~VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
                infoPtr->buildGeometryInfo.geometryCount = static_cast<uint32_t>(m_pendingBlasInputs[i].geometryArray.size());
                infoPtr->buildGeometryInfo.pGeometries = m_pendingBlasInputs[i].geometryArray.data();
                infoPtr->buildRangeInfo = m_pendingBlasInputs[i].buildRangeInfoArray.data();

                std::vector<uint32_t> maxPrimCount(m_pendingBlasInputs[i].buildRangeInfoArray.size());
                for (size_t j = 0; j < m_pendingBlasInputs[i].buildRangeInfoArray.size(); ++j) {
                    maxPrimCount[j] = m_pendingBlasInputs[i].buildRangeInfoArray[j].primitiveCount;
                }

                infoPtr->buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
                Device::pfn_vkGetAccelerationStructureBuildSizesKHR(
                    m_device.device(),
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                    &infoPtr->buildGeometryInfo,
                    maxPrimCount.data(),
                    &infoPtr->buildSizesInfo);

                maxScratchSize = std::max(maxScratchSize, infoPtr->buildSizesInfo.buildScratchSize);
                if (EnableCompaction &&
                    (infoPtr->buildGeometryInfo.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
                    ++compactionCount;
                }

                if (DiagnosticsEnabled()) {
                    std::cerr << "[BLAS] size modelIndex=" << m_pendingBlasInputs[i].modelIndexReference
                              << " buildSize=" << infoPtr->buildSizesInfo.accelerationStructureSize
                              << " scratchSize=" << infoPtr->buildSizesInfo.buildScratchSize
                              << " flags=" << infoPtr->buildGeometryInfo.flags << "\n";
                }
            }

            auto scratchBuffer = std::make_unique<Buffer>(
                m_device,
                maxScratchSize,
                1,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            const VkDeviceAddress scratchBufferDeviceAddress = scratchBuffer->getDeviceAddress();

            VkQueryPool queryPool = VK_NULL_HANDLE;
            if (EnableCompaction && compactionCount > 0) {
                VkQueryPoolCreateInfo queryPoolCreateInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                queryPoolCreateInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
                queryPoolCreateInfo.queryCount = compactionCount;
                vkCreateQueryPool(m_device.device(), &queryPoolCreateInfo, nullptr, &queryPool);
            }

            std::vector<uint32_t> indicesToCreate{};
            VkDeviceSize batchSize = 0;
            constexpr VkDeviceSize BatchSizeLimit = 256ull * 1024ull * 1024ull;

            for (uint32_t i = 0; i < blasCount; ++i) {
                indicesToCreate.push_back(i);
                batchSize += buildInfos[i]->buildSizesInfo.accelerationStructureSize;
                if (batchSize <= BatchSizeLimit && i != blasCount - 1) {
                    continue;
                }

                VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();
                CmdCreateBlas(commandBuffer, indicesToCreate, buildInfos, scratchBufferDeviceAddress, queryPool);
                m_device.endSingleTimeCommands(commandBuffer, "blas_build");

                if (queryPool != VK_NULL_HANDLE) {
                    commandBuffer = m_device.beginSingleTimeCommands();
                    CmdCompactBlas(commandBuffer, indicesToCreate, buildInfos, queryPool);
                    m_device.endSingleTimeCommands(commandBuffer, "blas_compact");
                }

                indicesToCreate.clear();
                batchSize = 0;
            }

            if (queryPool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(m_device.device(), queryPool, nullptr);
            }

            m_pendingBlasInputs.clear();
            m_pendingModelIndexReferences.clear();
        }

        bool HasInstance(id_t instanceId) const {
            const auto mapEntry = m_instanceIdToIndexMap.find(instanceId);
            if (mapEntry == m_instanceIdToIndexMap.end()) {
                return false;
            }
            return mapEntry->second >= 0 && static_cast<size_t>(mapEntry->second) < m_instances.size();
        }

        void CreateInstance(Model &model,
                            id_t instanceId,
                            id_t shaderOffset,
                            const glm::mat4 &transform = glm::mat4{1.0f},
                            uint32_t mask = 0xFF) {
            if (HasInstance(instanceId)) {
                return;
            }

            const auto blasEntry = m_blasBuildInfoMap.find(model.getIndexReference());
            if (blasEntry == m_blasBuildInfoMap.end() || blasEntry->second == nullptr ||
                blasEntry->second->accelerationStructure == VK_NULL_HANDLE) {
                std::cerr << "[TLAS] missing BLAS for modelIndex=" << model.getIndexReference()
                          << " instanceId=" << instanceId << "\n";
                return;
            }

            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = Utils::GlmMatrixToVulkanMatrix(transform);
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.instanceCustomIndex = instanceId;
            instance.accelerationStructureReference =
                m_device.getAccelerationStructureAddressKHR(blasEntry->second->accelerationStructure);
            instance.mask = mask;
            instance.instanceShaderBindingTableRecordOffset = shaderOffset;

            if (DiagnosticsEnabled()) {
                std::cerr << "[TLAS] create instance instanceId=" << instanceId
                          << " shaderOffset=" << shaderOffset
                          << " blasAddress=" << instance.accelerationStructureReference
                          << " model='" << model.GetName() << "'"
                          << " modelIndex=" << model.getIndexReference() << "\n";
            }

            m_instances.emplace_back(instance);
            m_instanceIdToIndexMap[instanceId] = static_cast<id_t>(m_instances.size() - 1);
        }

        bool UpdateInstance(id_t instanceId, const glm::mat4 &transform, uint32_t mask = 0xFF) {
            const auto mapEntry = m_instanceIdToIndexMap.find(instanceId);
            if (mapEntry == m_instanceIdToIndexMap.end()) {
                return false;
            }

            const id_t index = mapEntry->second;
            if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) {
                return false;
            }

            m_instances[index].transform = Utils::GlmMatrixToVulkanMatrix(transform);
            m_instances[index].mask = mask;
            m_shouldUpdate = true;
            return true;
        }

        bool BuildTopLevel(VkBuildAccelerationStructureFlagBitsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                           bool update = false,
                           bool motion = false) {
            uint32_t instanceCount = static_cast<uint32_t>(m_instances.size());
            const uint32_t debugInstanceLimit = GetDebugInstanceLimit();
            if (debugInstanceLimit > 0) {
                instanceCount = std::min(instanceCount, debugInstanceLimit);
            }
            if (instanceCount == 0) {
                return false;
            }

            VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();
            const VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;

            auto instanceBuffer = std::make_unique<Buffer>(
                m_device,
                instanceBufferSize,
                1,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            instanceBuffer->map();
            instanceBuffer->writeToBuffer(m_instances.data(), instanceBufferSize);
            instanceBuffer->flush(instanceBufferSize);

            VkBufferDeviceAddressInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            bufferInfo.buffer = instanceBuffer->getBuffer();
            const VkDeviceAddress instanceAddress = vkGetBufferDeviceAddress(m_device.device(), &bufferInfo);

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
                0, nullptr);

            auto scratchBuffer = CmdCreateTlas(commandBuffer, instanceAddress, instanceCount, flags, update, motion);
            (void)scratchBuffer;

            m_device.endSingleTimeCommands(commandBuffer, update ? "tlas_update" : "tlas_build");
            return m_lastBuildRecreatedHandle;
        }

        bool ShouldUpdate() const { return m_shouldUpdate; }
        void ClearUpdateFlag() { m_shouldUpdate = false; }
        bool HasValidTlas() const { return m_tlas != VK_NULL_HANDLE && m_tlasBuffer != nullptr; }
        const VkAccelerationStructureKHR &GetTlasHandle() const { return m_tlas; }

    private:
        struct PendingBlasInput {
            std::vector<VkAccelerationStructureGeometryKHR> geometryArray{};
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfoArray{};
            VkBuildAccelerationStructureFlagsKHR flags{};
            uint32_t modelIndexReference{};
        };

        struct BlasBuildInfo {
            VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
            VkAccelerationStructureBuildRangeInfoKHR *buildRangeInfo = nullptr;
            VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
            VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        };

        static bool DiagnosticsEnabled() {
            static const bool enabled = []() {
                if (const char *envValue = std::getenv("FEATHERVK_RT_DIAGNOSTICS")) {
                    return envValue[0] == '1';
                }
                return false;
            }();
            return enabled;
        }

        static uint32_t GetDebugInstanceLimit() {
            static const uint32_t limit = []() -> uint32_t {
                if (const char *envValue = std::getenv("FEATHERVK_TLAS_INSTANCE_LIMIT")) {
                    return static_cast<uint32_t>(std::max(0, std::atoi(envValue)));
                }
                return 0;
            }();
            return limit;
        }

        void DestroyRetiredTlasResources(bool forceAll) {
            for (auto it = m_retiredTlasResources.begin(); it != m_retiredTlasResources.end();) {
                if (!forceAll && --it->framesRemaining > 0) {
                    ++it;
                    continue;
                }

                if (it->accelerationStructure != VK_NULL_HANDLE) {
                    Device::pfn_vkDestroyAccelerationStructureKHR(m_device.device(), it->accelerationStructure, nullptr);
                }
                it = m_retiredTlasResources.erase(it);
            }
        }

        void CmdCreateBlas(VkCommandBuffer commandBuffer,
                           const std::vector<uint32_t> &indices,
                           const std::vector<BlasBuildInfo *> &buildInfos,
                           VkDeviceAddress scratchBufferDeviceAddress,
                           VkQueryPool queryPool) {
            if (queryPool != VK_NULL_HANDLE) {
                vkResetQueryPool(m_device.device(), queryPool, 0, static_cast<uint32_t>(indices.size()));
            }

            uint32_t queryCount = 0;
            for (const uint32_t idx: indices) {
                VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                createInfo.size = buildInfos[idx]->buildSizesInfo.accelerationStructureSize;

                auto buffer = std::make_unique<Buffer>(
                    m_device,
                    createInfo.size,
                    1,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                createInfo.buffer = buffer->getBuffer();
                m_blasBuffers.emplace_back(std::move(buffer));

                Device::pfn_vkCreateAccelerationStructureKHR(
                    m_device.device(),
                    &createInfo,
                    nullptr,
                    &buildInfos[idx]->accelerationStructure);

                buildInfos[idx]->buildGeometryInfo.dstAccelerationStructure = buildInfos[idx]->accelerationStructure;
                buildInfos[idx]->buildGeometryInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

                Device::pfn_vkCmdBuildAccelerationStructuresKHR(
                    commandBuffer,
                    1,
                    &buildInfos[idx]->buildGeometryInfo,
                    &buildInfos[idx]->buildRangeInfo);

                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0,
                    1,
                    &barrier,
                    0,
                    nullptr,
                    0,
                    nullptr);

                if (queryPool != VK_NULL_HANDLE) {
                    Device::pfn_vkCmdWriteAccelerationStructuresPropertiesKHR(
                        commandBuffer,
                        1,
                        &buildInfos[idx]->accelerationStructure,
                        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                        queryPool,
                        queryCount++);
                }
            }
        }

        void CmdCompactBlas(VkCommandBuffer commandBuffer,
                            const std::vector<uint32_t> &indices,
                            const std::vector<BlasBuildInfo *> &buildInfos,
                            VkQueryPool queryPool) {
            std::vector<VkDeviceSize> compactSizes(indices.size());
            vkGetQueryPoolResults(
                m_device.device(),
                queryPool,
                0,
                static_cast<uint32_t>(compactSizes.size()),
                compactSizes.size() * sizeof(VkDeviceSize),
                compactSizes.data(),
                sizeof(VkDeviceSize),
                VK_QUERY_RESULT_WAIT_BIT);

            uint32_t queryCount = 0;
            for (const uint32_t idx: indices) {
                const VkAccelerationStructureKHR oldBlas = buildInfos[idx]->accelerationStructure;
                buildInfos[idx]->buildSizesInfo.accelerationStructureSize = compactSizes[queryCount++];

                VkAccelerationStructureCreateInfoKHR compactCreateInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                compactCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                compactCreateInfo.size = buildInfos[idx]->buildSizesInfo.accelerationStructureSize;

                auto compactBuffer = std::make_unique<Buffer>(
                    m_device,
                    compactCreateInfo.size,
                    1,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                compactCreateInfo.buffer = compactBuffer->getBuffer();
                m_blasBuffers.emplace_back(std::move(compactBuffer));

                Device::pfn_vkCreateAccelerationStructureKHR(
                    m_device.device(),
                    &compactCreateInfo,
                    nullptr,
                    &buildInfos[idx]->accelerationStructure);

                VkCopyAccelerationStructureInfoKHR copyInfo{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
                copyInfo.dst = buildInfos[idx]->accelerationStructure;
                copyInfo.src = oldBlas;
                copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                Device::pfn_vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
                m_retiredBlases.push_back(oldBlas);
            }
        }

        std::unique_ptr<Buffer> CmdCreateTlas(VkCommandBuffer commandBuffer,
                                              VkDeviceAddress instanceBufferDeviceAddress,
                                              uint32_t instanceCount,
                                              VkBuildAccelerationStructureFlagsKHR flags,
                                              bool update,
                                              bool motion) {
            (void)motion;

            VkAccelerationStructureGeometryInstancesDataKHR instancesData{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
            instancesData.arrayOfPointers = VK_FALSE;
            instancesData.data.deviceAddress = instanceBufferDeviceAddress;

            VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geometry.geometry.instances = instancesData;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            buildInfo.flags = flags | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            Device::pfn_vkGetAccelerationStructureBuildSizesKHR(
                m_device.device(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &buildInfo,
                &instanceCount,
                &sizeInfo);

            const bool shouldBuildFresh = !update || m_tlas == VK_NULL_HANDLE || m_tlasBuffer == nullptr;
            const bool canUseUpdatePath = update && !shouldBuildFresh;
            buildInfo.mode = canUseUpdatePath ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                              : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

            m_lastBuildRecreatedHandle = false;
            if (shouldBuildFresh) {
                VkAccelerationStructureKHR previousTlas = m_tlas;
                std::unique_ptr<Buffer> previousTlasBuffer = std::move(m_tlasBuffer);

                VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                createInfo.size = sizeInfo.accelerationStructureSize;
                auto newTlasBuffer = std::make_unique<Buffer>(
                    m_device,
                    sizeInfo.accelerationStructureSize,
                    1,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                createInfo.buffer = newTlasBuffer->getBuffer();

                VkAccelerationStructureKHR newTlas = VK_NULL_HANDLE;
                Device::pfn_vkCreateAccelerationStructureKHR(
                    m_device.device(),
                    &createInfo,
                    nullptr,
                    &newTlas);

                m_tlas = newTlas;
                m_tlasBuffer = std::move(newTlasBuffer);
                m_lastBuildRecreatedHandle = true;

                if (previousTlas != VK_NULL_HANDLE || previousTlasBuffer != nullptr) {
                    m_retiredTlasResources.emplace_back(RetiredTlasResource{
                        previousTlas,
                        std::move(previousTlasBuffer),
                        DeferredDestroyFrameCount});
                }
            }

            auto scratchBuffer = std::make_unique<Buffer>(
                m_device,
                sizeInfo.buildScratchSize,
                1,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VkBufferDeviceAddressInfo bufferDeviceAddressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            bufferDeviceAddressInfo.buffer = scratchBuffer->getBuffer();
            const VkDeviceAddress scratchBufferDeviceAddress =
                vkGetBufferDeviceAddress(m_device.device(), &bufferDeviceAddressInfo);

            buildInfo.srcAccelerationStructure = canUseUpdatePath ? m_tlas : VK_NULL_HANDLE;
            buildInfo.dstAccelerationStructure = m_tlas;
            buildInfo.scratchData.deviceAddress = scratchBufferDeviceAddress;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{instanceCount, 0, 0, 0};
            VkAccelerationStructureBuildRangeInfoKHR *buildRangeInfoPtr = &buildRangeInfo;

            Device::pfn_vkCmdBuildAccelerationStructuresKHR(
                commandBuffer,
                1,
                &buildInfo,
                &buildRangeInfoPtr);

            return scratchBuffer;
        }

        Device &m_device;

        id_t m_nextInstanceId = 0;
        bool m_shouldUpdate = false;
        bool m_lastBuildRecreatedHandle = false;

        std::vector<PendingBlasInput> m_pendingBlasInputs{};
        std::unordered_set<uint32_t> m_pendingModelIndexReferences{};
        std::unordered_map<uint32_t, std::unique_ptr<BlasBuildInfo>> m_blasBuildInfoMap{};
        std::vector<std::unique_ptr<Buffer>> m_blasBuffers{};
        std::vector<VkAccelerationStructureKHR> m_retiredBlases{};

        VkAccelerationStructureKHR m_tlas = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_tlasBuffer{};
        std::vector<VkAccelerationStructureInstanceKHR> m_instances{};
        std::unordered_map<id_t, id_t> m_instanceIdToIndexMap{};
        std::vector<RetiredTlasResource> m_retiredTlasResources{};
    };
}

#endif
