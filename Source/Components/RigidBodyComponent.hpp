#pragma once

#include <cassert>
#include <cmath>
#include <cfloat>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../ECS/SceneRegistry.hpp"
#include "MeshRendererComponent.hpp"

namespace Kaamoo {
    class RigidBodyComponent : public Component {
    public:
        inline static const float EPSILON = 0.0001f;
        inline static const glm::vec3 GRAVITY = glm::vec3(0, 0.98f, 0);

        explicit RigidBodyComponent(const rapidjson::Value &object) {
            if (object.HasMember("isKinematic")) {
                m_isKinematic = object["isKinematic"].GetBool();
            }
            if (object.HasMember("omega")) {
                auto omegaArray = object["omega"].GetArray();
                m_omega = glm::vec3(omegaArray[0].GetFloat(), omegaArray[1].GetFloat(), omegaArray[2].GetFloat());
            }
            if (object.HasMember("velocity")) {
                auto velocityArray = object["velocity"].GetArray();
                m_velocity = glm::vec3(velocityArray[0].GetFloat(), velocityArray[1].GetFloat(), velocityArray[2].GetFloat());
            }
            if (object.HasMember("useGravity")) {
                m_useGravity = object["useGravity"].GetBool();
            }
            name = "RigidBodyComponent";
        }

        void Loaded(id_t entityId, ECS::SceneRegistry &sceneRegistry) override {
            m_entityId = entityId;

            if (!sceneRegistry.TryGetComponent(entityId, m_meshRendererComponent) ||
                m_meshRendererComponent == nullptr ||
                m_meshRendererComponent->GetModelPtr() == nullptr) {
                throw std::runtime_error("RigidBodyComponent needs a MeshRendererComponent");
            }

            if (!sceneRegistry.TryGetComponent(entityId, m_transformComponent) || m_transformComponent == nullptr) {
                throw std::runtime_error("RigidBodyComponent needs a TransformComponent");
            }

            glm::mat3 I0{};
            glm::vec3 massCenter{};
            float totalMass = 0.0f;
            MeshComputeInertia(m_transformComponent, m_meshRendererComponent->GetModelPtr().get(), 1.0f, &I0, &massCenter, &totalMass);

            m_I0 = I0;
            m_invI0 = glm::inverse(I0);
            m_nativeMassCenter = massCenter;
            m_totalMass = totalMass;
            m_invMass = m_totalMass > EPSILON ? 1.0f / m_totalMass : 0.0f;

            auto &vertices = m_meshRendererComponent->GetModelPtr()->GetVertices();
            glm::vec3 min = vertices[0].position;
            glm::vec3 max = vertices[0].position;
            float maxRadius = 0.0f;
            for (auto &vertex: vertices) {
                vertex.position -= massCenter;
                min = glm::min(min, vertex.position);
                max = glm::max(max, vertex.position);
                maxRadius = glm::max(maxRadius, glm::length(vertex.position));
            }

            m_aabb.min = min;
            m_aabb.max = max;

            m_meshRendererComponent->GetModelPtr()->RefreshVertexBuffer(vertices);
            m_meshRendererComponent->GetModelPtr()->SetMaxRadius(maxRadius);
        }

        void FixedUpdate(const ComponentUpdateInfo &updateInfo) override {
            TransformComponent *transformComponent = updateInfo.transform;
            if (transformComponent == nullptr) {
                transformComponent = m_transformComponent;
            }
            if (transformComponent == nullptr) {
                return;
            }

            const glm::vec3 massCenter = GetMassCenter(transformComponent);
            if (m_useGravity && !m_isKinematic && m_totalMass > EPSILON) {
                AddJ(massCenter, FIXED_UPDATE_INTERVAL * m_totalMass * GRAVITY);
            }

            for (auto &pair: m_momentum) {
                const glm::vec3 r = std::get<0>(pair) - massCenter;
                m_velocity += std::get<1>(pair) * m_invMass;
                m_omega += m_invI0 * glm::cross(r, std::get<1>(pair));
            }
            m_momentum.clear();

            if (glm::length(m_velocity) < EPSILON) {
                m_velocity = glm::vec3(0);
            }
            if (glm::length(m_omega) < EPSILON) {
                m_omega = glm::vec3(0);
            }

            auto rotationMatrix = transformComponent->GetRotationMatrix();
            transformComponent->Translate(m_velocity * FIXED_UPDATE_INTERVAL);
            transformComponent->Rotate(m_omega * FIXED_UPDATE_INTERVAL);
            m_I0 = rotationMatrix * m_I0 * glm::transpose(rotationMatrix);
        }

