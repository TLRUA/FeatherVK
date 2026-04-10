#pragma once

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_set>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "../Core/InputState.hpp"
#include "../Descriptor.h"
#include "../Device.hpp"
#include "../GUI.hpp"
#include "../Image.h"
#include "../Material.hpp"
#include "../MyWindow.hpp"
#include "../Pipeline.hpp"
#include "../RenderCore/RenderCore.hpp"
#include "../Renderer.h"
#include "../Sampler.h"
#include "../Utils/JsonUtils.hpp"

#include "../Utils/ProjectPaths.hpp"
#include "../Components/CameraComponent.hpp"
#include "../Components/Input/CameraMovementComponent.hpp"
#include "../Components/Input/ObjectMovementComponent.hpp"
#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../Components/RigidBodyComponent.hpp"
#include "../Components/TransformComponent.hpp"
#include "../ECS/SceneRegistry.hpp"
#include "../Managers/EntityCommandService.hpp"
#include "../Managers/EditorSelectionService.hpp"
#include "../Managers/HierarchyService.hpp"
#include "../Managers/ModelRepository.hpp"
#include "../RHI/RHIDevice.hpp"
#include "../Managers/SceneComponentLoader.hpp"
#include "../Managers/TransformService.hpp"
#include "../Components/UIComponent.hpp"
#include "../Systems/TransformHierarchySystem.hpp"
#ifdef RAY_TRACING
#include "../Managers/RayTracingSceneContext.hpp"
#endif

namespace FeatherVK {
#ifdef RAY_TRACING
    inline const static std::string ConfigPath = "RayTracing/";
    const std::string RayTracingDenoiseComputeShaderName = "Compute/RayTracingDenoise.comp.spv";
    const std::string PostVertexShaderName = "Post/passthrough.vert.spv";
    const std::string PostFragmentShaderName = "Post/post.frag.spv";
#else
    inline const static std::string ConfigPath = "Rasterization/";
#endif
    inline std::string GetBasePath() { return ProjectPaths::ConfigurationsDir(ConfigPath); }
    inline std::string GetBaseTexturePath() { return ProjectPaths::TexturesDir(); }
    inline const static std::string EntitiesFileName = "Entities.json";
    inline const static std::string MaterialsFileName = "Materials.json";
    inline const static std::string ComponentsFileName = "Components.json";
    inline const static std::string SkyboxCubeMapName = "Cubemap";

    const int MATERIAL_NUMBER = 16;
#ifdef RAY_TRACING
    constexpr uint32_t RuntimeEntityDescCapacity = 1024;
#endif

    class ResourceManager final : public IEditorScenePersistence, public IRenderInvalidationSink {
    public:
        ResourceManager()
            : m_modelRepository(m_device),
              m_sceneComponentLoader(m_modelRepository)
#ifdef RAY_TRACING
              , m_rayTracingSceneContext(m_device)
#endif
        {
            m_inputState.Attach(m_window.getGLFWwindow());
            m_globalPool = DescriptorPool::Builder(m_device).
                    setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).build();
            m_modelRepository.SetRenderResourceRegistry(&m_renderCore.GetResourceRegistry());
            m_entityCommandService.SetDependencies(m_modelRepository
#ifdef RAY_TRACING
                , &m_rayTracingSceneContext
#endif
            );
            loadEntities();
            loadMaterials();
            GUI::Init(m_renderer, m_window, m_device);
        }

        ~ResourceManager() {
            if (m_device.device() != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(m_device.device());
            }
            m_sceneRegistry.Clear();
#ifdef RAY_TRACING
            m_rayTracingSceneContext.Release();
            m_pEntityDescBuffer.reset();
            m_pEntityDescs.clear();
#endif
            m_textureCache.clear();
            m_modelRepository.Clear();
            m_materials.clear();
        }


        Device &GetDevice() { return m_device; }

        RHI::RHIDevice &GetRHI() { return m_device; }
        const RHI::RHIDevice &GetRHI() const { return m_device; }

        MyWindow &GetWindow() { return m_window; }

        InputState &GetInputState() { return m_inputState; }

        HierarchyTree &GetHierarchyTree() { return m_hierarchyService.GetTree(); }
        HierarchyService &GetHierarchyService() { return m_hierarchyService; }
        EntityCommandService &GetEntityCommandService() { return m_entityCommandService; }
        EditorSelectionService &GetEditorSelectionService() { return m_editorSelectionService; }
        TransformService &GetTransformService() { return m_transformService; }
        ModelRepository &GetModelRepository() { return m_modelRepository; }

        Material::Map &GetMaterials() { return m_materials; }


        ECS::SceneRegistry &GetSceneRegistry() { return m_sceneRegistry; }
        const ECS::SceneRegistry &GetSceneRegistry() const { return m_sceneRegistry; }

        Renderer &GetRenderer() { return m_renderer; }

        RenderCore::CoreServices &GetRenderCore() { return m_renderCore; }
        const RenderCore::CoreServices &GetRenderCore() const { return m_renderCore; }

        [[nodiscard]] bool IsSceneDirty() const override { return m_sceneDirty; }

        void MarkSceneDirty() override { m_sceneDirty = true; }

        void ClearSceneDirty() override { m_sceneDirty = false; }

        void MarkRenderSceneDirty() override {
            m_renderSceneDirty = true;
        }

        [[nodiscard]] bool IsRenderSceneDirty() const {
            return m_renderSceneDirty;
        }

        bool ConsumeRenderSceneDirty() {
            const bool dirty = m_renderSceneDirty;
            m_renderSceneDirty = false;
            return dirty;
        }

        void MarkMeshRendererRenderResourcesDirty(id_t entityId) override {
            if (entityId != std::numeric_limits<id_t>::max()) {
                m_dirtyMeshRendererRenderResourceEntities.insert(entityId);
            }
            m_renderSceneDirty = true;
        }

        void MarkAllMeshRendererRenderResourcesDirty() override {
            m_meshRendererRenderResourcesFullRebuildDirty = true;
            m_dirtyMeshRendererRenderResourceEntities.clear();
            m_renderSceneDirty = true;
        }

        void RequestSceneSave() override {
            m_sceneSaveRequested = true;
            m_sceneDirty = true;
        }

        [[nodiscard]] bool ConsumeSceneSaveRequest() override {
            const bool shouldSave = m_sceneSaveRequested;
            m_sceneSaveRequested = false;
            return shouldSave;
        }

        bool SyncSceneViewportLayout(const ViewportRect &scenePanelRect, const ViewportRect &sceneViewportRect) {
            const bool sceneExtentChanged = m_renderer.UpdateSceneViewportLayout(scenePanelRect, sceneViewportRect);
            if (sceneExtentChanged) {
                m_renderCoreRenderTargetsDirty = true;
                MarkSceneSizedImageDescriptorsDirty();
                MarkRenderSceneDirty();
            }
            return sceneExtentChanged;
        }

        void SyncRenderCoreSceneResources() {
            if (!m_renderCoreStaticResourcesRegistered) {
                RegisterStaticRenderCoreResources();
                m_renderCoreStaticResourcesRegistered = true;
                m_renderCoreRenderTargetsDirty = true;
            }
            if (m_renderCoreRenderTargetsDirty) {
                RegisterRendererRenderTargets();
                m_renderCoreRenderTargetsDirty = false;
            }
            SyncMeshRendererRenderCoreResources();
            FlushPendingDescriptorRefreshes();
        }

#ifdef RAY_TRACING
        std::shared_ptr<Buffer>& GetEntityDescBuffer() { return m_pEntityDescBuffer; }
        std::vector<EntityDesc>& GetEntityDescs() { return m_pEntityDescs; }
        RayTracingSceneContext &GetRayTracingSceneContext() { return m_rayTracingSceneContext; }

        bool TryGetRayTracingMaterialDesc(Material::id_t materialId, EntityDesc &entityDesc) const {
            const auto entry = m_rayTracingMaterialDescs.find(materialId);
            if (entry == m_rayTracingMaterialDescs.end()) {
                return false;
            }
            entityDesc = entry->second;
            return true;
        }

        uint32_t GetRayTracingShaderOffset(Material::id_t materialId) const {
            const auto entry = m_rayTracingShaderOffsets.find(materialId);
            return entry == m_rayTracingShaderOffsets.end() ? 0u : entry->second;
        }

