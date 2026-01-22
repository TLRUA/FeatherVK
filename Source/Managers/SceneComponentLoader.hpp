#pragma once

#include <string>

#include <glm/vec3.hpp>
#include <rapidjson/document.h>

#include "../Components/CameraComponent.hpp"
#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Components/UIComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "ModelRepository.hpp"

namespace FeatherVK {
    class SceneComponentLoader {
    public:
        explicit SceneComponentLoader(ModelRepository &modelRepository)
            : m_modelRepository(modelRepository) {}

        void Emplace(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            const std::string typeName = componentObject["type"].GetString();

            if (typeName == "MeshRendererComponent") {
                EmplaceMeshRenderer(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "LightComponent") {
                EmplaceLight(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "ObjectMovementComponent") {
                EmplaceObjectMovement(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "CameraMovementComponent") {
                EmplaceCameraMovement(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "CameraComponent") {
                sceneRegistry.EmplaceComponent<CameraComponent>(entityId);
                return;
            }
            if (typeName == "RigidBodyComponent") {
                EmplaceRigidBody(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "UIComponent") {
                EmplaceUi(sceneRegistry, entityId, componentObject);
                return;
            }
            if (typeName == "TransformComponent") {
                sceneRegistry.EmplaceComponent<TransformComponent>(entityId);
                return;
            }

            throw std::runtime_error("Unsupported component type in scene configuration: " + typeName);
        }

    private:
        static glm::vec3 ParseVec3(const rapidjson::Value &value) {
            return glm::vec3{value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat()};
        }

        void EmplaceMeshRenderer(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            std::shared_ptr<Model> model = nullptr;
            if (componentObject.HasMember("model")) {
                const std::string modelName = componentObject["model"].GetString();
                model = m_modelRepository.GetOrLoad(modelName, modelName);
            }

            auto *component = sceneRegistry.EmplaceComponent<MeshRendererComponent>(
                entityId,
                model,
                static_cast<id_t>(componentObject["materialId"].GetInt()));

            if (componentObject.HasMember("visible")) {
                component->visible = componentObject["visible"].GetBool();
            }
            if (componentObject.HasMember("renderLayer")) {
                component->renderLayer = componentObject["renderLayer"].GetUint();
            }
            if (componentObject.HasMember("castShadow")) {
                component->castShadow = componentObject["castShadow"].GetBool();
            }
            if (componentObject.HasMember("receiveShadow")) {
                component->receiveShadow = componentObject["receiveShadow"].GetBool();
            }
        }

        static void EmplaceLight(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            auto *component = sceneRegistry.EmplaceComponent<LightComponent>(entityId);
            if (componentObject.HasMember("color")) {
                component->color = ParseVec3(componentObject["color"]);
            }
            if (componentObject.HasMember("category")) {
                const std::string category = componentObject["category"].GetString();
                component->SetLightCategory(category == "Directional" ? LightCategory::DIRECTIONAL_LIGHT : LightCategory::POINT_LIGHT);
            }
            if (componentObject.HasMember("intensity")) {
                component->lightIntensity = componentObject["intensity"].GetFloat();
            }
        }

        static void EmplaceObjectMovement(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            auto *component = sceneRegistry.EmplaceComponent<ObjectMovementComponent>(entityId);
            if (componentObject.HasMember("moveSpeed")) {
                component->moveSpeed = componentObject["moveSpeed"].GetFloat();
            }
        }

        static void EmplaceCameraMovement(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            auto *component = sceneRegistry.EmplaceComponent<CameraMovementComponent>(entityId);
            if (componentObject.HasMember("focusMoveTime")) {
                component->focusMoveTime = componentObject["focusMoveTime"].GetFloat();
            }
            if (componentObject.HasMember("lookSpeed")) {
                component->lookSpeed = componentObject["lookSpeed"].GetFloat();
            }
            if (componentObject.HasMember("moveSpeed")) {
                component->moveSpeed = componentObject["moveSpeed"].GetFloat();
            }
        }

        static void EmplaceRigidBody(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            auto *component = sceneRegistry.EmplaceComponent<RigidBodyComponent>(entityId);
            if (componentObject.HasMember("isKinematic")) {
                component->isKinematic = componentObject["isKinematic"].GetBool();
            }
            if (componentObject.HasMember("omega")) {
                component->omega = ParseVec3(componentObject["omega"]);
            }
            if (componentObject.HasMember("velocity")) {
                component->velocity = ParseVec3(componentObject["velocity"]);
            }
            if (componentObject.HasMember("useGravity")) {
                component->useGravity = componentObject["useGravity"].GetBool();
            }
        }

        static void EmplaceUi(ECS::SceneRegistry &sceneRegistry, id_t entityId, const rapidjson::Value &componentObject) {
            UIComponent::ElementType elementType = UIComponent::ElementType::Canvas;
            if (componentObject.HasMember("uiType")) {
                const std::string typeString = componentObject["uiType"].GetString();
                if (typeString == "Panel") {
                    elementType = UIComponent::ElementType::Panel;
                } else if (typeString == "Image") {
                    elementType = UIComponent::ElementType::Image;
                } else if (typeString == "Text") {
                    elementType = UIComponent::ElementType::Text;
                } else if (typeString == "Button") {
                    elementType = UIComponent::ElementType::Button;
                }
            }

            sceneRegistry.EmplaceComponent<UIComponent>(entityId, elementType);
        }

        ModelRepository &m_modelRepository;
    };
}
