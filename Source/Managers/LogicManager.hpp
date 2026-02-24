#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "../Core/SimulationConstants.hpp"
#include "../Systems/CameraMovementSystem.hpp"
#include "../Systems/CameraSystem.hpp"
#include "../Systems/ObjectMovementSystem.hpp"
#include "../Systems/RigidBodySystem.hpp"
#include "../Systems/TransformHierarchySystem.hpp"
#include "TransformService.hpp"
#include "EditorInteractionSystem.hpp"
#include "ResourceManager.hpp"

namespace FeatherVK {
    class LogicManager {
    public:
        explicit LogicManager(std::shared_ptr<ResourceManager> resourceManager) {
            m_resourceManager = std::move(resourceManager);
            m_editorInteractionSystem = std::make_unique<EditorInteractionSystem>(
                m_resourceManager->GetWindow(),
                m_resourceManager->GetRenderer(),
                m_resourceManager->GetInputState(),
                m_resourceManager->GetEditorSelectionService(),
                m_resourceManager->GetTransformService());
            m_objectMovementSystem = std::make_unique<ObjectMovementSystem>(
                m_resourceManager->GetInputState(),
                m_resourceManager->GetTransformService());
            m_cameraMovementSystem = std::make_unique<CameraMovementSystem>(
                m_resourceManager->GetInputState(),
                m_resourceManager->GetTransformService());
            m_cameraSystem = std::make_unique<CameraSystem>();
            m_rigidBodySystem = std::make_unique<RigidBodySystem>(m_resourceManager->GetTransformService());
            m_transformHierarchySystem = std::make_unique<TransformHierarchySystem>();
        }

        ~LogicManager() = default;

        LogicManager(const LogicManager &) = delete;
        LogicManager &operator=(const LogicManager &) = delete;

        void UpdateComponents(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            UpdateUbo(frameInfo);

            auto &renderer = m_resourceManager->GetRenderer();
            auto &sceneRegistry = *frameInfo.sceneRegistry;
            RendererInfo rendererInfo{renderer.getAspectRatio(), renderer.FOV_Y, renderer.NEAR_CLIP, renderer.FAR_CLIP};

            m_transformHierarchySystem->Update(sceneRegistry, m_resourceManager->GetHierarchyService(), m_resourceManager->GetTransformService());
            m_rigidBodySystem->Initialize(sceneRegistry);
            m_objectMovementSystem->Update(sceneRegistry);
            m_cameraMovementSystem->Update(sceneRegistry, frameInfo, rendererInfo);
            m_transformHierarchySystem->Update(sceneRegistry, m_resourceManager->GetHierarchyService(), m_resourceManager->GetTransformService());
            m_cameraSystem->Update(sceneRegistry, frameInfo, rendererInfo);
            m_editorInteractionSystem->Update(frameInfo);
            FixedUpdateComponents(frameInfo);
            m_transformHierarchySystem->Update(sceneRegistry, m_resourceManager->GetHierarchyService(), m_resourceManager->GetTransformService());
        }

        void FixedUpdateComponents(FrameInfo &frameInfo) {
            if (frameInfo.sceneRegistry == nullptr) {
                return;
            }

            auto &sceneRegistry = *frameInfo.sceneRegistry;
            static float reservedFrameTime = 0;
            float frameTime = frameInfo.frameTime + reservedFrameTime;

            while (frameTime >= FIXED_UPDATE_INTERVAL) {
                frameTime -= FIXED_UPDATE_INTERVAL;

                m_rigidBodySystem->FixedUpdate(sceneRegistry);
                m_rigidBodySystem->LateFixedUpdate(sceneRegistry);
                m_transformHierarchySystem->Update(sceneRegistry, m_resourceManager->GetHierarchyService(), m_resourceManager->GetTransformService());
            }
            reservedFrameTime = frameTime;
        }

        void UpdateUbo(FrameInfo &frameInfo) {
            frameInfo.globalUbo.curTime = frameInfo.totalTime;
        }

    private:
        std::shared_ptr<ResourceManager> m_resourceManager;
        std::unique_ptr<EditorInteractionSystem> m_editorInteractionSystem;
        std::unique_ptr<ObjectMovementSystem> m_objectMovementSystem;
        std::unique_ptr<CameraMovementSystem> m_cameraMovementSystem;
        std::unique_ptr<CameraSystem> m_cameraSystem;
        std::unique_ptr<RigidBodySystem> m_rigidBodySystem;
        std::unique_ptr<TransformHierarchySystem> m_transformHierarchySystem;
    };
}
