#ifndef TRANSFORM_COMPONENT_INCLUDED
#define TRANSFORM_COMPONENT_INCLUDED

#include <limits>

#include <glm/detail/type_mat3x3.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>

#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class TransformComponent {
    public:
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        glm::vec3 getForwardDir() const {
            const float yaw = GetRotation().y;
            return glm::vec3{glm::sin(yaw), 0, glm::cos(yaw)};
        }

        void Translate(glm::vec3 t) {
            translation += t;
            MarkLocalDirty();
        }

        void Rotate(glm::vec3 r, glm::vec3 rotateCenter = glm::vec3(0.f)) {
            rotation += r;
            MarkLocalDirty();
        }

        void SetParentEntityId(id_t id) {
            if (parentEntityId == id) {
                return;
            }
            parentEntityId = id;
            MarkHierarchyDirty();
        }

        id_t GetParentEntityId() const {
            return parentEntityId;
        }

        bool HasParent() const {
            return parentEntityId != InvalidEntityId;
        }

        void ClearParentEntityId() {
            SetParentEntityId(InvalidEntityId);
        }

        glm::mat3 normalMatrix() const {
            return worldNormalMatrix;
        }

        glm::mat4 mat4() const {
            return worldMatrix;
        }

        void SetTranslation(glm::vec3 t) {
            translation = t;
            MarkLocalDirty();
        }

        glm::vec3 GetTranslation() const {
            return worldTranslation;
        }

        glm::vec3 GetRelativeTranslation() const {
            return translation;
        }

        void SetScale(glm::vec3 s) {
            scale = s;
            MarkLocalDirty();
        }

        glm::vec3 GetScale() const {
            return worldScale;
        }

        glm::vec3 GetRelativeScale() const {
            return scale;
        }

        void SetRotation(glm::vec3 r) {
            rotation = r;
            MarkLocalDirty();
        }

        glm::vec3 GetRotation() const {
            return worldRotation;
        }

        glm::vec3 GetRelativeRotation() const {
            return rotation;
        }

        glm::mat3 GetRotationMatrix() const {
            auto rotationMatrix = glm::mat4(1.0f);
            rotationMatrix = glm::rotate(rotationMatrix, worldRotation.y, {0, 1, 0});
            rotationMatrix = glm::rotate(rotationMatrix, worldRotation.x, {1, 0, 0});
            rotationMatrix = glm::rotate(rotationMatrix, worldRotation.z, {0, 0, 1});
            return glm::mat3(rotationMatrix);
        }

        void SetWorldTransform(const glm::vec3 &newWorldTranslation,
                               const glm::vec3 &newWorldScale,
                               const glm::vec3 &newWorldRotation) {
            worldTranslation = newWorldTranslation;
            worldScale = newWorldScale;
            worldRotation = newWorldRotation;
            RebuildWorldMatrices();
            localDirty = false;
            hierarchyDirty = false;
            worldDirty = false;
        }

        bool IsDirty() const {
            return localDirty || hierarchyDirty || worldDirty;
        }

        bool HasValidWorldTransform() const {
            return !worldDirty;
        }

    private:
        void MarkLocalDirty() {
            localDirty = true;
            worldDirty = true;
        }

        void MarkHierarchyDirty() {
            hierarchyDirty = true;
            worldDirty = true;
        }

        void RebuildWorldMatrices() {
            auto transform = glm::translate(glm::mat4{1.f}, worldTranslation);
            transform = glm::rotate(transform, worldRotation.y, {0, 1, 0});
            transform = glm::rotate(transform, worldRotation.x, {1, 0, 0});
            transform = glm::rotate(transform, worldRotation.z, {0, 0, 1});
            transform = glm::scale(transform, worldScale);
            worldMatrix = transform;

            glm::mat3 invScaleMatrix = glm::mat4{1.f};
            const glm::vec3 invScale = 1.0f / worldScale;
            invScaleMatrix[0][0] = invScale.x;
            invScaleMatrix[1][1] = invScale.y;
            invScaleMatrix[2][2] = invScale.z;
            worldNormalMatrix = GetRotationMatrix() * invScaleMatrix;
        }

        id_t parentEntityId = InvalidEntityId;
        glm::vec3 translation{};
        glm::vec3 scale{1.f, 1.f, 1.f};
        glm::vec3 rotation{};
        glm::vec3 worldTranslation{};
        glm::vec3 worldScale{1.f, 1.f, 1.f};
        glm::vec3 worldRotation{};
        glm::mat4 worldMatrix{1.f};
        glm::mat3 worldNormalMatrix{1.f};
        bool localDirty = true;
        bool hierarchyDirty = true;
        bool worldDirty = true;
    };
}

#endif
