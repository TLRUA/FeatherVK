#pragma once

#include <algorithm>
#include <limits>

#include <glm/glm.hpp>

#include "../Components/CameraComponent.hpp"
#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../Components/UIComponent.hpp"
#include "../Managers/EditorSceneUtils.hpp"
#include "../Managers/EntityCommandService.hpp"
#include "../Managers/LightEmitterMeshUtils.hpp"
#include "../Managers/TransformService.hpp"
#include "Imgui/imgui.h"
#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class EditorInspectorSystem {
    public:
        static void RenderSelectedEntity(ECS::SceneRegistry *sceneRegistry,
                                         TransformService &transformService,
                                         EntityCommandService &entityCommandService,
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
                RenderAddComponent(sceneRegistry, entityCommandService, selectedId, frameInfo);
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
        static constexpr id_t InvalidRayTracingInstanceId = std::numeric_limits<id_t>::max();

        template<typename T>
        static T *TryGet(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            T *component = nullptr;
            return sceneRegistry != nullptr && sceneRegistry->TryGetComponent(entityId, component) ? component : nullptr;
        }

        static id_t ResolveRuntimeRayTracingInstanceId(const FrameInfo &frameInfo, id_t entityId) {
            if (frameInfo.rayTracingInstanceIds == nullptr) {
                return InvalidRayTracingInstanceId;
            }

            const auto instanceIt = frameInfo.rayTracingInstanceIds->find(entityId);
            return instanceIt == frameInfo.rayTracingInstanceIds->end() ? InvalidRayTracingInstanceId : instanceIt->second;
        }

        static void SyncLightEmitterEmissive(ECS::SceneRegistry *sceneRegistry,
                                             id_t entityId,
                                             const LightComponent &lightComponent) {
            if (sceneRegistry == nullptr) {
                return;
            }
            LightEmitterMeshUtils::SyncLightEmitterEmissive(*sceneRegistry, entityId, lightComponent);
        }

        static bool IsLightEmitterMesh(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            return sceneRegistry != nullptr && LightEmitterMeshUtils::IsLightEmitterMesh(*sceneRegistry, entityId);
        }

        static bool ApplyLightEmitterMeshConstraints(ECS::SceneRegistry *sceneRegistry, id_t entityId) {
            return sceneRegistry != nullptr && LightEmitterMeshUtils::ApplyLightEmitterMeshConstraints(*sceneRegistry, entityId);
        }

        static void RenderAddComponent(ECS::SceneRegistry *sceneRegistry,
                                       EntityCommandService &entityCommandService,
                                       id_t entityId,
                                       FrameInfo &frameInfo) {
            if (sceneRegistry == nullptr || !sceneRegistry->IsAlive(entityId)) {
                return;
            }

            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                const auto options = entityCommandService.GetAvailableComponentOptions(*sceneRegistry, entityId);
                if (options.empty()) {
                    ImGui::TextDisabled("No supported components available");
                }

                for (const auto &option: options) {
                    if (option.enabled) {
                        if (ImGui::MenuItem(option.label)) {
                            if (entityCommandService.AddComponent(*sceneRegistry, entityId, option.preset, frameInfo)) {
                                ImGui::CloseCurrentPopup();
                                break;
                            }
                        }
                    } else {
                        ImGui::MenuItem(option.label, nullptr, false, false);
                        if (option.disabledReason != nullptr) {
                            ImGui::TextDisabled("  %s", option.disabledReason);
                        }
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::Separator();
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
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::Text("Rotation:");
            ImGui::SameLine(90);
            glm::vec3 rotationByDegrees = glm::degrees(transform->GetRelativeRotation());
            if (ImGui::InputFloat3("##Rotation", &rotationByDegrees.x)) {
                transformService.SetRotation(*sceneRegistry, entityId, glm::radians(rotationByDegrees));
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::Text("Scale:");
            ImGui::SameLine(90);
            glm::vec3 tempScale = transform->GetRelativeScale();
            if (ImGui::InputFloat3("##Scale", &tempScale.x)) {
                transformService.SetScale(*sceneRegistry, entityId, tempScale);
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::TreePop();
        }

        static void RenderCameraMovement(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<CameraMovementComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("CameraMovementComponent")) {
                return;
            }

            if (ImGui::InputFloat("Move Speed", &component->moveSpeed)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            if (ImGui::InputFloat("Look Speed", &component->lookSpeed)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            if (ImGui::InputFloat("Focus Move Time", &component->focusMoveTime)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::TreePop();
        }

        static void RenderObjectMovement(ECS::SceneRegistry *sceneRegistry, id_t entityId, FrameInfo &frameInfo) {
            auto *component = TryGet<ObjectMovementComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("ObjectMovementComponent")) {
                return;
            }

            if (ImGui::InputFloat("Move Speed", &component->moveSpeed)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::TreePop();
        }

        static void RenderMeshRenderer(ECS::SceneRegistry *sceneRegistry,
                                       id_t entityId,
                                       std::vector<EntityDesc> *gameObjectDescs,
                                       Material::Map *materials,
                                       FrameInfo &frameInfo) {
            auto *component = TryGet<MeshRendererComponent>(sceneRegistry, entityId);
            if (component == nullptr || !ImGui::TreeNode("MeshRendererComponent")) {
                return;
            }
            const bool isLightEmitterMesh = IsLightEmitterMesh(sceneRegistry, entityId);
            if (isLightEmitterMesh && ApplyLightEmitterMeshConstraints(sceneRegistry, entityId)) {
                EditorSceneUtils::MarkMeshRendererRenderResourcesDirty(frameInfo, entityId);
                EditorSceneUtils::MarkSceneDirty(frameInfo);
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
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            bool castShadow = component->CastsShadow();
            if (isLightEmitterMesh) {
                castShadow = false;
                ImGui::BeginDisabled(true);
            }
            if (ImGui::Checkbox("Cast Shadow", &castShadow)) {
                component->SetCastShadow(castShadow);
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            if (isLightEmitterMesh) {
                ImGui::EndDisabled();
            }
            bool receiveShadow = component->ReceivesShadow();
            if (isLightEmitterMesh) {
                receiveShadow = false;
                ImGui::BeginDisabled(true);
            }
            if (ImGui::Checkbox("Receive Shadow", &receiveShadow)) {
                component->SetReceiveShadow(receiveShadow);
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            if (isLightEmitterMesh) {
                ImGui::EndDisabled();
            }
            uint32_t renderLayer = component->GetRenderLayer();
            if (ImGui::InputScalar("Render Layer", ImGuiDataType_U32, &renderLayer)) {
                component->SetRenderLayer(std::min(renderLayer, 7u));
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

#ifdef RAY_TRACING
            const id_t runtimeRayTracingInstanceId = ResolveRuntimeRayTracingInstanceId(frameInfo, entityId);
            if (isLightEmitterMesh &&
                gameObjectDescs != nullptr &&
                runtimeRayTracingInstanceId != InvalidRayTracingInstanceId &&
                static_cast<size_t>(runtimeRayTracingInstanceId) < gameObjectDescs->size()) {
                EntityDesc &entityDesc = gameObjectDescs->at(runtimeRayTracingInstanceId);
                if (component->HasPbrOverride()) {
                    entityDesc.pbr = *component->GetPbrOverride();
                }
                entityDesc.renderOptions = 0;
            }

            if (gameObjectDescs != nullptr &&
                runtimeRayTracingInstanceId != InvalidRayTracingInstanceId &&
                static_cast<size_t>(runtimeRayTracingInstanceId) < gameObjectDescs->size() &&
                ImGui::TreeNode("PBR")) {
                const EntityDesc &desc = gameObjectDescs->at(runtimeRayTracingInstanceId);
                PBR editablePbr = component->HasPbrOverride() ? *component->GetPbrOverride() : desc.pbr;
                if (isLightEmitterMesh) {
                    if (auto *lightComponent = TryGet<LightComponent>(sceneRegistry, entityId); lightComponent != nullptr) {
                        editablePbr.emissive = LightEmitterMeshUtils::CreateLightEmitterEmissive(*lightComponent);
                    }
                }
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
                            if (isLightEmitterMesh) {
                                ImGui::BeginDisabled(true);
                            }
                            if (ImGui::InputFloat3("##Emissive", &editablePbr.emissive.x)) {
                                pbrChanged = true;
                            }
                            if (isLightEmitterMesh) {
                                ImGui::EndDisabled();
                            } else {
                                Utils::ClampVec3(editablePbr.emissive, 0, 1);
                            }
                            break;
                        default:
                            break;
                    }
                }
                if (pbrChanged) {
                    if (isLightEmitterMesh) {
                        if (auto *lightComponent = TryGet<LightComponent>(sceneRegistry, entityId); lightComponent != nullptr) {
                            editablePbr.emissive = LightEmitterMeshUtils::CreateLightEmitterEmissive(*lightComponent);
                        }
                    }
                    component->SetPbrOverride(editablePbr);
                    gameObjectDescs->at(runtimeRayTracingInstanceId).pbr = editablePbr;
                    EditorSceneUtils::MarkMeshRendererRenderResourcesDirty(frameInfo, entityId);
                    EditorSceneUtils::MarkSceneDirty(frameInfo);
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

            bool lightChanged = false;

            ImGui::Text("Color:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Color", &component->color.x)) {
                Utils::ClampVec3(component->color, 0.0f, 1.0f);
                lightChanged = true;
            }

            ImGui::Text("Intensity:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat("##Intensity", &component->lightIntensity)) {
                component->lightIntensity = std::max(component->lightIntensity, 0.0f);
                lightChanged = true;
            }

            if (lightChanged) {
                SyncLightEmitterEmissive(sceneRegistry, entityId, *component);
                if (sceneRegistry != nullptr && sceneRegistry->HasComponent<MeshRendererComponent>(entityId)) {
                    EditorSceneUtils::MarkMeshRendererRenderResourcesDirty(frameInfo, entityId);
                }
                EditorSceneUtils::MarkSceneDirty(frameInfo);
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
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::Text("Omega:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Omega", &component->omega.x)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            ImGui::Text("Mass:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat("##Mass", &component->totalMass)) {
                component->inverseMass = component->totalMass > RigidBodyComponent::EPSILON ? 1.0f / component->totalMass : 0.0f;
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }

            if (ImGui::Checkbox("Use Gravity", &component->useGravity)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
            }
            if (ImGui::Checkbox("Is Kinematic", &component->isKinematic)) {
                EditorSceneUtils::MarkSceneDirty(frameInfo);
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

