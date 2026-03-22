#pragma once

#include <string>
#include <memory>
#include <vector>
#include <rapidjson/document.h>

#include "Imgui/imgui.h"
#include "../StructureInfos.h"
#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class GameObject;
    class TransformComponent;

    namespace ECS {
        class SceneRegistry;
    }

    struct ComponentAwakeInfo {
        GameObject *gameObject = nullptr;
        id_t entityId = 0;
        ECS::SceneRegistry *sceneRegistry = nullptr;
        TransformComponent *transform = nullptr;
    };

    struct ComponentUpdateInfo {
        GameObject *gameObject = nullptr;
        id_t entityId = 0;
        ECS::SceneRegistry *sceneRegistry = nullptr;
        TransformComponent *transform = nullptr;
        FrameInfo *frameInfo = nullptr;
        RendererInfo *rendererInfo = nullptr;
    };

    const float FIXED_UPDATE_INTERVAL = 0.02f;

    class Component {
    public:
        virtual std::string GetName() { return name; }

        virtual ~Component() = default;

        virtual void OnLoad(GameObject *gameObject) {}

        virtual void Loaded(GameObject *gameObject) {}

        virtual void OnLoad(id_t entityId, ECS::SceneRegistry &sceneRegistry) {
            (void)entityId;
            (void)sceneRegistry;
            OnLoad(static_cast<GameObject *>(nullptr));
        }

        virtual void Loaded(id_t entityId, ECS::SceneRegistry &sceneRegistry) {
            (void)entityId;
            (void)sceneRegistry;
            Loaded(static_cast<GameObject *>(nullptr));
        }

        virtual void Awake(const ComponentAwakeInfo &awakeInfo) {}

        virtual void Start(const ComponentUpdateInfo &updateInfo) {}

        virtual void Update(const ComponentUpdateInfo &updateInfo) {}

        virtual void LateUpdate(const ComponentUpdateInfo &updateInfo) {}

        virtual void FixedUpdate(const ComponentUpdateInfo &updateInfo) {}

        virtual void LateFixedUpdate(const ComponentUpdateInfo &updateInfo) {}

        virtual void OnDisable(const ComponentUpdateInfo &updateInfo) {}

        virtual void OnEnable(const ComponentUpdateInfo &updateInfo) {}

#ifdef RAY_TRACING
        virtual void SetUI(std::vector<GameObjectDesc> *, FrameInfo &frameInfo) {}
#else
        virtual void SetUI(Material::Map *, FrameInfo &frameInfo) {}
#endif

    protected:
        std::string name = "Component";
    };
}


