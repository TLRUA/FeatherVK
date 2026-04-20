#pragma once

#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "../Material.hpp"
#include "../RenderCore/RenderResourceHandle.hpp"
#include "../RenderMesh.hpp"
#include "../StructureInfos.h"
#include "../Utils/Utils.hpp"

namespace FeatherVK {
    struct RenderLightProxy {
        LightCategory lightCategory{LightCategory::NONE};
        glm::vec3 color{1.0f};
        float intensity{1.0f};
        float radius{1.0f};
    };

    struct RenderView {
        ViewportRect panelRect{};
        ViewportRect viewportRect{};
        VkExtent2D renderExtent{};
        float aspectRatio{1.0f};
    };

    struct RenderCamera {
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        id_t entityId{InvalidEntityId};
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 inverseViewMatrix{1.0f};
        glm::mat4 inverseProjectionMatrix{1.0f};
        glm::vec3 worldPosition{0.0f};
        float fovY{0.0f};
        float nearClip{0.0f};
        float farClip{0.0f};
        float aspectRatio{1.0f};
        bool valid{false};
    };

    struct RenderMeshInstance {
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        id_t entityId{InvalidEntityId};
        glm::mat4 worldTransform{1.0f};
        glm::mat3 normalMatrix{1.0f};
        RenderCore::RenderResourceHandle meshResource{};
        RenderCore::RenderResourceHandle materialResource{};
        RenderCore::RenderResourceHandle materialInstanceResource{};
        Material::id_t materialId{0};
        RenderMesh renderMesh{};
        std::optional<PBR> pbrOverride{};
        std::optional<RenderLightProxy> lightProxy{};
        std::string pipelineCategory{};
        unsigned int renderQueue{0};
        uint32_t renderLayer{0};
        bool active{false};
        bool visible{false};
        bool castShadow{false};
        bool receiveShadow{false};
        bool defaultRenderLayer{true};
        bool skyboxLike{false};
        bool overlayLike{false};
        bool lightPassLike{false};
        bool specialPipeline{false};
        id_t rayTracingInstanceId{std::numeric_limits<id_t>::max()};

        [[nodiscard]] bool IsRenderable() const {
            return active && visible && renderMesh.IsValid();
        }

        [[nodiscard]] bool IsDefaultLayerRenderable() const {
            return IsRenderable() && defaultRenderLayer;
        }

        [[nodiscard]] bool CastsShadowInDefaultLayer() const {
            return IsDefaultLayerRenderable() && castShadow;
        }

        [[nodiscard]] bool IsShadowCaster() const {
            return CastsShadowInDefaultLayer() && !skyboxLike && !overlayLike;
        }

        [[nodiscard]] bool IsRayTracingRenderable() const {
            return active && visible && meshResource.IsValid() && !skyboxLike && !overlayLike;
        }

        [[nodiscard]] bool IsPickable() const {
            return IsDefaultLayerRenderable() && !skyboxLike && !overlayLike;
        }

        [[nodiscard]] bool IsSkyboxLike() const {
            return skyboxLike;
        }

        [[nodiscard]] bool UsesSpecialPipeline() const {
            return specialPipeline;
        }

        [[nodiscard]] bool HasLightProxy() const {
            return lightProxy.has_value();
        }
    };