        bool HasValidRayTracingTlas() const {
            return m_rayTracingSceneContext.HasValidTlas();
        }

        void MarkRayTracingTlasDescriptorDirty() {
            MarkRayTracingRayGenDescriptorsDirty();
        }
#endif

        bool FlushPendingDescriptorRefreshes() {
            bool refreshed = false;
            if (m_postDescriptorDirty) {
                if (RefreshPostDescriptorSet()) {
                    m_postDescriptorDirty = false;
                    refreshed = true;
                }
            }
#ifdef RAY_TRACING
            if (m_rayTracingRayGenDescriptorDirty) {
                if (RefreshRayTracingRayGenDescriptorSet()) {
                    m_rayTracingRayGenDescriptorDirty = false;
                    refreshed = true;
                }
            }
            if (m_computeDescriptorDirty) {
                if (RefreshComputeDescriptorSet()) {
                    m_computeDescriptorDirty = false;
                    refreshed = true;
                }
            }
#endif
            return refreshed;
        }

        void SaveScene() {
            rapidjson::Document entitiesDocument;
            entitiesDocument.SetArray();
            rapidjson::Document componentsDocument;
            componentsDocument.SetArray();

            auto &entityAllocator = entitiesDocument.GetAllocator();
            auto &componentAllocator = componentsDocument.GetAllocator();

            const auto makeStringValue = [](const std::string &value, auto &allocator) {
                rapidjson::Value stringValue;
                stringValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
                return stringValue;
            };
            const auto makeVec3Value = [](const glm::vec3 &value, auto &allocator) {
                rapidjson::Value vec(rapidjson::kArrayType);
                vec.PushBack(value.x, allocator);
                vec.PushBack(value.y, allocator);
                vec.PushBack(value.z, allocator);
                return vec;
            };
            const auto makeTransformValue = [&](const TransformComponent &transform) {
                rapidjson::Value transformObject(rapidjson::kObjectType);
                transformObject.AddMember("translation", makeVec3Value(transform.GetRelativeTranslation(), entityAllocator), entityAllocator);
                transformObject.AddMember("scale", makeVec3Value(transform.GetRelativeScale(), entityAllocator), entityAllocator);
                transformObject.AddMember("rotation", makeVec3Value(glm::degrees(transform.GetRelativeRotation()), entityAllocator), entityAllocator);
                return transformObject;
            };
            int nextComponentId = 1;
            const auto appendComponent = [&](rapidjson::Value &componentIds, rapidjson::Value componentObject) {
                componentObject.AddMember("id", nextComponentId, componentAllocator);
                componentsDocument.PushBack(componentObject, componentAllocator);
                componentIds.PushBack(nextComponentId, entityAllocator);
                ++nextComponentId;
            };

            for (const auto entityId: m_sceneRegistry.GetEntityOrder()) {
                if (!m_sceneRegistry.IsAlive(entityId) || m_entityCommandService.IsPendingDestroy(entityId)) {
                    continue;
                }

                rapidjson::Value entityObject(rapidjson::kObjectType);
                entityObject.AddMember("id", entityId, entityAllocator);
                entityObject.AddMember("name", makeStringValue(m_sceneRegistry.GetEntityName(entityId), entityAllocator), entityAllocator);
                entityObject.AddMember("IsActive", m_sceneRegistry.IsEntityActive(entityId), entityAllocator);

                if (auto *transform = TryGetSceneComponent<TransformComponent>(entityId); transform != nullptr) {
                    entityObject.AddMember("transform", makeTransformValue(*transform), entityAllocator);
                    if (transform->HasParent()) {
                        entityObject.AddMember("parentId", transform->GetParentEntityId(), entityAllocator);
                    }
                }

                rapidjson::Value componentIds(rapidjson::kArrayType);

                if (auto *meshRenderer = TryGetSceneComponent<MeshRendererComponent>(entityId); meshRenderer != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("MeshRendererComponent", componentAllocator), componentAllocator);
                    if (meshRenderer->GetModelPtr() != nullptr) {
                        componentObject.AddMember("model", makeStringValue(meshRenderer->GetModelPtr()->GetName(), componentAllocator), componentAllocator);
                    }
                    componentObject.AddMember("materialId", meshRenderer->GetMaterialID(), componentAllocator);
                    componentObject.AddMember("visible", meshRenderer->IsVisible(), componentAllocator);
                    componentObject.AddMember("renderLayer", meshRenderer->GetRenderLayer(), componentAllocator);
                    componentObject.AddMember("castShadow", meshRenderer->CastsShadow(), componentAllocator);
                    componentObject.AddMember("receiveShadow", meshRenderer->ReceivesShadow(), componentAllocator);
                    if (meshRenderer->HasPbrOverride()) {
                        const PBR &pbr = *meshRenderer->GetPbrOverride();
                        rapidjson::Value pbrObject(rapidjson::kObjectType);
                        pbrObject.AddMember("albedo", makeVec3Value(pbr.albedo, componentAllocator), componentAllocator);
                        pbrObject.AddMember("normal", makeVec3Value(pbr.normal, componentAllocator), componentAllocator);
                        pbrObject.AddMember("metallic", pbr.metallic, componentAllocator);
                        pbrObject.AddMember("roughness", pbr.roughness, componentAllocator);
                        pbrObject.AddMember("opacity", pbr.opacity, componentAllocator);
                        pbrObject.AddMember("ao", pbr.AO, componentAllocator);
                        pbrObject.AddMember("emissive", makeVec3Value(pbr.emissive, componentAllocator), componentAllocator);
                        componentObject.AddMember("pbrOverride", pbrObject, componentAllocator);
                    }
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (TryGetSceneComponent<CameraComponent>(entityId) != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("CameraComponent", componentAllocator), componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (auto *cameraMovement = TryGetSceneComponent<CameraMovementComponent>(entityId); cameraMovement != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("CameraMovementComponent", componentAllocator), componentAllocator);
                    componentObject.AddMember("moveSpeed", cameraMovement->moveSpeed, componentAllocator);
                    componentObject.AddMember("lookSpeed", cameraMovement->lookSpeed, componentAllocator);
                    componentObject.AddMember("focusMoveTime", cameraMovement->focusMoveTime, componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (auto *objectMovement = TryGetSceneComponent<ObjectMovementComponent>(entityId); objectMovement != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("ObjectMovementComponent", componentAllocator), componentAllocator);
                    componentObject.AddMember("moveSpeed", objectMovement->moveSpeed, componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (auto *light = TryGetSceneComponent<LightComponent>(entityId); light != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("LightComponent", componentAllocator), componentAllocator);
                    componentObject.AddMember("category", makeStringValue(light->GetLightTypeLabel(), componentAllocator), componentAllocator);
                    componentObject.AddMember("color", makeVec3Value(light->color, componentAllocator), componentAllocator);
                    componentObject.AddMember("intensity", light->lightIntensity, componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (auto *rigidBody = TryGetSceneComponent<RigidBodyComponent>(entityId); rigidBody != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("RigidBodyComponent", componentAllocator), componentAllocator);
                    componentObject.AddMember("velocity", makeVec3Value(rigidBody->velocity, componentAllocator), componentAllocator);
                    componentObject.AddMember("omega", makeVec3Value(rigidBody->omega, componentAllocator), componentAllocator);
                    componentObject.AddMember("useGravity", rigidBody->useGravity, componentAllocator);
                    componentObject.AddMember("isKinematic", rigidBody->isKinematic, componentAllocator);
                    componentObject.AddMember("totalMass", rigidBody->totalMass, componentAllocator);
                    componentObject.AddMember("restitution", rigidBody->restitution, componentAllocator);
                    componentObject.AddMember("friction", rigidBody->friction, componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                if (auto *uiComponent = TryGetSceneComponent<UIComponent>(entityId); uiComponent != nullptr) {
                    rapidjson::Value componentObject(rapidjson::kObjectType);
                    componentObject.AddMember("type", makeStringValue("UIComponent", componentAllocator), componentAllocator);
                    componentObject.AddMember("uiType", makeStringValue(UIComponent::GetElementTypeName(uiComponent->GetElementType()), componentAllocator), componentAllocator);
                    appendComponent(componentIds, std::move(componentObject));
                }

                entityObject.AddMember("componentIds", componentIds, entityAllocator);
                entitiesDocument.PushBack(entityObject, entityAllocator);
            }

            rapidjson::StringBuffer entitiesBuffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> entitiesWriter(entitiesBuffer);
            entitiesDocument.Accept(entitiesWriter);

            rapidjson::StringBuffer componentsBuffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> componentsWriter(componentsBuffer);
            componentsDocument.Accept(componentsWriter);

            JsonUtils::WriteJsonFile(GetBasePath() + EntitiesFileName, entitiesBuffer.GetString());
            JsonUtils::WriteJsonFile(GetBasePath() + ComponentsFileName, componentsBuffer.GetString());
            ClearSceneDirty();
            m_sceneSaveRequested = false;
        }

        void loadEntities() {
            struct HierarchyEntry {
                id_t entityId;
                std::optional<id_t> parentEntityId{};
            };

            m_sceneRegistry.Clear();
            m_hierarchyService.Reset();
            m_entityCommandService.Reset();
            m_editorSelectionService.ClearSelection();
            m_transformService.Reset();
#ifdef RAY_TRACING
            m_rayTracingSceneContext.Release();
#endif
            InvalidateRenderCoreRegistry();
            ClearSceneDirty();
            m_sceneSaveRequested = false;

            std::string entitiesJsonString = JsonUtils::ReadJsonFile(GetBasePath() + EntitiesFileName);
            std::string componentsJsonString = JsonUtils::ReadJsonFile(GetBasePath() + ComponentsFileName);

            rapidjson::Document entitiesDocument;
            rapidjson::Document componentsDocument;
            entitiesDocument.Parse(entitiesJsonString.c_str());
            componentsDocument.Parse(componentsJsonString.c_str());

            std::unordered_map<int, rapidjson::Value> componentsMap;
            if (componentsDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < componentsDocument.Size(); i++) {
                    rapidjson::Value componentValue = std::move(componentsDocument[i]);
                    componentsMap[componentValue["id"].GetInt()] = componentValue;
                }
            }

            std::vector<HierarchyEntry> hierarchyEntries{};

            if (entitiesDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < entitiesDocument.Size(); i++) {
                    const rapidjson::Value &object = entitiesDocument[i];

                    const std::string entityName = object.HasMember("name") ? object["name"].GetString() : "Entity";
                    const bool active = !object.HasMember("IsActive") || object["IsActive"].GetBool();
                    const id_t entityId = object.HasMember("id")
                                              ? m_sceneRegistry.CreateEntityWithId(object["id"].GetUint(), entityName, active)
                                              : m_sceneRegistry.CreateEntity(entityName, active);
                    TransformComponent *transformComponentPtr = m_sceneRegistry.EmplaceComponent<TransformComponent>(entityId);
                    HierarchyEntry hierarchyEntry{entityId, object.HasMember("parentId")
                                                               ? std::optional<id_t>{object["parentId"].GetUint()}
                                                               : std::nullopt};

                    if (object.HasMember("transform")) {
                        const rapidjson::Value &transformJsonObj = object["transform"];
                        const rapidjson::Value &translationArray = transformJsonObj["translation"];
                        const rapidjson::Value &scaleArray = transformJsonObj["scale"];
                        const rapidjson::Value &rotationArray = transformJsonObj["rotation"];

                        m_transformService.SetTranslation(m_sceneRegistry, entityId, glm::vec3{
                            translationArray[0].GetFloat(),
                            translationArray[1].GetFloat(),
                            translationArray[2].GetFloat()});
                        m_transformService.SetScale(m_sceneRegistry, entityId, glm::vec3{
                            scaleArray[0].GetFloat(),
                            scaleArray[1].GetFloat(),
                            scaleArray[2].GetFloat()});
                        m_transformService.SetRotation(m_sceneRegistry, entityId, glm::radians(glm::vec3{
                            rotationArray[0].GetFloat(),
                            rotationArray[1].GetFloat(),
                            rotationArray[2].GetFloat()}));
                    }

                    hierarchyEntries.push_back(std::move(hierarchyEntry));

                    if (object.HasMember("componentIds")) {
                        auto componentIdsArray = object["componentIds"].GetArray();
                        for (rapidjson::SizeType j = 0; j < componentIdsArray.Size(); j++) {
                            const int componentId = componentIdsArray[j].GetInt();
                            m_sceneComponentLoader.Emplace(m_sceneRegistry, entityId, componentsMap[componentId]);
                        }
                    }
                }
            }

            for (const auto &hierarchyEntry: hierarchyEntries) {
                m_transformService.SetParent(m_sceneRegistry, m_hierarchyService, hierarchyEntry.entityId, hierarchyEntry.parentEntityId);
            }

            TransformHierarchySystem{}.Update(m_sceneRegistry, m_hierarchyService, m_transformService);

#ifdef RAY_TRACING
            for (const auto entityId: m_sceneRegistry.View<MeshRendererComponent>()) {
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                if (meshRendererComponent != nullptr && meshRendererComponent->GetModelPtr() != nullptr) {
                    m_rayTracingSceneContext.QueueBlasBuild(meshRendererComponent->GetModelPtr());
                }
            }
            m_rayTracingSceneContext.BuildPendingBlas(
                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
#endif
        }

        void loadMaterials() {
            auto &shaderLibrary = m_renderCore.GetShaderLibrary();

            uint32_t minUniformOffsetAlignment = std::lcm(m_device.properties.limits.minUniformBufferOffsetAlignment,
                                                          m_device.properties.limits.nonCoherentAtomSize);
            std::string materialsString = JsonUtils::ReadJsonFile(GetBasePath() + "Materials.json");
            rapidjson::Document materialsDocument;
            materialsDocument.Parse(materialsString.c_str());

            auto globalUboBufferPtr = std::make_shared<Buffer>(
                    m_device, sizeof(GlobalUbo), SwapChain::MAX_FRAMES_IN_FLIGHT,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, minUniformOffsetAlignment);
            globalUboBufferPtr->map();

#ifdef RAY_TRACING
            uint32_t minStorageBufferOffsetAlignment = m_device.properties2.properties.limits.minStorageBufferOffsetAlignment;
        {
            std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{};
            std::vector<std::shared_ptr<Image>> imagePointers{m_renderer.getOffscreenImageColor(0), m_renderer.getOffscreenImageColor(1)};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{};
            std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{};
            std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};
            std::vector<VkDescriptorImageInfo> imageInfos;


            //Generate Shader
            const std::string rayGenShaderPath = "RayTracing/raytrace.rgen.spv";
            shaderModulePointers.push_back(shaderLibrary.LoadStage(rayGenShaderPath, ShaderCategory::rayGen));

            //Miss Shader
            const std::string rayMissShaderPath = "RayTracing/raytrace.rmiss.spv";
            shaderModulePointers.push_back(shaderLibrary.LoadStage(rayMissShaderPath, ShaderCategory::rayMiss));
            const std::string rayMiss2ShaderPath = "RayTracing/raytraceShadow.rmiss.spv";
            shaderModulePointers.push_back(shaderLibrary.LoadStage(rayMiss2ShaderPath, ShaderCategory::rayMiss2));

            //Load materials
            std::unordered_map<int, glm::vec2> textureEntries{};
            std::unordered_map<int, PBR> pbrMaterials{};
            std::unordered_map<int, int> idShaderOffsetMap{};
            int shaderGroupOffset = 0;
            if (materialsDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < materialsDocument.Size(); i++) {
                    const rapidjson::Value &object = materialsDocument[i];
                    const int id = object["id"].GetInt();
                    const std::string rayClosestShaderName = object["rayClosestHitShader"].GetString();
                    shaderModulePointers.push_back(shaderLibrary.LoadStage(rayClosestShaderName, ShaderCategory::rayClosestHit));
                    const std::string rayAnyHitShaderName = object.HasMember("rayAnyHitShader")
                        ? object["rayAnyHitShader"].GetString()
                        : "RayTracing/anyHit.rahit.spv";
                    shaderModulePointers.push_back(shaderLibrary.LoadStage(rayAnyHitShaderName, ShaderCategory::rayAnyHit));

                    idShaderOffsetMap.emplace(id, shaderGroupOffset);
                    shaderGroupOffset++;

                    auto textureNames = object["texture"].GetArray();
                    glm::i32vec2 textureEntry{};
                    textureEntry.x = imageInfos.size();
                    for (auto &textureNameGenericValue: textureNames) {
                        std::string textureName = textureNameGenericValue.GetString();
                        auto texture = GetOrCreateTexture(textureName);
                        auto imageInfo = texture.image->descriptorInfo(*texture.sampler);
                        imagePointers.emplace_back(texture.image);
                        samplerPointers.emplace_back(texture.sampler);
                        imageInfos.emplace_back(*imageInfo);
                    }
                    textureEntry.y = imageInfos.size() - textureEntry.x;
                    if (object.HasMember("PBR")) {
                        auto pbr = PBRLoader::loadPBR(object["PBR"]);
                        if (PBRParametersCount - PBRLoader::getValidPropertyCount(pbr) != textureEntry.y) {
                            throw std::runtime_error("Texture count does not match with PBR properties");
                        }
                        pbrMaterials.emplace(id, pbr);
                    }
                    textureEntries.emplace(id, textureEntry);
                }
            }
            m_rayTracingShaderOffsets.clear();
            m_rayTracingMaterialDescs.clear();
            for (const auto &entry: idShaderOffsetMap) {
                m_rayTracingShaderOffsets.emplace(entry.first, static_cast<uint32_t>(entry.second));
            }
            for (const auto &entry: textureEntries) {
                EntityDesc materialDesc{};
                materialDesc.textureEntry = entry.second;
                const auto pbrEntry = pbrMaterials.find(entry.first);
                if (pbrEntry != pbrMaterials.end()) {
                    materialDesc.pbr = pbrEntry->second;
                } else {
                    materialDesc.pbr.albedo = glm::vec3{0.8f, 0.2f, 0.2f};
                    materialDesc.pbr.normal = glm::vec3{0.0f};
                    materialDesc.pbr.metallic = 0.0f;
                    materialDesc.pbr.roughness = 1.0f;
                    materialDesc.pbr.opacity = 1.0f;
                    materialDesc.pbr.AO = 1.0f;
                    materialDesc.pbr.emissive = glm::vec3{0.0f};
                }
                m_rayTracingMaterialDescs.emplace(entry.first, materialDesc);
            }

            //TLAS, offscreen, GBuffer
            auto rayGenDescriptorSetLayoutPtr = DescriptorSetLayout::Builder(m_device).
                    addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR).
                    addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
                    addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
                    addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
                    addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
                    build();
            auto rayGenDescriptorSet = std::make_shared<VkDescriptorSet>();
            descriptorSetLayoutPointers.push_back(rayGenDescriptorSetLayoutPtr);
            descriptorSetPointers.push_back(rayGenDescriptorSet);

            auto accelerationStructureInfo = std::make_shared<VkWriteDescriptorSetAccelerationStructureKHR>();
            accelerationStructureInfo->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            accelerationStructureInfo->accelerationStructureCount = 1;
            accelerationStructureInfo->pAccelerationStructures = &m_rayTracingSceneContext.GetTlasHandle();

            std::vector<VkDescriptorImageInfo> offscreenImageInfos;
            auto offScreenImageInfo = std::make_shared<VkDescriptorImageInfo>();
            offScreenImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offScreenImageInfo->imageView = m_renderer.getOffscreenImageColor(0)->imageView;
            offscreenImageInfos.emplace_back(*offScreenImageInfo);
            offScreenImageInfo->imageView = m_renderer.getOffscreenImageColor(1)->imageView;
            offscreenImageInfos.emplace_back(*offScreenImageInfo);

            std::vector<VkDescriptorImageInfo> worldPosImageInfos{};
            auto worldPosImageInfo = m_renderer.getWorldPosImageColor(0)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);
            worldPosImageInfo = m_renderer.getWorldPosImageColor(1)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            std::vector<VkDescriptorImageInfo> rayTracingGuideImageInfos{};
            auto rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(0)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);
            rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(1)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);

