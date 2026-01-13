#pragma once

#include "../TransformComponent.hpp"
#include "InputControllerComponent.hpp"

namespace FeatherVK {
    class ObjectMovementComponent : public InputControllerComponent {
    public:
        ObjectMovementComponent(GLFWwindow *window) : InputControllerComponent(window) {
            name = "ObjectMovementComponent";
        }

        void Update(const ComponentUpdateInfo &updateInfo) override {
            TransformComponent *moveTransform = updateInfo.transform;
            if (moveTransform == nullptr) {
                return;
            }

            if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) {
                moveTransform->Translate({-m_gameObjectMovementSpeed, 0, 0});
            }
            if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) {
                moveTransform->Translate({m_gameObjectMovementSpeed, 0, 0});
            }
            if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) {
                moveTransform->Translate({0, 0, m_gameObjectMovementSpeed});
            }
            if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) {
                moveTransform->Translate({0, 0, -m_gameObjectMovementSpeed});
            }
        }

    private:
        float m_gameObjectMovementSpeed{0.003f};
    };
}