    struct RenderLightInstance {
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        id_t entityId{InvalidEntityId};
        LightCategory lightCategory{LightCategory::NONE};
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, 0.0f, 1.0f};
        glm::vec3 color{1.0f};
        float intensity{1.0f};
        float radius{1.0f};
        bool active{false};
    };

    struct RenderSceneStats {
        uint32_t meshInstanceCount{0};
        uint32_t visibleMeshInstanceCount{0};
        uint32_t lightCount{0};
        uint32_t activeLightCount{0};
    };

    class RenderScene {
    public:
        void Clear() {
            m_view = {};
            m_camera = {};
            m_meshInstances.clear();
            m_lightInstances.clear();
            m_meshEntityToIndex.clear();
            m_lightEntityToIndex.clear();
            m_stats = {};
        }

        void SetView(RenderView view) {
            m_view = view;
        }

        void SetCamera(RenderCamera camera) {
            m_camera = camera;
        }

        void ReserveMeshInstances(size_t count) {
            m_meshInstances.reserve(count);
            m_meshEntityToIndex.reserve(count);
        }

        void ReserveLightInstances(size_t count) {
            m_lightInstances.reserve(count);
            m_lightEntityToIndex.reserve(count);
        }

        void AddMeshInstance(RenderMeshInstance meshInstance) {
            if (meshInstance.visible) {
                ++m_stats.visibleMeshInstanceCount;
            }
            ++m_stats.meshInstanceCount;
            if (meshInstance.entityId != RenderMeshInstance::InvalidEntityId) {
                m_meshEntityToIndex[meshInstance.entityId] = m_meshInstances.size();
            }
            m_meshInstances.push_back(std::move(meshInstance));
        }

        void UpdateMeshInstance(RenderMeshInstance meshInstance) {
            const auto indexIt = m_meshEntityToIndex.find(meshInstance.entityId);
            if (indexIt == m_meshEntityToIndex.end() || indexIt->second >= m_meshInstances.size()) {
                AddMeshInstance(std::move(meshInstance));
                return;
            }

            auto &current = m_meshInstances[indexIt->second];
            if (current.visible) {
                --m_stats.visibleMeshInstanceCount;
            }
            if (meshInstance.visible) {
                ++m_stats.visibleMeshInstanceCount;
            }
            current = std::move(meshInstance);
        }

        void RemoveMeshInstance(id_t entityId) {
            const auto indexIt = m_meshEntityToIndex.find(entityId);
            if (indexIt == m_meshEntityToIndex.end() || indexIt->second >= m_meshInstances.size()) {
                return;
            }

            const size_t index = indexIt->second;
            if (m_meshInstances[index].visible) {
                --m_stats.visibleMeshInstanceCount;
            }
            --m_stats.meshInstanceCount;

            const size_t lastIndex = m_meshInstances.size() - 1;
            if (index != lastIndex) {
                m_meshInstances[index] = std::move(m_meshInstances[lastIndex]);
                if (m_meshInstances[index].entityId != RenderMeshInstance::InvalidEntityId) {
                    m_meshEntityToIndex[m_meshInstances[index].entityId] = index;
                }
            }
            m_meshInstances.pop_back();
            m_meshEntityToIndex.erase(indexIt);
        }

        void AddLightInstance(RenderLightInstance lightInstance) {
            if (lightInstance.active) {
                ++m_stats.activeLightCount;
            }
            ++m_stats.lightCount;
            if (lightInstance.entityId != RenderLightInstance::InvalidEntityId) {
                m_lightEntityToIndex[lightInstance.entityId] = m_lightInstances.size();
            }
            m_lightInstances.push_back(std::move(lightInstance));
        }

        void UpdateLightInstance(RenderLightInstance lightInstance) {
            const auto indexIt = m_lightEntityToIndex.find(lightInstance.entityId);
            if (indexIt == m_lightEntityToIndex.end() || indexIt->second >= m_lightInstances.size()) {
                AddLightInstance(std::move(lightInstance));
                return;
            }

            auto &current = m_lightInstances[indexIt->second];
            if (current.active) {
                --m_stats.activeLightCount;
            }
            if (lightInstance.active) {
                ++m_stats.activeLightCount;
            }
            current = std::move(lightInstance);
        }

        void RemoveLightInstance(id_t entityId) {
            const auto indexIt = m_lightEntityToIndex.find(entityId);
            if (indexIt == m_lightEntityToIndex.end() || indexIt->second >= m_lightInstances.size()) {
                return;
            }

            const size_t index = indexIt->second;
            if (m_lightInstances[index].active) {
                --m_stats.activeLightCount;
            }
            --m_stats.lightCount;

            const size_t lastIndex = m_lightInstances.size() - 1;
            if (index != lastIndex) {
                m_lightInstances[index] = std::move(m_lightInstances[lastIndex]);
                if (m_lightInstances[index].entityId != RenderLightInstance::InvalidEntityId) {
                    m_lightEntityToIndex[m_lightInstances[index].entityId] = index;
                }
            }
            m_lightInstances.pop_back();
            m_lightEntityToIndex.erase(indexIt);
        }

        [[nodiscard]] const RenderView &GetView() const { return m_view; }
        [[nodiscard]] const RenderCamera &GetCamera() const { return m_camera; }
        [[nodiscard]] const std::vector<RenderMeshInstance> &GetMeshInstances() const { return m_meshInstances; }
        [[nodiscard]] const std::vector<RenderLightInstance> &GetLightInstances() const { return m_lightInstances; }
        [[nodiscard]] const RenderSceneStats &GetStats() const { return m_stats; }

        [[nodiscard]] const RenderMeshInstance *FindMeshInstance(id_t entityId) const {
            const auto indexIt = m_meshEntityToIndex.find(entityId);
            if (indexIt == m_meshEntityToIndex.end() || indexIt->second >= m_meshInstances.size()) {
                return nullptr;
            }
            return &m_meshInstances[indexIt->second];
        }

        [[nodiscard]] const RenderLightInstance *FindLightInstance(id_t entityId) const {
            const auto indexIt = m_lightEntityToIndex.find(entityId);
            if (indexIt == m_lightEntityToIndex.end() || indexIt->second >= m_lightInstances.size()) {
                return nullptr;
            }
            return &m_lightInstances[indexIt->second];
        }

    private:
        RenderView m_view{};
        RenderCamera m_camera{};
        std::vector<RenderMeshInstance> m_meshInstances{};
        std::vector<RenderLightInstance> m_lightInstances{};
        std::unordered_map<id_t, size_t> m_meshEntityToIndex{};
        std::unordered_map<id_t, size_t> m_lightEntityToIndex{};
        RenderSceneStats m_stats{};
    };
}