            DescriptorWriter rayGenDescriptorWriter(rayGenDescriptorSetLayoutPtr, *m_globalPool);
            if (m_rayTracingSceneContext.HasValidTlas()) {
                rayGenDescriptorWriter.writeTLAS(0, accelerationStructureInfo);
            }
            rayGenDescriptorWriter.
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImages(3, shadowTermImageInfos).
                    writeImages(4, rayTracingGuideImageInfos).
                    build(rayGenDescriptorSet);

            //ObjectDesc
            m_pEntityDescs.clear();

            const uint32_t entityDescBufferCount = static_cast<uint32_t>(std::max(
                    m_pEntityDescs.size(),
                    static_cast<size_t>(RuntimeEntityDescCapacity)));
            m_pEntityDescBuffer = std::make_shared<Buffer>(m_device, sizeof(EntityDesc), entityDescBufferCount,
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, minStorageBufferOffsetAlignment);
            m_pEntityDescBuffer->map(m_pEntityDescBuffer->getBufferSize());
            m_pEntityDescBuffer->writeToBuffer(m_pEntityDescs.data(), m_pEntityDescs.size() * sizeof(EntityDesc));
            bufferPointers.push_back(m_pEntityDescBuffer);

            //Skybox cube map
            auto skyBoxImage = std::make_shared<Image>(m_device, ImageType.CubeMap);
            skyBoxImage->createTextureImage(GetBaseTexturePath() + SkyboxCubeMapName, true);
            skyBoxImage->createImageView();
            auto skyBoxSampler = std::make_shared<Sampler>(m_device);
            skyBoxSampler->createTextureSampler();
            imagePointers.emplace_back(skyBoxImage);
            samplerPointers.emplace_back(skyBoxSampler);

