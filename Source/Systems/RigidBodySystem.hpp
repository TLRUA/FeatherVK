#pragma once

#include <cassert>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

#include "../Core/SimulationConstants.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Model.hpp"
#include "../Managers/TransformService.hpp"

namespace FeatherVK {
    class RigidBodySystem {
    public:
        explicit RigidBodySystem(TransformService &transformService) : m_transformService(transformService) {}

        void Initialize(ECS::SceneRegistry &sceneRegistry) {
            for (const auto entityId: sceneRegistry.View<RigidBodyComponent, TransformComponent>()) {
                RigidBodyComponent *rigidBody = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, rigidBody) ||
                    !sceneRegistry.TryGetComponent(entityId, transform) ||
                    rigidBody == nullptr ||
                    transform == nullptr ||
                    rigidBody->initialized) {
                    continue;
                }

                MeshRendererComponent *meshRenderer = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, meshRenderer) ||
                    meshRenderer == nullptr ||
                    meshRenderer->GetModelPtr() == nullptr) {
                    throw std::runtime_error("RigidBodyComponent needs a MeshRendererComponent");
                }

                InitializeRigidBody(*rigidBody, *transform, *meshRenderer);
            }
        }

        void FixedUpdate(ECS::SceneRegistry &sceneRegistry) const {
            for (const auto entityId: sceneRegistry.View<RigidBodyComponent, TransformComponent>()) {
                if (!sceneRegistry.IsEntityActive(entityId)) {
                    continue;
                }

                RigidBodyComponent *rigidBody = nullptr;
                TransformComponent *transform = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, rigidBody) ||
                    !sceneRegistry.TryGetComponent(entityId, transform) ||
                    rigidBody == nullptr ||
                    transform == nullptr ||
                    !rigidBody->initialized) {
                    continue;
                }

                const glm::vec3 massCenter = GetMassCenter(*rigidBody, *transform);
                if (rigidBody->useGravity && !rigidBody->isKinematic && rigidBody->totalMass > RigidBodyComponent::EPSILON) {
                    rigidBody->pendingImpulses.emplace_back(
                        massCenter,
                        FIXED_UPDATE_INTERVAL * rigidBody->totalMass * RigidBodyComponent::GRAVITY);
                }

                const glm::mat3 worldInverseInertia = ComputeWorldInverseInertia(*rigidBody, *transform);
                for (const auto &impulse: rigidBody->pendingImpulses) {
                    const glm::vec3 r = std::get<0>(impulse) - massCenter;
                    rigidBody->velocity += std::get<1>(impulse) * rigidBody->inverseMass;
                    rigidBody->omega += worldInverseInertia * glm::cross(r, std::get<1>(impulse));
                }
                rigidBody->pendingImpulses.clear();

                if (glm::length(rigidBody->velocity) < RigidBodyComponent::EPSILON) {
                    rigidBody->velocity = glm::vec3(0.0f);
                }
                if (glm::length(rigidBody->omega) < RigidBodyComponent::EPSILON) {
                    rigidBody->omega = glm::vec3(0.0f);
                }

                m_transformService.Translate(sceneRegistry, entityId, rigidBody->velocity * FIXED_UPDATE_INTERVAL);
                m_transformService.Rotate(sceneRegistry, entityId, rigidBody->omega * FIXED_UPDATE_INTERVAL);
            }
        }

        void LateFixedUpdate(ECS::SceneRegistry &sceneRegistry) const {
            for (const auto entityId: sceneRegistry.View<RigidBodyComponent>()) {
                RigidBodyComponent *rigidBody = nullptr;
                if (!sceneRegistry.TryGetComponent(entityId, rigidBody) || rigidBody == nullptr) {
                    continue;
                }

                rigidBody->collisionMap.clear();
            }
        }

    private:
        static glm::vec3 GetMassCenter(const RigidBodyComponent &rigidBody, const TransformComponent &transform) {
            return transform.mat4() * glm::vec4(rigidBody.nativeMassCenter, 1.0f);
        }

        static glm::mat3 ComputeWorldInverseInertia(const RigidBodyComponent &rigidBody, const TransformComponent &transform) {
            const auto rotationMatrix = transform.GetRotationMatrix();
            const auto worldInertia = rotationMatrix * rigidBody.inertiaTensor * glm::transpose(rotationMatrix);
            return glm::inverse(worldInertia);
        }

        void InitializeRigidBody(RigidBodyComponent &rigidBody,
                                 TransformComponent &transform,
                                 MeshRendererComponent &meshRenderer) {
            auto model = meshRenderer.GetModelPtr();
            if (model == nullptr) {
                throw std::runtime_error("RigidBodyComponent needs a valid model");
            }

            glm::mat3 inertiaTensor{};
            glm::vec3 massCenter{};
            float totalMass = 0.0f;
            MeshComputeInertia(transform, *model, 1.0f, inertiaTensor, massCenter, totalMass);

            rigidBody.inertiaTensor = inertiaTensor;
            rigidBody.inverseInertiaTensor =
                totalMass > RigidBodyComponent::EPSILON ? glm::inverse(inertiaTensor) : glm::mat3{1.0f};
            rigidBody.nativeMassCenter = massCenter;
            rigidBody.totalMass = totalMass > RigidBodyComponent::EPSILON ? totalMass : 1.0f;
            rigidBody.inverseMass =
                rigidBody.totalMass > RigidBodyComponent::EPSILON ? 1.0f / rigidBody.totalMass : 0.0f;

            auto &vertices = model->GetVertices();
            if (vertices.empty()) {
                throw std::runtime_error("RigidBodyComponent needs a non-empty model");
            }

            glm::vec3 minBounds{0.0f};
            glm::vec3 maxBounds{0.0f};
            float maxRadius = 0.0f;

            const bool shouldCenterModel = m_centeredModels.insert(model.get()).second;
            for (size_t index = 0; index < vertices.size(); ++index) {
                auto &vertex = vertices[index];
                if (shouldCenterModel) {
                    vertex.position -= massCenter;
                }

                if (index == 0) {
                    minBounds = vertex.position;
                    maxBounds = vertex.position;
                } else {
                    minBounds = glm::min(minBounds, vertex.position);
                    maxBounds = glm::max(maxBounds, vertex.position);
                }
                maxRadius = glm::max(maxRadius, glm::length(vertex.position));
            }

            rigidBody.localAabb.min = minBounds;
            rigidBody.localAabb.max = maxBounds;

            if (shouldCenterModel) {
                model->RefreshVertexBuffer(vertices);
                model->SetMaxRadius(maxRadius);
            }

            rigidBody.initialized = true;
        }

        static void MeshComputeInertia(const TransformComponent &transform,
                                       const Model &model,
                                       float density,
                                       glm::mat3 &inertiaTensor,
                                       glm::vec3 &massCenter,
                                       float &totalMass) {
            const auto &indices = model.GetIndices();
            const auto &vertices = model.GetVertices();
            assert(indices.size() % 3 == 0 && "Indices size must be a multiple of 3");

            float mass = 0.0f;
            glm::vec3 localMassCenter{0.0f};
            float Ia = 0.0f;
            float Ib = 0.0f;
            float Ic = 0.0f;
            float Iap = 0.0f;
            float Ibp = 0.0f;
            float Icp = 0.0f;

            for (size_t i = 0; i < indices.size(); i += 3) {
                glm::vec3 triangleVertices[3];
                for (int j = 0; j < 3; ++j) {
                    triangleVertices[j] = transform.GetScale() * vertices[indices[i + j]].position;
                }

                const float detJ = glm::dot(triangleVertices[0], glm::cross(triangleVertices[1], triangleVertices[2]));
                const float tetrahedronVolume = detJ / 6.0f;
                const float tetrahedronMass = density * tetrahedronVolume;
                const glm::vec3 tetrahedronMassCenter =
                    (triangleVertices[0] + triangleVertices[1] + triangleVertices[2]) / 4.0f;

                Ia += detJ * (ComputeInertiaMoment(triangleVertices, 1) + ComputeInertiaMoment(triangleVertices, 2));
                Ib += detJ * (ComputeInertiaMoment(triangleVertices, 0) + ComputeInertiaMoment(triangleVertices, 2));
                Ic += detJ * (ComputeInertiaMoment(triangleVertices, 0) + ComputeInertiaMoment(triangleVertices, 1));
                Iap += detJ * ComputeInertiaProduct(triangleVertices, 1, 2);
                Ibp += detJ * ComputeInertiaProduct(triangleVertices, 0, 1);
                Icp += detJ * ComputeInertiaProduct(triangleVertices, 0, 2);

                localMassCenter += tetrahedronMassCenter * tetrahedronMass;
                mass += tetrahedronMass;
            }

            if (std::abs(mass) < RigidBodyComponent::EPSILON) {
                mass = 1.0f;
            }

            localMassCenter /= mass;
            Ia = density * Ia / 60.0f - mass * (std::pow(localMassCenter[1], 2) + std::pow(localMassCenter[2], 2));
            Ib = density * Ib / 60.0f - mass * (std::pow(localMassCenter[0], 2) + std::pow(localMassCenter[2], 2));
            Ic = density * Ic / 60.0f - mass * (std::pow(localMassCenter[0], 2) + std::pow(localMassCenter[1], 2));
            Iap = density * Iap / 120.0f - mass * localMassCenter[1] * localMassCenter[2];
            Ibp = density * Ibp / 120.0f - mass * localMassCenter[0] * localMassCenter[1];
            Icp = density * Icp / 120.0f - mass * localMassCenter[0] * localMassCenter[2];

            inertiaTensor = glm::mat3(
                Ia, -Ibp, -Icp,
                -Ibp, Ib, -Iap,
                -Icp, -Iap, Ic);
            massCenter = localMassCenter;
            totalMass = mass;
        }

        static float ComputeInertiaMoment(glm::vec3 p[3], int i) {
            return std::pow(p[0][i], 2) + p[1][i] * p[2][i] +
                   std::pow(p[1][i], 2) + p[0][i] * p[2][i] +
                   std::pow(p[2][i], 2) + p[0][i] * p[1][i];
        }

        static float ComputeInertiaProduct(glm::vec3 p[3], int i, int j) {
            return 2 * p[0][i] * p[0][j] + p[1][i] * p[2][j] + p[2][i] * p[1][j] +
                   2 * p[1][i] * p[1][j] + p[0][i] * p[2][j] + p[2][i] * p[0][j] +
                   2 * p[2][i] * p[2][j] + p[0][i] * p[1][j] + p[1][i] * p[0][j];
        }

        TransformService &m_transformService;
        std::unordered_set<Model *> m_centeredModels{};
    };
}
