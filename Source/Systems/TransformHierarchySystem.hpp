#pragma once

#include <unordered_set>
#include <vector>

#include "../Components/TransformComponent.hpp"
#include "../Managers/TransformService.hpp"
#include "../Managers/HierarchyService.hpp"

namespace FeatherVK {
    class TransformHierarchySystem {
    public:
        void Update(ECS::SceneRegistry &sceneRegistry,
                    HierarchyService &hierarchyService,
                    TransformService &transformService) const {
            const std::vector<id_t> dirtyEntities = transformService.ConsumeDirtyEntities();
            if (dirtyEntities.empty()) {
                return;
            }

            std::unordered_set<id_t> dirtySet{dirtyEntities.begin(), dirtyEntities.end()};
            std::unordered_set<id_t> dirtyRoots{};

            for (const id_t entityId: dirtyEntities) {
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }

                TransformComponent *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
                if (transform == nullptr) {
                    continue;
                }

                id_t dirtyRootId = entityId;
                while (transform->HasParent()) {
                    const id_t parentEntityId = transform->GetParentEntityId();
                    if (!sceneRegistry.IsAlive(parentEntityId) || dirtySet.count(parentEntityId) == 0) {
                        break;
                    }

                    TransformComponent *parentTransform = sceneRegistry.TryGetComponent<TransformComponent>(parentEntityId);
                    if (parentTransform == nullptr) {
                        break;
                    }

                    dirtyRootId = parentEntityId;
                    transform = parentTransform;
                }

                dirtyRoots.emplace(dirtyRootId);
            }

            for (const id_t entityId: dirtyRoots) {
                if (!sceneRegistry.IsAlive(entityId)) {
                    continue;
                }

                TransformComponent *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
                if (transform == nullptr) {
                    continue;
                }

                TransformComponent *parentTransform = nullptr;
                if (transform->HasParent()) {
                    parentTransform = sceneRegistry.TryGetComponent<TransformComponent>(transform->GetParentEntityId());
                }
                UpdateSubtree(sceneRegistry, hierarchyService, transformService, entityId, parentTransform, true);
            }
        }

    private:
        static void UpdateSubtree(ECS::SceneRegistry &sceneRegistry,
                                  HierarchyService &hierarchyService,
                                  TransformService &transformService,
                                  id_t entityId,
                                  TransformComponent *parentTransform,
                                  bool parentDirty) {
            TransformComponent *transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
            if (transform == nullptr) {
                return;
            }

            glm::vec3 worldTranslation = transform->GetRelativeTranslation();
            glm::vec3 worldScale = transform->GetRelativeScale();
            glm::vec3 worldRotation = transform->GetRelativeRotation();

            if (transform->HasParent() &&
                (parentTransform == nullptr || !sceneRegistry.IsAlive(transform->GetParentEntityId()))) {
                transformService.ClearParent(sceneRegistry, hierarchyService, entityId);
                transform = sceneRegistry.TryGetComponent<TransformComponent>(entityId);
                parentTransform = nullptr;
            }

            if (parentTransform != nullptr) {
                worldTranslation += parentTransform->GetTranslation();
                worldScale *= parentTransform->GetScale();
                worldRotation += parentTransform->GetRotation();
            }

            const bool shouldUpdate = parentDirty || transform->IsDirty() || !transform->HasValidWorldTransform();
            if (shouldUpdate) {
                transform->SetWorldTransform(worldTranslation, worldScale, worldRotation);
            }

            for (const id_t childId: hierarchyService.GetTree().GetChildren(entityId)) {
                UpdateSubtree(sceneRegistry, hierarchyService, transformService, childId, transform, shouldUpdate);
            }
        }
    };
}
