#pragma once

#include <optional>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "HierarchyService.hpp"

namespace FeatherVK {
    class TransformService {
    public:
        void Reset() {
            m_dirtyEntities.clear();
            m_dirtyEntitySet.clear();
        }

        bool SetParent(ECS::SceneRegistry &sceneRegistry,
                       HierarchyService &hierarchyService,
                       id_t entityId,
                       std::optional<id_t> parentEntityId) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            if (parentEntityId.has_value() && !sceneRegistry.IsAlive(*parentEntityId)) {
                parentEntityId.reset();
            }

            if (parentEntityId.has_value()) {
                transform->SetParentEntityId(*parentEntityId);
            } else {
                transform->ClearParentEntityId();
            }

            hierarchyService.AttachEntity(entityId, parentEntityId);
            MarkDirty(entityId);
            return true;
        }

        bool ClearParent(ECS::SceneRegistry &sceneRegistry,
                         HierarchyService &hierarchyService,
                         id_t entityId) {
            return SetParent(sceneRegistry, hierarchyService, entityId, std::nullopt);
        }

        bool SetTranslation(ECS::SceneRegistry &sceneRegistry, id_t entityId, const glm::vec3 &translation) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            transform->SetTranslation(translation);
            MarkDirty(entityId);
            return true;
        }

        bool SetScale(ECS::SceneRegistry &sceneRegistry, id_t entityId, const glm::vec3 &scale) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            transform->SetScale(scale);
            MarkDirty(entityId);
            return true;
        }

        bool SetRotation(ECS::SceneRegistry &sceneRegistry, id_t entityId, const glm::vec3 &rotation) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            transform->SetRotation(rotation);
            MarkDirty(entityId);
            return true;
        }

        bool Translate(ECS::SceneRegistry &sceneRegistry, id_t entityId, const glm::vec3 &delta) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            transform->Translate(delta);
            MarkDirty(entityId);
            return true;
        }

        bool Rotate(ECS::SceneRegistry &sceneRegistry, id_t entityId, const glm::vec3 &delta) {
            auto *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return false;
            }

            transform->Rotate(delta);
            MarkDirty(entityId);
            return true;
        }

        void MarkDirty(id_t entityId) {
            if (m_dirtyEntitySet.insert(entityId).second) {
                m_dirtyEntities.push_back(entityId);
            }
        }

        std::vector<id_t> ConsumeDirtyEntities() {
            std::vector<id_t> dirtyEntities = std::move(m_dirtyEntities);
            m_dirtyEntities.clear();
            m_dirtyEntitySet.clear();
            return dirtyEntities;
        }

        void PublishResolvedDirtyEntities(std::vector<id_t> dirtyEntities) {
            m_resolvedDirtyEntities = std::move(dirtyEntities);
        }

        std::vector<id_t> ConsumeResolvedDirtyEntities() {
            std::vector<id_t> dirtyEntities = std::move(m_resolvedDirtyEntities);
            m_resolvedDirtyEntities.clear();
            return dirtyEntities;
        }

    private:
        std::vector<id_t> m_dirtyEntities{};
        std::unordered_set<id_t> m_dirtyEntitySet{};
        std::vector<id_t> m_resolvedDirtyEntities{};
    };
}