        void LateFixedUpdate(const ComponentUpdateInfo &) override {
            m_collisionMap.clear();
        }

        struct AABB {
            glm::vec3 min;
            glm::vec3 max;
        };

        AABB GetAABB(TransformComponent *transformComponent) const {
            auto mat4 = transformComponent->mat4();
            glm::vec3 translation = mat4[3];
            AABB aabb{};
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    const float a = mat4[j][i] * m_aabb.min[j];
                    const float b = mat4[j][i] * m_aabb.max[j];
                    aabb.min[i] += a < b ? a : b;
                    aabb.max[i] += a < b ? b : a;
                }
            }
            aabb.max += translation;
            aabb.min += translation;
            return aabb;
        }

        glm::vec3 GetMassCenter(TransformComponent *transformComponent) const {
            return transformComponent->mat4() * glm::vec4(m_nativeMassCenter, 1);
        }

        void SetVelocity(glm::vec3 velocity) {
            m_velocity = velocity;
        }

        void SetOmega(glm::vec3 omega) {
            m_omega = omega;
        }

        glm::vec3 GetVelocity() const {
            return m_velocity;
        }

        glm::vec3 GetOmega() const {
            return m_omega;
        }

        float GetInvMass() const {
            return m_invMass;
        }

        auto GetJ() const {
            return m_momentum;
        }

        void AddJ(glm::vec3 position, glm::vec3 j) {
            m_momentum.emplace_back(position, j);
        }

        const std::unordered_map<id_t, int> &GetCollisionMap() const {
            return m_collisionMap;
        }

        glm::mat3 GetInvI0(TransformComponent *transformComponent) const {
            auto rotateMatrix = transformComponent->GetRotationMatrix();
            auto I0 = rotateMatrix * m_I0 * glm::transpose(rotateMatrix);
            return glm::inverse(I0);
        }

        bool IsKinematic() const {
            return m_isKinematic;
        }

