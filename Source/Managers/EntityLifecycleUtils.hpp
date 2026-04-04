#pragma once

#include "../ECS/SceneRegistry.hpp"
#include "../StructureInfos.h"
#include "EntityCommandService.hpp"

namespace FeatherVK::EntityLifecycle {
    inline bool IsPendingDestroy(const EntityCommandService *entityCommandService, id_t entityId) {
        return entityCommandService != nullptr && entityCommandService->IsPendingDestroy(entityId);
    }

    inline bool IsPendingDestroy(const FrameInfo &frameInfo, id_t entityId) {
        return IsPendingDestroy(frameInfo.entityCommandService, entityId);
    }

    inline bool IsAlive(const ECS::SceneRegistry *sceneRegistry,
                        const EntityCommandService *entityCommandService,
                        id_t entityId) {
        return sceneRegistry != nullptr &&
               sceneRegistry->IsAlive(entityId) &&
               !IsPendingDestroy(entityCommandService, entityId);
    }

    inline bool IsAlive(const FrameInfo &frameInfo, id_t entityId) {
        return IsAlive(frameInfo.sceneRegistry, frameInfo.entityCommandService, entityId);
    }

    inline bool IsActive(const ECS::SceneRegistry *sceneRegistry,
                         const EntityCommandService *entityCommandService,
                         id_t entityId) {
        return IsAlive(sceneRegistry, entityCommandService, entityId) &&
               sceneRegistry->IsEntityActive(entityId);
    }

    inline bool IsActive(const FrameInfo &frameInfo, id_t entityId) {
        return IsActive(frameInfo.sceneRegistry, frameInfo.entityCommandService, entityId);
    }

    template<typename T>
    inline bool TryGetLiveComponent(const FrameInfo &frameInfo, id_t entityId, T *&component) {
        component = nullptr;
        return IsAlive(frameInfo, entityId) &&
               frameInfo.sceneRegistry != nullptr &&
               frameInfo.sceneRegistry->TryGetComponent(entityId, component) &&
               component != nullptr;
    }
}
