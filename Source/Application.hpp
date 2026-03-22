#pragma once

#include <chrono>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <string>

#include "ComponentFactory.hpp"
#include "Core/Events.hpp"
#include "Core/Logger.hpp"
#include "Descriptor.h"
#include "Device.hpp"
#include "GUI.hpp"
#include "Image.h"
#include "Managers/LogicManager.hpp"
#include "Managers/RenderManager.hpp"
#include "Managers/ResourceManager.hpp"
#include "ECS/SceneRegistry.hpp"
#include "Material.hpp"
#include "Model.hpp"
#include "MyWindow.hpp"
#include "Pipeline.hpp"
#include "Renderer.h"
#include "Sampler.h"
#include "ShaderBuilder.h"
#include "Utils/JsonUtils.hpp"

namespace FeatherVK {
    class Application {
    public:
        Application() {
            m_resourceManager = std::make_shared<ResourceManager>();
            m_renderManager = std::make_unique<RenderManager>(m_resourceManager);
            m_logicManager = std::make_unique<LogicManager>(m_resourceManager);
        }

        ~Application() {
            GUI::Destroy();
        }

        void run() {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float totalTime = 0.0f;
            Awake();

            auto &window = m_resourceManager->GetWindow();
            auto &renderer = m_resourceManager->GetRenderer();
            auto &materials = m_resourceManager->GetMaterials();
            auto &device = m_resourceManager->GetDevice();
            auto &sceneRegistry = m_resourceManager->GetSceneRegistry();

            while (!window.shouldClose()) {
                glfwPollEvents();

                Event event{};
                while (EventQueue::Poll(event)) {
                    if (event.type == EventType::WindowResized) {
                        Logger::Info("Window resized to " + std::to_string(event.width) + "x" + std::to_string(event.height));
                    }
                }

                auto newTime = std::chrono::high_resolution_clock::now();
                float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
                totalTime += frameTime;
                currentTime = newTime;

                if (auto commandBuffer = renderer.beginFrame()) {
                    int frameIndex = renderer.getFrameIndex();
                    FrameInfo frameInfo{
                            frameIndex,
                            frameTime,
                            totalTime,
                            commandBuffer,
                            m_legacyGameObjects,
                            &sceneRegistry,
                            materials,
                            m_ubo,
                            window.getCurrentExtent(),
                            GUI::GetSelectedId(),
                            GUI::GetSelectedId(),
                            false};

                    GUI::BeginFrame(ImVec2(frameInfo.extent.width, frameInfo.extent.height));
                    UpdateComponents(frameInfo);
                    UpdateRendering(frameInfo);
                }
            }

            vkDeviceWaitIdle(device.device());
        }

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

    private:
        GlobalUbo m_ubo{};
        GameObject::Map m_legacyGameObjects{};

        std::shared_ptr<ResourceManager> m_resourceManager;
        std::unique_ptr<RenderManager> m_renderManager;
        std::unique_ptr<LogicManager> m_logicManager;

        void Awake() {
            auto &sceneRegistry = m_resourceManager->GetSceneRegistry();

            ComponentAwakeInfo awakeInfo{};
            awakeInfo.sceneRegistry = &sceneRegistry;

            for (const auto entityId: sceneRegistry.GetEntityOrder()) {
                TransformComponent *transform = nullptr;
                sceneRegistry.TryGetComponent(entityId, transform);

                awakeInfo.entityId = entityId;
                awakeInfo.transform = transform;
                awakeInfo.gameObject = nullptr;

                for (auto *component: sceneRegistry.GetComponents(entityId)) {
                    if (component != nullptr) {
                        component->Awake(awakeInfo);
                    }
                }
            }
        }

        void UpdateComponents(FrameInfo &frameInfo) {
            m_logicManager->UpdateComponents(frameInfo);
        }

        void UpdateRendering(FrameInfo &frameInfo) {
            auto &renderer = m_resourceManager->GetRenderer();
            auto &hierarchyTree = m_resourceManager->GetHierarchyTree();
#ifdef RAY_TRACING
            auto &gameObjectDescBuffer = m_resourceManager->GetGameObjectDescBuffer();
            auto &gameObjectDescs = m_resourceManager->GetGameObjectDescs();
            frameInfo.pGameObjectDescBuffer = gameObjectDescBuffer;
            frameInfo.pGameObjectDescs = gameObjectDescs;
#endif
            m_renderManager->UpdateRendering(renderer, frameInfo, hierarchyTree);
        }
    };
}








