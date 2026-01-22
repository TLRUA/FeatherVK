#pragma once

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

namespace FeatherVK {
    class InputState {
    public:
        InputState() = default;

        explicit InputState(GLFWwindow *window) {
            Attach(window);
        }

        ~InputState() {
            Detach();
        }

        void Attach(GLFWwindow *window) {
            if (window == nullptr) {
                throw std::runtime_error("InputState requires a valid GLFW window");
            }

            if (m_window == window) {
                SyncCursorPosition();
                return;
            }

            Detach();
            m_window = window;
            s_instances[m_window] = this;
            SyncCursorPosition();
            glfwSetCursorPosCallback(m_window, CursorPositionCallback);
        }

        void BeginFrame() {
            m_previousCursorPositionNormalized = m_cursorPositionNormalized;
        }

        GLFWwindow *GetWindow() const {
            return m_window;
        }

        bool IsKeyDown(int key) const {
            return m_window != nullptr && glfwGetKey(m_window, key) == GLFW_PRESS;
        }

        bool IsMouseButtonDown(int button) const {
            return m_window != nullptr && glfwGetMouseButton(m_window, button) == GLFW_PRESS;
        }

        glm::vec2 GetCursorPositionNormalized() const {
            return m_cursorPositionNormalized;
        }

        glm::vec2 GetCursorDeltaNormalized() const {
            return m_cursorPositionNormalized - m_previousCursorPositionNormalized;
        }

        bool TryGetCursorFramebufferPosition(float &cursorFramebufferX, float &cursorFramebufferY) const {
            if (m_window == nullptr || !m_hasCursorPosition) {
                return false;
            }

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                return false;
            }

            cursorFramebufferX = m_cursorPositionNormalized.x * static_cast<float>(framebufferWidth);
            cursorFramebufferY = m_cursorPositionNormalized.y * static_cast<float>(framebufferHeight);
            return true;
        }

    private:
        static void CursorPositionCallback(GLFWwindow *window, double xpos, double ypos) {
            const auto instanceIt = s_instances.find(window);
            if (instanceIt == s_instances.end() || instanceIt->second == nullptr) {
                return;
            }

            instanceIt->second->UpdateCursorPosition(xpos, ypos);
        }

        void UpdateCursorPosition(double xpos, double ypos) {
            if (m_window == nullptr) {
                return;
            }

            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                return;
            }

            m_cursorPositionNormalized.x = std::clamp(static_cast<float>(xpos) / static_cast<float>(windowWidth), 0.0f, 1.0f);
            m_cursorPositionNormalized.y = std::clamp(static_cast<float>(ypos) / static_cast<float>(windowHeight), 0.0f, 1.0f);
            m_hasCursorPosition = true;
        }

        void SyncCursorPosition() {
            if (m_window == nullptr) {
                return;
            }

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(m_window, &cursorX, &cursorY);
            UpdateCursorPosition(cursorX, cursorY);
            m_previousCursorPositionNormalized = m_cursorPositionNormalized;
        }

        void Detach() {
            if (m_window == nullptr) {
                return;
            }

            const auto instanceIt = s_instances.find(m_window);
            if (instanceIt != s_instances.end() && instanceIt->second == this) {
                s_instances.erase(instanceIt);
            }
            m_window = nullptr;
        }

        GLFWwindow *m_window = nullptr;
        glm::vec2 m_cursorPositionNormalized{0.0f};
        glm::vec2 m_previousCursorPositionNormalized{0.0f};
        bool m_hasCursorPosition = false;

        inline static std::unordered_map<GLFWwindow *, InputState *> s_instances{};
    };
}
