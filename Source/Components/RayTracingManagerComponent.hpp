#pragma once

#include "Component.hpp"

// Deprecated compatibility component kept for existing scene data.
// Global RT resources are owned by ResourceManager/RenderManager, not by this scene entity.
#ifdef RAY_TRACING
namespace FeatherVK {
    class RayTracingManagerComponent : public Component {
    public:
        ~RayTracingManagerComponent() override = default;

        RayTracingManagerComponent() {
            name = "RayTracingManagerComponent";
        }

        explicit RayTracingManagerComponent(const rapidjson::Value &) {
            name = "RayTracingManagerComponent";
        }

        void OnLoad(id_t, ECS::SceneRegistry &) override {
        }

        void Loaded(id_t, ECS::SceneRegistry &) override {
        }

        void LateUpdate(const ComponentUpdateInfo &) override {
            // RenderManager::SyncRayTracingScene owns TLAS updates globally.
        }
    };
}

#endif