            auto sceneDescriptorSetLayoutPtr = DescriptorSetLayout::Builder(m_device).
                    addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                               VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR).
                    addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR).
                    addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, imageInfos.size()).
                    addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR).
                    build();
            descriptorSetLayoutPointers.push_back(sceneDescriptorSetLayoutPtr);

            auto sceneDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(sceneDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo(globalUboBufferPtr->getBufferSize())).
                    writeBuffer(1, m_pEntityDescBuffer->descriptorInfo(m_pEntityDescBuffer->getBufferSize())).
                    writeImages(2, imageInfos).
                    writeImage(3, skyBoxImage->descriptorInfo(*skyBoxSampler)).
                    build(sceneDescriptorSet);
            descriptorSetPointers.push_back(sceneDescriptorSet);
            auto material = std::make_shared<Material>(m_device, Material::MaterialId::rayTracing, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers,
                                                       imagePointers, samplerPointers, bufferPointers, "RayTracing");
            m_materials.emplace(Material::MaterialId::rayTracing, std::move(material));

        }

        //Hybrid raster materials mirror ray tracing material IDs so MeshRendererComponent can draw a raster scene color base.
        {
            auto hybridRasterDescriptorSetLayoutPtr =
                    DescriptorSetLayout::Builder(m_device).
                            addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT).
                            build();

            auto hybridRasterDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(hybridRasterDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo()).
                    build(hybridRasterDescriptorSet);

            for (const auto &materialDescEntry: m_rayTracingMaterialDescs) {
                const auto materialId = materialDescEntry.first;
                if (m_materials.find(materialId) != m_materials.end()) {
                    continue;
                }

                std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                        shaderLibrary.LoadStage("Hybrid/HybridRaster.vert.spv", ShaderCategory::vertex),
                        shaderLibrary.LoadStage("Hybrid/HybridRaster.frag.spv", ShaderCategory::fragment)};
                std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{hybridRasterDescriptorSetLayoutPtr};
                std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{hybridRasterDescriptorSet};
                std::vector<std::shared_ptr<Image>> imagePointers{};
                std::vector<std::shared_ptr<Sampler>> samplerPointers{};
                std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

                auto hybridRasterMaterial = std::make_shared<Material>(m_device,
                                                                        materialId,
                                                                        shaderModulePointers,
                                                                        descriptorSetLayoutPointers,
                                                                        descriptorSetPointers,
                                                                        imagePointers,
                                                                        samplerPointers,
                                                                        bufferPointers,
                                                                        PipelineCategory.Opaque);
                m_materials.emplace(materialId, std::move(hybridRasterMaterial));
            }
        }
        //Post
        {
            auto postSystemDescriptorSetLayoutPtr =
                    DescriptorSetLayout::Builder(m_device).
                            addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT).
                            addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2).
                            addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2).
                            addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2).
                            build();


            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto offScreenPostImageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);
            offScreenPostImageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);

            std::vector<VkDescriptorImageInfo> sceneColorImageInfos{};
            auto sceneColorImageInfo = m_renderer.getSceneColorImageColor(0)->descriptorInfo();
            sceneColorImageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneColorImageInfos.emplace_back(*sceneColorImageInfo);
            sceneColorImageInfo = m_renderer.getSceneColorImageColor(1)->descriptorInfo();
            sceneColorImageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneColorImageInfos.emplace_back(*sceneColorImageInfo);

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            auto postSystemDescriptorSet = std::make_shared<VkDescriptorSet>();
            auto postDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(postSystemDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, sceneColorImageInfos).
                    writeImages(3, shadowTermImageInfos).
                    build(postDescriptorSet);

            std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                    shaderLibrary.LoadStage(PostVertexShaderName, ShaderCategory::vertex),
                    shaderLibrary.LoadStage(PostFragmentShaderName, ShaderCategory::fragment),
            };
            std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{postSystemDescriptorSetLayoutPtr};
            std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{postDescriptorSet};
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

            auto postMaterial = std::make_shared<Material>(m_device, Material::MaterialId::post, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
                                                           bufferPointers,
                                                           PipelineCategory.Post);
            m_materials.emplace(Material::MaterialId::post, std::move(postMaterial));
        }

        //Compute
        {
            auto computeSystemDescriptorSetLayoutPtr =
                    DescriptorSetLayout::Builder(m_device).
                            addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT).
                            addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2).
                            addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2).
                            addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT).
                            addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2).
                            addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2).
                            addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2).
                            build();

            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto offScreenPostImageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);
            offScreenPostImageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);

            std::vector<VkDescriptorImageInfo> worldPosImageInfos{};
            auto worldPosImageInfo = m_renderer.getWorldPosImageColor(0)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);
            worldPosImageInfo = m_renderer.getWorldPosImageColor(1)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);

            auto denoisingImageInfo = m_renderer.getDenoisingAccumulationImageColor()->descriptorInfo();
            denoisingImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            std::vector<VkDescriptorImageInfo> shadowMomentsImageInfos{};
            auto shadowMomentsImageInfo = m_renderer.getShadowMomentsImageColor(0)->descriptorInfo();
            shadowMomentsImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowMomentsImageInfos.emplace_back(*shadowMomentsImageInfo);
            shadowMomentsImageInfo = m_renderer.getShadowMomentsImageColor(1)->descriptorInfo();
            shadowMomentsImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowMomentsImageInfos.emplace_back(*shadowMomentsImageInfo);

            std::vector<VkDescriptorImageInfo> rayTracingGuideImageInfos{};
            auto rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(0)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);
            rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(1)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);

            auto postDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(computeSystemDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImage(3, denoisingImageInfo).
                    writeImages(4, shadowTermImageInfos).
                    writeImages(5, shadowMomentsImageInfos).
                    writeImages(6, rayTracingGuideImageInfos).
                    build(postDescriptorSet);

            std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                    shaderLibrary.LoadStage(RayTracingDenoiseComputeShaderName, ShaderCategory::compute),
            };
            std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{computeSystemDescriptorSetLayoutPtr};
            std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{postDescriptorSet};
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

            auto computeMaterial = std::make_shared<Material>(m_device, Material::MaterialId::compute, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
                                                              bufferPointers,
                                                              PipelineCategory.Compute);
            m_materials.emplace(Material::MaterialId::compute, std::move(computeMaterial));
        }
