#pragma once

#include <algorithm>
#include <limits>

#include <glm/glm.hpp>

#include "../Components/CameraComponent.hpp"
#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RayTracingInstanceComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Components/UIComponent.hpp"
#include "../Managers/TransformService.hpp"
#include "Imgui/imgui.h"
#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class EditorInspectorSystem {
    public:
        static void RenderSelectedEntity(ECS::SceneRegistry *sceneRegistry,
                                         TransformService &transformService,
                                         id_t selectedId,
                                         std::vector<EntityDesc> *gameObjectDescs,
                                         Material::Map *materials,
                                         FrameInfo &frameInfo) {
            if (sceneRegistry == nullptr || !sceneRegistry->IsAlive(selectedId)) {
                return;
            }

            ImGui::Text("Name:");
            ImGui::SameLine(70);
            ImGui::Text(sceneRegistry->GetEntityName(selectedId).c_str());

            RenderTransform(sceneRegistry, transformService, selectedId, frameInfo);

            if (ImGui::TreeNode("Components")) {
                RenderCameraMovement(sceneRegistry, selectedId, frameInfo);
                RenderObjectMovement(sceneRegistry, selectedId, frameInfo);
                RenderMeshRenderer(sceneRegistry, selectedId, gameObjectDescs, materials, frameInfo);
                RenderCamera(sceneRegistry, selectedId);
                RenderLight(sceneRegistry, selectedId, frameInfo);
                RenderRigidBody(sceneRegistry, selectedId, frameInfo);
                RenderUi(sceneRegistry, selectedId);
                ImGui::TreePop();
            }
        }

    private:
        template<typename T>
        static T *TryGet(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            T *component = nullptr;
            return sceneRegistry != nullptr && sceneRegistry->TryGetComponent(entityId, component) ? component : nullptr;
        }

        static void RenderTransform(ECS::SceneRegistry *sceneRegistry,
                                    TransformService &transformService,
                                    id_t entityId,
                                    FrameInfo &frameInfo) {
            auto *transform = TryGet<TransformComponent>(sceneRegistry, entityId);
            if (transform == nullptr || !ImGui::TreeNode("Transform")) {
                return;
            }

            ImGui::Text("Position:");
            ImGui::SameLine(90);
            glm::vec3 tempPosition = transform->GetRelativeTranslation();
            if (ImGui::InputFloat3("##Position", &tempPosition.x)) {
                transformService.SetTranslation(*sceneRegistry, entityId, tempPosition);
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Rotation:");
            ImGui::SameLine(90);
            glm::vec3 rotationByDegrees = glm::degrees(transform->GetRelativeRotation());
            if (ImGui::InputFloat3("##Rotation", &rotationByDegrees.x)) {
                transformService.SetRotation(*sceneRegistry, entityId, glm::radians(rotationByDegrees));
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Scale:");
            ImGui::SameLine(90);
            glm::vec3 tempScale = transform->GetRelativeScale();
            if (ImGui::InputFloat3("##Scale", &tempScale.x)) {
                transformService.SetScale(*sceneRegistry, entityId, tempScale);
                frameInfo.sceneUpdated = true;
            }

            ImGui::TreePop();
        }

        static void RenderCameraMovement(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<CameraMovementComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("CameraMovementComponent")) {
                return;
            }

            if (ImGui::InputFloat("Move Speed", &component->moveSpeed)) {
                frameInfo.sceneUpdated = true;
            }
            if (ImGui::InputFloat("Look Speed", &component->lookSpeed)) {
                frameInfo.sceneUpdated = true;
            }
            if (ImGui::InputFloat("Focus Move Time", &component->focusMoveTime)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::TreePop();
        }

        static void RenderObjectMovement(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<ObjectMovementComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("ObjectMovementComponent")) {
                return;
            }

            if (ImGui::InputFloat("Move Speed", &component->moveSpeed)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::TreePop();
        }

        static void RenderMeshRenderer(ECS::SceneRegistry *sceneRegistry,
                                       id_t entityId,
                                       std::vector<EntityDesc> *gameObjectDescs,
                                       Material::Map *materials,
                                       FrameInfo &frameInfo) {
            auto *component = TryGet<MeshRendererComponent>(sceneRegistry, entityId);
            auto *rayTracingInstance = TryGet<RayTracingInstanceComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("MeshRendererComponent")) {
                return;
            }

            if (component->GetModelPtr() != nullptr) {
                ImGui::Text("Model:       %s", component->GetModelPtr()->GetName().c_str());
                ImGui::Text("Vertices:    %u", component->GetModelPtr()->getVertexCount());
                ImGui::Text("Indices:     %u", component->GetModelPtr()->getIndexCount());
            } else {
                ImGui::Text("Model:       <none>");
            }
            ImGui::Text("Material:    %d", component->GetMaterialID());

            bool visible = component->IsVisible();
            if (ImGui::Checkbox("Visible", &visible)) {
                component->SetVisible(visible);
                frameInfo.sceneUpdated = true;
            }
            bool castShadow = component->CastsShadow();
            if (ImGui::Checkbox("Cast Shadow", &castShadow)) {
                component->SetCastShadow(castShadow);
                frameInfo.sceneUpdated = true;
            }
            bool receiveShadow = component->ReceivesShadow();
            if (ImGui::Checkbox("Receive Shadow", &receiveShadow)) {
                component->SetReceiveShadow(receiveShadow);
                frameInfo.sceneUpdated = true;
            }
            uint32_t renderLayer = component->GetRenderLayer();
            if (ImGui::InputScalar("Render Layer", ImGuiDataType_U32, &renderLayer)) {
                component->SetRenderLayer(std::min(renderLayer, 7u));
                frameInfo.sceneUpdated = true;
            }

#ifdef RAY_TRACING
            if (rayTracingInstance != nullptr && rayTracingInstance->IsValid()) {
                ImGui::Text("RT Instance: %u", rayTracingInstance->instanceId);
            }

            if (gameObjectDescs != nullptr &&
                rayTracingInstance != nullptr &&
                rayTracingInstance->IsValid() &&
                static_cast<size_t>(rayTracingInstance->instanceId) < gameObjectDescs->size() &&
                ImGui::TreeNode("PBR")) {
                const EntityDesc &desc = gameObjectDescs->at(rayTracingInstance->instanceId);
                PBR editablePbr = component->HasPbrOverride() ? *component->GetPbrOverride() : desc.pbr;
                auto validProperty = PBRLoader::GetValidProperty(editablePbr);
                bool pbrChanged = false;
                for (const auto &item: validProperty) {
                    switch (item) {
                        case 0:
                            ImGui::Text("Albedo:");
                            ImGui::SameLine(120);
                            ImGui::SetNextItemWidth(140);
                            if (ImGui::InputFloat3("##Albedo", &editablePbr.albedo.x)) {
                                pbrChanged = true;
                            }
                            Utils::ClampVec3(editablePbr.albedo, 0, 1);
                            break;
                        case 2:
                            ImGui::Text("Metallic:");
                            ImGui::SameLine(120);
                            ImGui::SetNextItemWidth(140);
                            if (ImGui::InputFloat("##Metallic", &editablePbr.metallic)) {
                                pbrChanged = true;
                            }
                            Utils::ClampFloat(editablePbr.metallic, 0, 1);
                            break;
                        case 3:
                            ImGui::Text("Roughness:");
                            ImGui::SameLine(120);
                            ImGui::SetNextItemWidth(140);
                            if (ImGui::InputFloat("##Roughness", &editablePbr.roughness)) {
                                pbrChanged = true;
                            }
                            Utils::ClampFloat(editablePbr.roughness, 0, 1);
                            break;
                        case 4:
                            ImGui::Text("Opacity:");
                            ImGui::SameLine(120);
                            ImGui::SetNextItemWidth(140);
                            if (ImGui::InputFloat("##Opacity", &editablePbr.opacity)) {
                                pbrChanged = true;
                            }
                            Utils::ClampFloat(editablePbr.opacity, 0, 1);
                            break;
                        case 6:
                            ImGui::Text("Emissive:");
                            ImGui::SameLine(120);
                            ImGui::SetNextItemWidth(140);
                            if (ImGui::InputFloat3("##Emissive", &editablePbr.emissive.x)) {
                                pbrChanged = true;
                            }
                            Utils::ClampVec3(editablePbr.emissive, 0, 1);
                            break;
                        default:
                            break;
                    }
                }
                if (pbrChanged) {
                    component->SetPbrOverride(editablePbr);
                    gameObjectDescs->at(rayTracingInstance->instanceId).pbr = editablePbr;
                    frameInfo.sceneUpdated = true;
                }
                ImGui::TreePop();
            }
#else
            if (materials != nullptr) {
                auto materialIt = materials->find(component->GetMaterialID());
                if (materialIt != materials->end() && materialIt->second != nullptr) {
                    ImGui::Text("Category:    %s", materialIt->second->getPipelineCategory().c_str());
                }
            }
#endif

            ImGui::TreePop();
        }

        static void RenderCamera(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            auto *component = TryGet<CameraComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("CameraComponent")) {
                return;
            }

            ImGui::Text("Projection/View matrices are updated by CameraSystem.");
            ImGui::TreePop();
        }

        static void RenderLight(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<LightComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("LightComponent")) {
                return;
            }

            ImGui::Text("Color:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Color", &component->color.x)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Intensity:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat("##Intensity", &component->lightIntensity)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Type:");
            ImGui::SameLine(90);
            ImGui::Text(component->GetLightTypeLabel());
            ImGui::TreePop();
        }

        static void RenderRigidBody(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<RigidBodyComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("RigidBodyComponent")) {
                return;
            }

            ImGui::Text("Velocity:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Velocity", &component->velocity.x)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Omega:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Omega", &component->omega.x)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Mass:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat("##Mass", &component->totalMass)) {
                component->inverseMass = component->totalMass > RigidBodyComponent::EPSILON ? 1.0f / component->totalMass : 0.0f;
                frameInfo.sceneUpdated = true;
            }

            if (ImGui::Checkbox("Use Gravity", &component->useGravity)) {
                frameInfo.sceneUpdated = true;
            }
            if (ImGui::Checkbox("Is Kinematic", &component->isKinematic)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::TreePop();
        }

        static void RenderUi(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            auto *component = TryGet<UIComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("UIComponent")) {
                return;
            }

            ImGui::Text("Type:");
            ImGui::SameLine(90);
            ImGui::Text(UIComponent::GetElementTypeName(component->GetElementType()).c_str());
            ImGui::Text("Render:");
            ImGui::SameLine(90);
            ImGui::Text("Editor marker only");
            ImGui::TreePop();
        }
    };
}
