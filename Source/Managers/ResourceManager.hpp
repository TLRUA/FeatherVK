#pragma once

#include <limits>
#include <numeric>
#include <optional>
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
#include "../Components/RayTracingInstanceComponent.hpp"
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

    class ResourceManager {
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

        bool SyncSceneViewportLayout(const ViewportRect &scenePanelRect, const ViewportRect &sceneViewportRect) {
            const bool sceneExtentChanged = m_renderer.UpdateSceneViewportLayout(scenePanelRect, sceneViewportRect);
#ifdef RAY_TRACING
            if (sceneExtentChanged) {
                RefreshSceneSizedDescriptors();
            }
#endif
            return sceneExtentChanged;
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

        bool RefreshRayTracingTlasDescriptor() {
            return RefreshRayTracingRayGenDescriptorSet();
        }
#endif

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
                if (meshRendererComponent == nullptr || m_sceneRegistry.HasComponent<RayTracingInstanceComponent>(entityId)) {
                    continue;
                }
                m_sceneRegistry.EmplaceComponent<RayTracingInstanceComponent>(entityId, m_rayTracingSceneContext.AllocateInstanceId());
            }

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
                    if (object.HasMember("rayAnyHitShader")) {
                        const std::string rayAnyHitShaderName = object["rayAnyHitShader"].GetString();
                        shaderModulePointers.push_back(shaderLibrary.LoadStage(rayAnyHitShaderName, ShaderCategory::rayAnyHit));
                    }

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

            for (const auto entityId: m_sceneRegistry.View<MeshRendererComponent, TransformComponent, RayTracingInstanceComponent>()) {
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                auto *transformComponent = TryGetSceneComponent<TransformComponent>(entityId);
                auto *rayTracingInstance = TryGetSceneComponent<RayTracingInstanceComponent>(entityId);
                if (meshRendererComponent == nullptr || transformComponent == nullptr || rayTracingInstance == nullptr) {
                    continue;
                }

                auto model = meshRendererComponent->GetModelPtr();
                if (model == nullptr) {
                    continue;
                }

                m_rayTracingSceneContext.CreateInstance(
                    *model,
                    rayTracingInstance->instanceId,
                    static_cast<id_t>(idShaderOffsetMap[meshRendererComponent->GetMaterialID()]),
                    transformComponent->mat4());
            }
            m_rayTracingSceneContext.BuildTopLevel();

            //TLAS, offscreen, GBuffer
            auto rayGenDescriptorSetLayoutPtr = DescriptorSetLayout::Builder(m_device).
                    addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR).
                    addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
                    addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2).
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

            DescriptorWriter(rayGenDescriptorSetLayoutPtr, *m_globalPool).
                    writeTLAS(0, accelerationStructureInfo).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    build(rayGenDescriptorSet);

            //ObjectDesc
            const auto meshRendererEntities = m_sceneRegistry.View<MeshRendererComponent>();
            size_t maxTlasId = 0;
            for (const auto entityId: meshRendererEntities) {
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                auto *rayTracingInstance = TryGetSceneComponent<RayTracingInstanceComponent>(entityId);
                if (meshRendererComponent == nullptr || rayTracingInstance == nullptr) {
                    continue;
                }
                maxTlasId = std::max(maxTlasId, static_cast<size_t>(rayTracingInstance->instanceId));
            }

            m_pEntityDescs.clear();
            m_pEntityDescs.resize(maxTlasId + 1);
            for (const auto entityId: meshRendererEntities) {
                EntityDesc modelDesc{};
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                auto *rayTracingInstance = TryGetSceneComponent<RayTracingInstanceComponent>(entityId);
                if (meshRendererComponent == nullptr || rayTracingInstance == nullptr || meshRendererComponent->GetModelPtr() == nullptr) {
                    continue;
                }

                modelDesc.vertexBufferAddress = meshRendererComponent->GetModelPtr()->getVertexBuffer()->getDeviceAddress();
                modelDesc.indexBufferAddress = meshRendererComponent->GetModelPtr()->getIndexBuffer()->getDeviceAddress();
                auto entry = textureEntries.find(meshRendererComponent->GetMaterialID());
                if (entry != textureEntries.end()) {
                    modelDesc.textureEntry = entry->second;
                }
                auto pbrEntry = pbrMaterials.find(meshRendererComponent->GetMaterialID());
                if (pbrEntry != pbrMaterials.end()) {
                    modelDesc.pbr = pbrEntry->second;
                } else {
                    auto baseDescEntry = m_rayTracingMaterialDescs.find(meshRendererComponent->GetMaterialID());
                    if (baseDescEntry != m_rayTracingMaterialDescs.end()) {
                        modelDesc.pbr = baseDescEntry->second.pbr;
                    }
                }
                m_pEntityDescs[rayTracingInstance->instanceId] = modelDesc;
            }

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
                    addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR).
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

        //Post
        {
            auto postSystemDescriptorSetLayoutPtr =
                    DescriptorSetLayout::Builder(m_device).
                            addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT).
                            addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2).
                            build();


            std::vector<VkDescriptorImageInfo> offscreenImageInfos{};
            auto offScreenPostImageInfo = m_renderer.getOffscreenImageColor(0)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);
            offScreenPostImageInfo = m_renderer.getOffscreenImageColor(1)->descriptorInfo();
            offScreenPostImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            offscreenImageInfos.emplace_back(*offScreenPostImageInfo);

            auto postSystemDescriptorSet = std::make_shared<VkDescriptorSet>();
            auto postDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(postSystemDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
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

            std::vector<VkDescriptorImageInfo> viewPosImageInfos{};
            auto viewPosImageInfo = m_renderer.getViewPosImageColor(0)->descriptorInfo();
            viewPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            viewPosImageInfos.emplace_back(*viewPosImageInfo);
            viewPosImageInfo = m_renderer.getViewPosImageColor(1)->descriptorInfo();
            viewPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            viewPosImageInfos.emplace_back(*viewPosImageInfo);

            auto postDescriptorSet = std::make_shared<VkDescriptorSet>();
            DescriptorWriter(computeSystemDescriptorSetLayoutPtr, *m_globalPool).
                    writeBuffer(0, globalUboBufferPtr->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImage(3, denoisingImageInfo).
                    writeImages(4, viewPosImageInfos).
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
        }
        struct TextureCacheEntry {
            std::shared_ptr<Image> image;
            std::shared_ptr<Sampler> sampler;
        };

        TextureCacheEntry GetOrCreateTexture(const std::string &textureName, bool isCubeMap = false, bool srgb = false) {
            const std::string key = (isCubeMap ? "CubeMap:" : "Default:") + std::string(srgb ? "SRGB:" : "Linear:") + textureName;
            auto entry = m_textureCache.find(key);
            if (entry != m_textureCache.end()) {
                return entry->second;
            }

            auto image = std::make_shared<Image>(m_device, isCubeMap ? ImageType.CubeMap : ImageType.Default);
            image->createTextureImage(GetBaseTexturePath() + textureName, srgb);
            image->createImageView();

            auto sampler = std::make_shared<Sampler>(m_device);
            sampler->createTextureSampler();

            TextureCacheEntry cacheEntry{image, sampler};
            m_textureCache.emplace(key, cacheEntry);
            return cacheEntry;
        }
    private:
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

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeBuffer(0, bufferPointers[0]->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    overwrite(*descriptorSets[0]);
            return true;
        }

#ifdef RAY_TRACING
        bool RefreshRayTracingRayGenDescriptorSet() {
            auto materialIt = m_materials.find(Material::MaterialId::rayTracing);
            if (materialIt == m_materials.end() || materialIt->second == nullptr) {
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

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeTLAS(0, accelerationStructureInfo).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
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

            std::vector<VkDescriptorImageInfo> viewPosImageInfos{};
            auto viewPosImageInfo = m_renderer.getViewPosImageColor(0)->descriptorInfo();
            viewPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            viewPosImageInfos.emplace_back(*viewPosImageInfo);
            viewPosImageInfo = m_renderer.getViewPosImageColor(1)->descriptorInfo();
            viewPosImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            viewPosImageInfos.emplace_back(*viewPosImageInfo);

            DescriptorWriter(descriptorSetLayouts[0], *m_globalPool).
                    writeBuffer(0, bufferPointers[0]->descriptorInfo()).
                    writeImages(1, offscreenImageInfos).
                    writeImages(2, worldPosImageInfos).
                    writeImage(3, denoisingImageInfo).
                    writeImages(4, viewPosImageInfos).
                    overwrite(*descriptorSets[0]);
            return true;
        }

        void RefreshSceneSizedDescriptors() {
            RefreshRayTracingRayGenDescriptorSet();
            RefreshPostDescriptorSet();
            RefreshComputeDescriptorSet();
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

#ifdef RAY_TRACING
        RayTracingSceneContext m_rayTracingSceneContext;
        std::shared_ptr<Buffer> m_pEntityDescBuffer;
        std::vector<EntityDesc> m_pEntityDescs;
        std::unordered_map<Material::id_t, EntityDesc> m_rayTracingMaterialDescs;
        std::unordered_map<Material::id_t, uint32_t> m_rayTracingShaderOffsets;
#endif
    };
}