#else
            auto bufferInfo = globalUboBufferPtr->descriptorInfo();

            auto globalDescriptorSetLayoutPointer = DescriptorSetLayout::Builder(m_device).
                    addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS).
                    addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL_GRAPHICS).
                    build();

            std::shared_ptr<VkDescriptorSet> globalDescriptorSetPointer = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(globalDescriptorSetLayoutPointer, *m_globalPool).
                    writeBuffer(0, bufferInfo).
                    writeImage(1, m_renderer.getShadowImageInfo()).
                    build(globalDescriptorSetPointer);

            if (materialsDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < materialsDocument.Size(); i++) {
                    const rapidjson::Value &object = materialsDocument[i];

                    const int id = object["id"].GetInt();
                    const std::string pipelineCategoryString = object["pipelineCategory"].GetString();

                    const std::string vertexShaderName = object["vertexShader"].GetString();
                    const std::string fragmentShaderName = object["fragmentShader"].GetString();

                    std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                            shaderLibrary.LoadStage(vertexShaderName, ShaderCategory::vertex),
                            shaderLibrary.LoadStage(fragmentShaderName, ShaderCategory::fragment)};

                    const bool tessEnabled = object.HasMember("tessellationControlShader");
                    if (tessEnabled) {
                        const std::string tessellationControlShaderName = object["tessellationControlShader"].GetString();
                        const std::string tessellationEvaluationShaderName = object["tessellationEvaluationShader"].GetString();
                        shaderModulePointers.emplace_back(
                                shaderLibrary.LoadStage(tessellationControlShaderName, ShaderCategory::tessellationControl));
                        shaderModulePointers.emplace_back(
                                shaderLibrary.LoadStage(tessellationEvaluationShaderName, ShaderCategory::tessellationEvaluation));
                    }

                    const bool geomEnabled = object.HasMember("geometryShader");
                    if (geomEnabled) {
                        const std::string geometryShaderName = object["geometryShader"].GetString();
                        shaderModulePointers.emplace_back(
                                shaderLibrary.LoadStage(geometryShaderName, ShaderCategory::geometry));
                    }

                    auto textureNames = object["texture"].GetArray();

                    std::vector<std::shared_ptr<Image>> imagePointers{};
                    std::vector<std::shared_ptr<Sampler>> samplerPointers{};
                    std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{globalDescriptorSetPointer};
                    std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{globalDescriptorSetLayoutPointer};

                    //Bind Descriptors
                    //binding points: textures, uniform buffers
                    DescriptorSetLayout::Builder descriptorSetLayoutBuilder(m_device);
                    int layoutBindingPoint = 0;
                    for (auto &textureNameGenericValue: textureNames) {
                        descriptorSetLayoutBuilder.addBinding(layoutBindingPoint++,
                                                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                              VK_SHADER_STAGE_ALL_GRAPHICS);
                    }

                    if (pipelineCategoryString == PipelineCategory.Shadow) {
                        descriptorSetLayoutBuilder.addBinding(layoutBindingPoint++,
                                                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                                                          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              VK_SHADER_STAGE_ALL_GRAPHICS);
                    } else if (pipelineCategoryString == PipelineCategory.Overlay ||
                               pipelineCategoryString == PipelineCategory.Opaque ||
                               pipelineCategoryString == PipelineCategory.TessellationGeometry ||
                               pipelineCategoryString == PipelineCategory.SkyBox) {
                        descriptorSetLayoutBuilder.addBinding(layoutBindingPoint++,
                                                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                              VK_SHADER_STAGE_ALL_GRAPHICS);
                    }

                    auto materialDescriptorSetLayoutPointer = descriptorSetLayoutBuilder.build();

                    DescriptorWriter descriptorWriter(materialDescriptorSetLayoutPointer, *m_globalPool);

                    //Write Descriptors
                    int writerBindingPoint = 0;
                    std::vector<std::shared_ptr<VkDescriptorImageInfo>> imageInfos;
                    for (auto &textureNameGenericValue: textureNames) {
                        std::string textureName = textureNameGenericValue.GetString();
                        bool isCubeMap = pipelineCategoryString == PipelineCategory.SkyBox;
                        auto texture = GetOrCreateTexture(textureName, isCubeMap);

                        auto imageInfo = texture.image->descriptorInfo(*texture.sampler);
                        imageInfos.emplace_back(imageInfo);
                        descriptorWriter.writeImage(writerBindingPoint++, imageInfo);
                        imagePointers.emplace_back(texture.image);
                        samplerPointers.emplace_back(texture.sampler);
                    }

                    std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};
                    if (pipelineCategoryString == "Shadow") {
                        auto shadowUboBuffer = std::make_shared<Buffer>(m_device, sizeof(ShadowUbo),
                                                                        SwapChain::MAX_FRAMES_IN_FLIGHT,
                                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                                                        minUniformOffsetAlignment);
                        bufferPointers.push_back(shadowUboBuffer);
                        auto shadowUboBufferInfo = shadowUboBuffer->descriptorInfo(sizeof(ShadowUbo) * SwapChain::MAX_FRAMES_IN_FLIGHT);
                        descriptorWriter.writeBuffer(writerBindingPoint++, shadowUboBufferInfo);
                    } else if (pipelineCategoryString == PipelineCategory.Overlay ||
                               pipelineCategoryString == PipelineCategory.Opaque ||
                               pipelineCategoryString == PipelineCategory.TessellationGeometry ||
                               pipelineCategoryString == PipelineCategory.SkyBox) {
                        imagePointers.push_back(m_renderer.getShadowImage());
                        samplerPointers.push_back(m_renderer.getShadowSampler());
                        auto imageInfo = m_renderer.getShadowImageInfo();
                        imageInfos.emplace_back(imageInfo);
                        descriptorWriter.writeImage(writerBindingPoint++, imageInfo);
                    }

                    std::shared_ptr<VkDescriptorSet> materialDescriptorSetPointer = std::make_shared<VkDescriptorSet>();
                    descriptorWriter.build(materialDescriptorSetPointer);

                    descriptorSetLayoutPointers.push_back(materialDescriptorSetLayoutPointer);
                    descriptorSetPointers.push_back(materialDescriptorSetPointer);


                    auto m_material = std::make_shared<Material>(m_device, id, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers,
                                                                 imagePointers, samplerPointers, bufferPointers, pipelineCategoryString);

                    m_materials.emplace(id, std::move(m_material));
                }
            }
