#pragma once

#include <chrono>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <string>

#include "Core/Events.hpp"
#include "Core/Logger.hpp"
#include "Descriptor.h"
#include "Device.hpp"
#include "GUI.hpp"
#include "Image.h"
#include "RenderCore/FrameData.hpp"
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

            auto &window = m_resourceManager->GetWindow();
            auto &renderer = m_resourceManager->GetRenderer();
            auto &materials = m_resourceManager->GetMaterials();
            auto &device = m_resourceManager->GetDevice();
            auto &sceneRegistry = m_resourceManager->GetSceneRegistry();

            while (!window.shouldClose()) {
                m_resourceManager->GetInputState().BeginFrame();
                glfwPollEvents();

                const VkExtent2D windowExtent = window.getCurrentExtent();
                GUI::UpdateLayout(ImVec2(static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height)));
                m_resourceManager->SyncSceneViewportLayout(GUI::GetScenePanelRect(), GUI::GetSceneContentRect());

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
                    const auto renderView = renderer.GetRenderView();
                    FrameInfo frameInfo{
                            frameIndex,
                            frameTime,
                            totalTime,
                            commandBuffer,
                            &renderer.getCurrentRHICommandList(),
                            &sceneRegistry,
                            materials,
                            m_ubo,
                            windowExtent,
                            renderView.renderExtent,
                            renderView.panelRect,
                            renderView.viewportRect,
                            m_resourceManager.get(),
                            &m_resourceManager->GetEntityCommandService(),
                            m_resourceManager->GetEditorSelectionService().GetSelectedId(),
                            false,
                            nullptr};
                    frameInfo.renderInvalidationSink = m_resourceManager.get();

                    GUI::BeginFrame(ImVec2(static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height)));
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

        std::shared_ptr<ResourceManager> m_resourceManager;
        std::unique_ptr<RenderManager> m_renderManager;
        std::unique_ptr<LogicManager> m_logicManager;

        void UpdateComponents(FrameInfo &frameInfo) {
            m_logicManager->UpdateComponents(frameInfo);
        }

        void UpdateRendering(FrameInfo &frameInfo) {
            auto &renderer = m_resourceManager->GetRenderer();
#ifdef RAY_TRACING
            auto &gameObjectDescBuffer = m_resourceManager->GetEntityDescBuffer();
            auto &gameObjectDescs = m_resourceManager->GetEntityDescs();
            frameInfo.pEntityDescBuffer = gameObjectDescBuffer;
            frameInfo.pEntityDescs = gameObjectDescs;
#endif
            m_renderManager->UpdateRendering(renderer, frameInfo);
            if (m_resourceManager->ConsumeSceneSaveRequest()) {
                m_resourceManager->SaveScene();
            }
        }
    };
}








