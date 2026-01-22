#pragma once

#include <glm/ext/matrix_clip_space.hpp>

#include "../Components/CameraComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../StructureInfos.h"

namespace FeatherVK {
    class CameraSystem {
    public:
        void Update(ECS::SceneRegistry &sceneRegistry, FrameInfo &frameInfo, const RendererInfo &rendererInfo) const {
            for (const auto entityId: sceneRegistry.View<CameraComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId)) {
                    continue;
                }

                CameraComponent *camera = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, camera) ||
                    !sceneRegistry.TryGetComponent(entityId, transform) ||
                    camera == nullptr ||
                    transform == nullptr) {
                    continue;
                }

                UpdateCameraMatrices(*camera, *transform, rendererInfo);
                frameInfo.globalUbo.viewMatrix = camera->viewMatrix;
                frameInfo.globalUbo.inverseViewMatrix = camera->inverseViewMatrix;
                frameInfo.globalUbo.projectionMatrix = camera->projectionMatrix;
#ifdef RAY_TRACING
                frameInfo.globalUbo.inverseProjectionMatrix = glm::inverse(camera->projectionMatrix);
#endif
            }
        }

    private:
        static void UpdateCameraMatrices(CameraComponent &camera, const TransformComponent &transform, const RendererInfo &rendererInfo) {
            SetViewYXZ(camera, transform.GetTranslation(), transform.GetRotation());
            SetPerspectiveProjection(
                camera,
                glm::radians(rendererInfo.fovY),
                rendererInfo.aspectRatio,
                rendererInfo.near,
                rendererInfo.far);
        }

        static void SetPerspectiveProjection(CameraComponent &camera, float fovY, float aspect, float near, float far) {
            const float tanHalfFovY = glm::tan(fovY / 2.0f);
            camera.projectionMatrix = glm::mat4{0.0f};
            camera.projectionMatrix[0][0] = 1.0f / (aspect * tanHalfFovY);
            camera.projectionMatrix[1][1] = 1.0f / tanHalfFovY;
            camera.projectionMatrix[2][2] = far / (far - near);
            camera.projectionMatrix[2][3] = 1.0f;
            camera.projectionMatrix[3][2] = -(far * near) / (far - near);
        }

        static void SetViewYXZ(CameraComponent &camera, glm::vec3 position, glm::vec3 rotation) {
            const float c3 = glm::cos(rotation.z);
            const float s3 = glm::sin(rotation.z);
            const float c2 = glm::cos(rotation.x);
            const float s2 = glm::sin(rotation.x);
            const float c1 = glm::cos(rotation.y);
            const float s1 = glm::sin(rotation.y);
            const glm::vec3 u{(c1 * c3 + s1 * s2 * s3), (c2 * s3), (c1 * s2 * s3 - c3 * s1)};
            const glm::vec3 v{(c3 * s1 * s2 - c1 * s3), (c2 * c3), (c1 * c3 * s2 + s1 * s3)};
            const glm::vec3 w{(c2 * s1), (-s2), (c1 * c2)};
            SetViewMatricesFromBasis(camera, position, u, v, w);
        }

        static void SetViewMatricesFromBasis(CameraComponent &camera,
                                             const glm::vec3 &position,
                                             const glm::vec3 &u,
                                             const glm::vec3 &v,
                                             const glm::vec3 &w) {
            camera.viewMatrix = glm::mat4{1.0f};
            camera.viewMatrix[0][0] = u.x;
            camera.viewMatrix[1][0] = u.y;
            camera.viewMatrix[2][0] = u.z;
            camera.viewMatrix[0][1] = v.x;
            camera.viewMatrix[1][1] = v.y;
            camera.viewMatrix[2][1] = v.z;
            camera.viewMatrix[0][2] = w.x;
            camera.viewMatrix[1][2] = w.y;
            camera.viewMatrix[2][2] = w.z;
            camera.viewMatrix[3][0] = -glm::dot(u, position);
            camera.viewMatrix[3][1] = -glm::dot(v, position);
            camera.viewMatrix[3][2] = -glm::dot(w, position);

            camera.inverseViewMatrix = glm::mat4{1.0f};
            camera.inverseViewMatrix[0][0] = u.x;
            camera.inverseViewMatrix[0][1] = u.y;
            camera.inverseViewMatrix[0][2] = u.z;
            camera.inverseViewMatrix[1][0] = v.x;
            camera.inverseViewMatrix[1][1] = v.y;
            camera.inverseViewMatrix[1][2] = v.z;
            camera.inverseViewMatrix[2][0] = w.x;
            camera.inverseViewMatrix[2][1] = w.y;
            camera.inverseViewMatrix[2][2] = w.z;
            camera.inverseViewMatrix[3][0] = position.x;
            camera.inverseViewMatrix[3][1] = position.y;
            camera.inverseViewMatrix[3][2] = position.z;
        }
    };
}