#endif
            //Gizmos
            {
                auto uiDescriptorSetLayoutPtr = DescriptorSetLayout::Builder(m_device).
                        addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS).
                        build();

                auto uiDescriptorSet = std::make_shared<VkDescriptorSet>();
                DescriptorWriter(uiDescriptorSetLayoutPtr, *m_globalPool).
                        writeBuffer(0, globalUboBufferPtr->descriptorInfo(globalUboBufferPtr->getBufferSize())).
                        build(uiDescriptorSet);

                std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{};
                std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{uiDescriptorSetLayoutPtr};
                std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{uiDescriptorSet};
                std::vector<std::shared_ptr<Image>> imagePointers{};
                std::vector<std::shared_ptr<Sampler>> samplerPointers{};
                std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

                auto uiMaterial = std::make_shared<Material>(m_device, Material::MaterialId::gizmos, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
                                                             bufferPointers,
                                                             PipelineCategory.Gizmos);
                m_materials.emplace(Material::MaterialId::gizmos, std::move(uiMaterial));
            }
            RegisterStaticRenderCoreResources();
            m_renderCoreStaticResourcesRegistered = true;
            m_renderCoreRenderTargetsDirty = true;
        }
        struct TextureCacheEntry {
            std::shared_ptr<Image> image;
            std::shared_ptr<Sampler> sampler;
            RenderCore::RenderResourceHandle textureHandle{};
        };

        TextureCacheEntry GetOrCreateTexture(const std::string &textureName, bool isCubeMap = false, bool srgb = false) {
            const std::string key = (isCubeMap ? "CubeMap:" : "Default:") + std::string(srgb ? "SRGB:" : "Linear:") + textureName;
            auto entry = m_textureCache.find(key);
            if (entry != m_textureCache.end()) {
                if (m_renderCoreStaticResourcesRegistered && !IsTextureResourceCurrent(entry->second)) {
                    entry->second.textureHandle = RegisterTextureResource(key, entry->second);
                }
                return entry->second;
            }

            auto image = std::make_shared<Image>(m_device, isCubeMap ? ImageType.CubeMap : ImageType.Default);
            image->createTextureImage(GetBaseTexturePath() + textureName, srgb);
            image->createImageView();

            auto sampler = std::make_shared<Sampler>(m_device);
            sampler->createTextureSampler();

            TextureCacheEntry cacheEntry{image, sampler};
            if (m_renderCoreStaticResourcesRegistered) {
                cacheEntry.textureHandle = RegisterTextureResource(key, cacheEntry);
            }
            m_textureCache.emplace(key, cacheEntry);
            return cacheEntry;
        }
    private:
        void InvalidateRenderCoreRegistry() {
            m_renderCore.GetResourceRegistry().Clear();
            m_modelRepository.SetRenderResourceRegistry(&m_renderCore.GetResourceRegistry());
            m_renderCoreStaticResourcesRegistered = false;
            m_renderCoreRenderTargetsDirty = true;
            MarkSceneSizedImageDescriptorsDirty();
            m_materialResourceHandles.clear();
            for (auto &[textureKey, textureEntry]: m_textureCache) {
                (void) textureKey;
                textureEntry.textureHandle = {};
            }
        }

        void MarkPostDescriptorsDirty() {
            m_postDescriptorDirty = true;
        }

        void MarkSceneSizedImageDescriptorsDirty() {
            MarkPostDescriptorsDirty();
#ifdef RAY_TRACING
            MarkComputeDescriptorsDirty();
            MarkRayTracingRayGenDescriptorsDirty();
#endif
        }

#ifdef RAY_TRACING
        void MarkComputeDescriptorsDirty() {
            m_computeDescriptorDirty = true;
        }

        void MarkRayTracingRayGenDescriptorsDirty() {
            m_rayTracingRayGenDescriptorDirty = true;
        }