#ifdef RAY_TRACING
        void SetUI(std::vector<GameObjectDesc> *, FrameInfo &) override {
#else
        void SetUI(Material::Map *, FrameInfo &) override {
#endif
            ImGui::Text("Velocity:");
            ImGui::SameLine(90);
            ImGui::InputFloat3("##Velocity", &m_velocity.x);

            ImGui::Text("Omega:");
            ImGui::SameLine(90);
            ImGui::InputFloat3("##Omega", &m_omega.x);

            ImGui::Text("Mass:");
            ImGui::SameLine(90);
            ImGui::InputFloat("##Mass", &m_totalMass);
        }

    private:
        static void MeshComputeInertia(TransformComponent *transformComponent, Model *model, float density, glm::mat3 *I0, glm::vec3 *massCenter, float *totalMass) {
            auto indices = model->GetIndices();
            auto vertices = model->GetVertices();
            assert(indices.size() % 3 == 0 && "Indices size must be a multiple of 3");

            float mass = 0;
            glm::vec3 localMassCenter{0};
            float Ia = 0, Ib = 0, Ic = 0, Iap = 0, Ibp = 0, Icp = 0;
            for (int i = 0; i < indices.size(); i += 3) {
                glm::vec3 triangleVertices[3];
                for (int j = 0; j < 3; j++) {
                    triangleVertices[j] = transformComponent->GetScale() * vertices[indices[i + j]].position;
                }

                const float detJ = glm::dot(triangleVertices[0], glm::cross(triangleVertices[1], triangleVertices[2]));
                const float tetVolume = detJ / 6.0f;
                const float tetMass = density * tetVolume;
                glm::vec3 tetMassCenter = (triangleVertices[0] + triangleVertices[1] + triangleVertices[2]) / 4.0f;

                Ia += detJ * (ComputeInertiaMoment(triangleVertices, 1) + ComputeInertiaMoment(triangleVertices, 2));
                Ib += detJ * (ComputeInertiaMoment(triangleVertices, 0) + ComputeInertiaMoment(triangleVertices, 2));
                Ic += detJ * (ComputeInertiaMoment(triangleVertices, 0) + ComputeInertiaMoment(triangleVertices, 1));
                Iap += detJ * (ComputeInertiaProduct(triangleVertices, 1, 2));
                Ibp += detJ * (ComputeInertiaProduct(triangleVertices, 0, 1));
                Icp += detJ * (ComputeInertiaProduct(triangleVertices, 0, 2));

                tetMassCenter *= tetMass;
                localMassCenter += tetMassCenter;
                mass += tetMass;
            }

            if (std::abs(mass) < EPSILON) {
                mass = 1.0f;
            }

            localMassCenter /= mass;
            Ia = density * Ia / 60.0f - mass * (std::pow(localMassCenter[1], 2) + std::pow(localMassCenter[2], 2));
            Ib = density * Ib / 60.0f - mass * (std::pow(localMassCenter[0], 2) + std::pow(localMassCenter[2], 2));
            Ic = density * Ic / 60.0f - mass * (std::pow(localMassCenter[0], 2) + std::pow(localMassCenter[1], 2));
            Iap = density * Iap / 120.0f - mass * localMassCenter[1] * localMassCenter[2];
            Ibp = density * Ibp / 120.0f - mass * localMassCenter[0] * localMassCenter[1];
            Icp = density * Icp / 120.0f - mass * localMassCenter[0] * localMassCenter[2];

            *I0 = glm::mat3(
                    Ia, -Ibp, -Icp,
                    -Ibp, Ib, -Iap,
                    -Icp, -Iap, Ic
            );
            *massCenter = localMassCenter;
            *totalMass = mass;
        }

        static float ComputeInertiaMoment(glm::vec3 p[3], int i) {
            return std::pow(p[0][i], 2) + p[1][i] * p[2][i]
                   + std::pow(p[1][i], 2) + p[0][i] * p[2][i]
                   + std::pow(p[2][i], 2) + p[0][i] * p[1][i];
        }

        static float ComputeInertiaProduct(glm::vec3 p[3], int i, int j) {
            return 2 * p[0][i] * p[0][j] + p[1][i] * p[2][j] + p[2][i] * p[1][j]
                   + 2 * p[1][i] * p[1][j] + p[0][i] * p[2][j] + p[2][i] * p[0][j]
                   + 2 * p[2][i] * p[2][j] + p[0][i] * p[1][j] + p[1][i] * p[0][j];
        }

    private:
        id_t m_entityId = std::numeric_limits<id_t>::max();
        TransformComponent *m_transformComponent = nullptr;
        MeshRendererComponent *m_meshRendererComponent = nullptr;
        std::unordered_map<id_t, int> m_collisionMap{};
        AABB m_aabb{};

        glm::vec3 m_velocity{0, 0, 0};
        glm::vec3 m_omega{0, 0, 0};
        glm::vec3 m_nativeMassCenter{0};
        std::vector<std::tuple<glm::vec3, glm::vec3>> m_momentum;
        glm::mat3 m_I0{1.0f};
        glm::mat3 m_invI0{1.0f};
        float m_totalMass = 1.0f;
        float m_invMass = 1.0f;
        float m_e = 0.7f;
        float m_u = 0.5f;

        bool m_isKinematic = false;
        bool m_useGravity = false;
    };
}