#endif

        static RHI::Extent2D ToRhiExtent(const VkExtent2D extent) {
            return {extent.width, extent.height};
        }

        std::string MakeMaterialResourceName(Material::id_t materialId) const {
            return "Material/" + std::to_string(materialId);
        }

        std::string MakeMaterialInstanceResourceName(id_t entityId) const {
            return "MaterialInstance/Entity/" + std::to_string(entityId);
        }

        std::optional<PBR> FindDefaultPbrForMaterial(Material::id_t materialId) const {
#ifdef RAY_TRACING
            const auto rtMaterial = m_rayTracingMaterialDescs.find(materialId);
            if (rtMaterial != m_rayTracingMaterialDescs.end()) {
                return rtMaterial->second.pbr;
            }
#endif
            return std::nullopt;
        }

        static bool AreVec3Equal(const glm::vec3 &lhs, const glm::vec3 &rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        static bool ArePbrEqual(const PBR &lhs, const PBR &rhs) {
            return AreVec3Equal(lhs.albedo, rhs.albedo) &&
                   AreVec3Equal(lhs.normal, rhs.normal) &&
                   lhs.metallic == rhs.metallic &&
                   lhs.roughness == rhs.roughness &&
                   lhs.opacity == rhs.opacity &&
                   lhs.AO == rhs.AO &&
                   AreVec3Equal(lhs.emissive, rhs.emissive);
        }

        static bool MaterialInstanceMatches(
            const RenderCore::MaterialInstance *materialInstance,
            RenderCore::RenderResourceHandle materialHandle,
            id_t ownerEntityId,
            const std::optional<PBR> &pbrOverride) {
            if (materialInstance == nullptr) {
                return false;
            }

            if (materialInstance->material != materialHandle ||
                materialInstance->ownerEntityId != ownerEntityId ||
                materialInstance->hasPbrOverride != pbrOverride.has_value()) {
                return false;
            }

            if (!pbrOverride.has_value()) {
                return true;
            }

            return ArePbrEqual(materialInstance->pbrOverride, *pbrOverride);
        }

        bool IsTextureResourceCurrent(const TextureCacheEntry &textureEntry) const {
            if (!textureEntry.textureHandle.IsValid()) {
                return false;
            }

            const auto *textureResource = m_renderCore.GetResourceRegistry().GetTexture(textureEntry.textureHandle);
            return textureResource != nullptr &&
                   textureResource->texture == textureEntry.image &&
                   textureResource->view == textureEntry.image &&
                   textureResource->sampler == textureEntry.sampler;
        }

        RenderCore::RenderResourceHandle RegisterTextureResource(const std::string &textureKey, TextureCacheEntry &textureEntry) {
            if (textureEntry.image == nullptr) {
                return {};
            }

            if (IsTextureResourceCurrent(textureEntry)) {
                return textureEntry.textureHandle;
            }

            textureEntry.textureHandle = m_renderCore.GetResourceRegistry().ImportTexture(
                "Texture/" + textureKey,
                textureEntry.image,
                textureEntry.image,
                textureEntry.sampler);
            return textureEntry.textureHandle;
        }

        RenderCore::RenderResourceHandle GetOrCreateMaterialResourceHandle(Material::id_t materialId) {
            auto &registry = m_renderCore.GetResourceRegistry();
            const auto materialEntry = m_materials.find(materialId);
            if (materialEntry == m_materials.end() || materialEntry->second == nullptr) {
                m_materialResourceHandles.erase(materialId);
                return {};
            }

            auto handleEntry = m_materialResourceHandles.find(materialId);
            if (handleEntry != m_materialResourceHandles.end()) {
                const auto *materialResource = registry.GetMaterial(handleEntry->second);
                if (materialResource != nullptr &&
                    materialResource->legacyMaterialId == materialId &&
                    materialResource->legacyMaterial == materialEntry->second) {
                    return handleEntry->second;
                }
            }

            auto materialHandle = registry.ImportMaterial(
                MakeMaterialResourceName(materialId),
                materialEntry->second,
                FindDefaultPbrForMaterial(materialId));
            m_materialResourceHandles[materialId] = materialHandle;
            return materialHandle;
        }

        bool IsMeshResourceCurrent(const RenderCore::MeshResource *meshResource,
                                   const std::shared_ptr<Model> &model) const {
            return meshResource != nullptr && meshResource->legacyModel == model;
        }

        void RegisterStaticRenderCoreResources() {
            for (auto &[textureKey, textureEntry]: m_textureCache) {
                RegisterTextureResource(textureKey, textureEntry);
            }

            for (auto &[materialId, material]: m_materials) {
                (void) material;
                GetOrCreateMaterialResourceHandle(materialId);
            }
        }

        void RegisterRendererRenderTargets() {
            auto &registry = m_renderCore.GetResourceRegistry();
            const auto sceneExtent = m_renderer.getSceneRenderExtent();
            const RHI::Extent2D extent = ToRhiExtent(sceneExtent);

            for (int index = 0; index < 2; ++index) {
                std::vector<RenderCore::RenderResourceHandle> sceneColorAttachments{
                    registry.ImportTexture(
                        "Renderer/SceneColor/" + std::to_string(index),
                        m_renderer.getSceneColorImageColor(index),
                        m_renderer.getSceneColorImageColor(index))};
                registry.ImportRenderTarget(
                    "Renderer/RenderTarget/SceneColor/" + std::to_string(index),
                    extent,
                    std::move(sceneColorAttachments));

#ifdef RAY_TRACING
                registry.ImportTexture(
                    "Renderer/RayTracingOutput/" + std::to_string(index),
                    m_renderer.getOffscreenImageColor(index),
                    m_renderer.getOffscreenImageColor(index));
                registry.ImportTexture(
                    "Renderer/WorldPosition/" + std::to_string(index),
                    m_renderer.getWorldPosImageColor(index),
                    m_renderer.getWorldPosImageColor(index));
                registry.ImportTexture(
                    "Renderer/ShadowTerm/" + std::to_string(index),
                    m_renderer.getShadowTermImageColor(index),
                    m_renderer.getShadowTermImageColor(index));
                registry.ImportTexture(
                    "Renderer/ShadowMoments/" + std::to_string(index),
                    m_renderer.getShadowMomentsImageColor(index),
                    m_renderer.getShadowMomentsImageColor(index));
                registry.ImportTexture(
                    "Renderer/RayTracingGuide/" + std::to_string(index),
                    m_renderer.getRayTracingGuideImageColor(index),
                    m_renderer.getRayTracingGuideImageColor(index));
#endif
            }

#ifdef RAY_TRACING
            registry.ImportTexture(
                "Renderer/DenoisingAccumulation",
                m_renderer.getDenoisingAccumulationImageColor(),
                m_renderer.getDenoisingAccumulationImageColor());
#else
            registry.ImportTexture(
                "Renderer/ShadowMap",
                m_renderer.getShadowImage(),
                m_renderer.getShadowImage(),
                m_renderer.getShadowSampler());
#endif
        }

        void SyncMeshRendererRenderCoreResources() {
            if (!m_meshRendererRenderResourcesFullRebuildDirty &&
                m_dirtyMeshRendererRenderResourceEntities.empty()) {
                return;
            }

            auto &registry = m_renderCore.GetResourceRegistry();
            std::vector<id_t> entitiesToSync{};
            if (m_meshRendererRenderResourcesFullRebuildDirty) {
                const auto meshEntities = m_sceneRegistry.View<MeshRendererComponent>();
                entitiesToSync.reserve(meshEntities.size());
                for (const auto entityId: meshEntities) {
                    entitiesToSync.push_back(entityId);
                }
            } else {
                entitiesToSync.reserve(m_dirtyMeshRendererRenderResourceEntities.size());
                for (const auto entityId: m_dirtyMeshRendererRenderResourceEntities) {
                    entitiesToSync.push_back(entityId);
                }
            }

            for (const auto entityId: entitiesToSync) {
                auto *meshRenderer = TryGetSceneComponent<MeshRendererComponent>(entityId);
                if (meshRenderer == nullptr) {
                    continue;
                }

                if (m_entityCommandService.IsPendingDestroy(entityId)) {
                    if (meshRenderer->GetMaterialInstanceHandle().IsValid()) {
                        registry.Destroy(meshRenderer->GetMaterialInstanceHandle());
                        meshRenderer->SetMaterialInstanceHandle({});
                    }
                    continue;
                }

                if (auto model = meshRenderer->GetModelPtr(); model != nullptr) {
                    auto meshHandle = meshRenderer->GetMeshResourceHandle();
                    const auto *meshResource = registry.GetMesh(meshHandle);
                    if (!IsMeshResourceCurrent(meshResource, model)) {
                        meshHandle = m_modelRepository.FindMeshResource(model->GetName());
                    }
                    if (!meshHandle.IsValid()) {
                        meshHandle = registry.ImportMesh("Mesh/" + model->GetName(), model, {}, model->GetName());
                    }
                    meshRenderer->SetMeshResourceHandle(meshHandle);
                } else if (meshRenderer->GetMeshResourceHandle().IsValid()) {
                    meshRenderer->SetMeshResourceHandle({});
                }

                auto materialHandle = meshRenderer->GetMaterialResourceHandle();
                const auto *materialResource = registry.GetMaterial(materialHandle);
                const auto materialEntry = m_materials.find(meshRenderer->GetMaterialID());
                const bool materialHandleCurrent =
                    materialEntry != m_materials.end() &&
                    materialEntry->second != nullptr &&
                    materialResource != nullptr &&
                    materialResource->legacyMaterialId == meshRenderer->GetMaterialID() &&
                    materialResource->legacyMaterial == materialEntry->second;
                if (!materialHandleCurrent) {
                    materialHandle = GetOrCreateMaterialResourceHandle(meshRenderer->GetMaterialID());
                }

                meshRenderer->SetMaterialResourceHandle(materialHandle);
                if (materialHandle.IsValid()) {
                    auto materialInstanceHandle = meshRenderer->GetMaterialInstanceHandle();
                    const auto pbrOverride = meshRenderer->GetPbrOverride();
                    const auto *materialInstance = registry.GetMaterialInstance(materialInstanceHandle);
                    if (!MaterialInstanceMatches(materialInstance, materialHandle, entityId, pbrOverride)) {
                        materialInstanceHandle = registry.ImportMaterialInstance(
                            MakeMaterialInstanceResourceName(entityId),
                            materialHandle,
                            entityId,
                            pbrOverride);
                    }
                    meshRenderer->SetMaterialInstanceHandle(materialInstanceHandle);
                } else if (meshRenderer->GetMaterialInstanceHandle().IsValid()) {
                    registry.Destroy(meshRenderer->GetMaterialInstanceHandle());
                    meshRenderer->SetMaterialInstanceHandle({});
                }
            }

            m_dirtyMeshRendererRenderResourceEntities.clear();
            m_meshRendererRenderResourcesFullRebuildDirty = false;
        }

        bool RefreshPostDescriptorSet() {
            auto materialIt = m_materials.find(Material::MaterialId::post);
            if (materialIt == m_materials.end() || materialIt->second == nullptr) {
                return false;
            }

            const auto descriptorSetLayouts = materialIt->second->getDescriptorSetLayoutPointers();
            const auto descriptorSets = materialIt->second->getDescriptorSetPointers();
            const auto &bufferPointers = materialIt->second->getBufferPointers();
            if (descriptorSetLayouts.empty() || descriptorSets.empty() ||
                descriptorSetLayouts[0] == nullptr || descriptorSets[0] == nullptr ||
                bufferPointers.empty() || bufferPointers[0] == nullptr) {
                return false;
            }

            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto imageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*imageInfo);
            imageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*imageInfo);

            std::vector<VkDescriptorImageInfo> sceneColorImageInfos{};
            auto sceneColorImageInfo = m_renderer.getSceneColorImageColor(0)->descriptorInfo();
            sceneColorImageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneColorImageInfos.emplace_back(*sceneColorImageInfo);
            sceneColorImageInfo = m_renderer.getSceneColorImageColor(1)->descriptorInfo();
            sceneColorImageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneColorImageInfos.emplace_back(*sceneColorImageInfo);

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeBuffer(0, bufferPointers[0]->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, sceneColorImageInfos).
                    writeImages(3, shadowTermImageInfos).
                    overwrite(*descriptorSets[0]);
            return true;
        }

#ifdef RAY_TRACING
        bool RefreshRayTracingRayGenDescriptorSet() {
            auto materialIt = m_materials.find(Material::MaterialId::rayTracing);
            if (materialIt == m_materials.end() || materialIt->second == nullptr || !m_rayTracingSceneContext.HasValidTlas()) {
                return false;
            }

            const auto descriptorSetLayouts = materialIt->second->getDescriptorSetLayoutPointers();
            const auto descriptorSets = materialIt->second->getDescriptorSetPointers();
            if (descriptorSetLayouts.empty() || descriptorSets.empty() ||
                descriptorSetLayouts[0] == nullptr || descriptorSets[0] == nullptr) {
                return false;
            }

            auto accelerationStructureInfo = std::make_shared<VkWriteDescriptorSetAccelerationStructureKHR>();
            accelerationStructureInfo->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            accelerationStructureInfo->accelerationStructureCount = 1;
            accelerationStructureInfo->pAccelerationStructures = &m_rayTracingSceneContext.GetTlasHandle();

            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto offscreenImageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            offscreenImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offscreenImageInfo);
            offscreenImageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            offscreenImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offscreenImageInfo);

            std::vector<VkDescriptorImageInfo> worldPosImageInfos{};
            auto worldPosImageInfo = m_renderer.getWorldPosImageColor(0)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);
            worldPosImageInfo = m_renderer.getWorldPosImageColor(1)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            std::vector<VkDescriptorImageInfo> rayTracingGuideImageInfos{};
            auto rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(0)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);
            rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(1)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeTLAS(0, accelerationStructureInfo).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImages(3, shadowTermImageInfos).
                    writeImages(4, rayTracingGuideImageInfos).
                    overwrite(*descriptorSets[0]);
            return true;
        }

        bool RefreshComputeDescriptorSet() {
            auto materialIt = m_materials.find(Material::MaterialId::compute);
            if (materialIt == m_materials.end() || materialIt->second == nullptr) {
                return false;
            }

            const auto descriptorSetLayouts = materialIt->second->getDescriptorSetLayoutPointers();
            const auto descriptorSets = materialIt->second->getDescriptorSetPointers();
            const auto &bufferPointers = materialIt->second->getBufferPointers();
            if (descriptorSetLayouts.empty() || descriptorSets.empty() ||
                descriptorSetLayouts[0] == nullptr || descriptorSets[0] == nullptr ||
                bufferPointers.empty() || bufferPointers[0] == nullptr) {
                return false;
            }

            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto offscreenImageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            offscreenImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offscreenImageInfo);
            offscreenImageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            offscreenImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offscreenImageInfo);

            std::vector<VkDescriptorImageInfo> worldPosImageInfos{};
            auto worldPosImageInfo = m_renderer.getWorldPosImageColor(0)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);
            worldPosImageInfo = m_renderer.getWorldPosImageColor(1)->descriptorInfo();
            worldPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            worldPosImageInfos.emplace_back(*worldPosImageInfo);

            auto denoisingImageInfo = m_renderer.getDenoisingAccumulationImageColor()->descriptorInfo();
            denoisingImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkDescriptorImageInfo> shadowTermImageInfos{};
            auto shadowTermImageInfo = m_renderer.getShadowTermImageColor(0)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);
            shadowTermImageInfo = m_renderer.getShadowTermImageColor(1)->descriptorInfo();
            shadowTermImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowTermImageInfos.emplace_back(*shadowTermImageInfo);

            std::vector<VkDescriptorImageInfo> shadowMomentsImageInfos{};
            auto shadowMomentsImageInfo = m_renderer.getShadowMomentsImageColor(0)->descriptorInfo();
            shadowMomentsImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowMomentsImageInfos.emplace_back(*shadowMomentsImageInfo);
            shadowMomentsImageInfo = m_renderer.getShadowMomentsImageColor(1)->descriptorInfo();
            shadowMomentsImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            shadowMomentsImageInfos.emplace_back(*shadowMomentsImageInfo);

            std::vector<VkDescriptorImageInfo> rayTracingGuideImageInfos{};
            auto rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(0)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);
            rayTracingGuideImageInfo = m_renderer.getRayTracingGuideImageColor(1)->descriptorInfo();
            rayTracingGuideImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            rayTracingGuideImageInfos.emplace_back(*rayTracingGuideImageInfo);

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeBuffer(0, bufferPointers[0]->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImage(3, denoisingImageInfo).
                    writeImages(4, shadowTermImageInfos).
                    writeImages(5, shadowMomentsImageInfos).
                    writeImages(6, rayTracingGuideImageInfos).
                    overwrite(*descriptorSets[0]);
            return true;
        }

#endif

        template<typename T>
        T *TryGetSceneComponent(const id_t entityId) {
            T *component = nullptr;
            return m_sceneRegistry.TryGetComponent(entityId, component) ? component : nullptr;
        }

        MyWindow m_window{SCENE_WIDTH + UI_LEFT_WIDTH + UI_LEFT_WIDTH_2, SCENE_HEIGHT, "FeatherVK"};
        InputState m_inputState{};
        Device m_device{m_window};
        Renderer m_renderer{m_window, m_device};
        RenderCore::CoreServices m_renderCore{m_device};
        std::shared_ptr<DescriptorPool> m_globalPool;
        ModelRepository m_modelRepository;
        SceneComponentLoader m_sceneComponentLoader;
        ECS::SceneRegistry m_sceneRegistry;
        HierarchyService m_hierarchyService;
        EntityCommandService m_entityCommandService;
        EditorSelectionService m_editorSelectionService;
        TransformService m_transformService;
        Material::Map m_materials;
        std::unordered_map<std::string, TextureCacheEntry> m_textureCache;
        std::unordered_map<Material::id_t, RenderCore::RenderResourceHandle> m_materialResourceHandles;
        bool m_sceneDirty = false;
        bool m_sceneSaveRequested = false;
        bool m_renderSceneDirty = true;
        bool m_renderCoreStaticResourcesRegistered = false;
        bool m_renderCoreRenderTargetsDirty = true;
        bool m_meshRendererRenderResourcesFullRebuildDirty = true;
        std::unordered_set<id_t> m_dirtyMeshRendererRenderResourceEntities{};
        bool m_postDescriptorDirty = false;
#ifdef RAY_TRACING
        bool m_computeDescriptorDirty = false;
        bool m_rayTracingRayGenDescriptorDirty = false;
#endif

#ifdef RAY_TRACING
        RayTracingSceneContext m_rayTracingSceneContext;
        std::shared_ptr<Buffer> m_pEntityDescBuffer;
        std::vector<EntityDesc> m_pEntityDescs;
        std::unordered_map<Material::id_t, EntityDesc> m_rayTracingMaterialDescs;
        std::unordered_map<Material::id_t, uint32_t> m_rayTracingShaderOffsets;
#endif
    };
}













