#pragma once

#include <numeric>
#include "../ComponentFactory.hpp"
#include "../Descriptor.h"
#include "../Device.hpp"
#include "../GUI.hpp"
#include "../GameObject.hpp"
#include "../Image.h"
#include "../Material.hpp"
#include "../MyWindow.hpp"
#include "../Pipeline.hpp"
#include "../Renderer.h"
#include "../Sampler.h"
#include "../ShaderBuilder.h"
#include "../Utils/JsonUtils.hpp"

#include "../Utils/ProjectPaths.hpp"
#include "../ECS/SceneRegistry.hpp"
#ifdef RAY_TRACING
#include "../RayTracing/BLAS.hpp"
#include "../RayTracing/TLAS.hpp"
#endif

namespace Kaamoo {
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
    inline const static std::string GameObjectsFileName = "GameObjects.json";
    inline const static std::string MaterialsFileName = "Materials.json";
    inline const static std::string ComponentsFileName = "Components.json";
    inline const static std::string SkyboxCubeMapName = "Cubemap";

    const int MATERIAL_NUMBER = 16;

    class ResourceManager {
    public:
        ResourceManager() {
            m_globalPool = DescriptorPool::Builder(m_device).
                    setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).
                    addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * MATERIAL_NUMBER).build();
            loadGameObjects();
            loadMaterials();
            GUI::Init(m_renderer, m_window);
        }

        ~ResourceManager() {
            m_materials.clear();
        }


        Device &GetDevice() { return m_device; }

        MyWindow &GetWindow() { return m_window; }

        HierarchyTree &GetHierarchyTree() { return m_hierarchyTree; }

        Material::Map &GetMaterials() { return m_materials; }


        ECS::SceneRegistry &GetSceneRegistry() { return m_sceneRegistry; }
        const ECS::SceneRegistry &GetSceneRegistry() const { return m_sceneRegistry; }

        Renderer &GetRenderer() { return m_renderer; }

#ifdef RAY_TRACING
        std::shared_ptr<Buffer>& GetGameObjectDescBuffer() { return m_pGameObjectDescBuffer; }
        std::vector<GameObjectDesc>& GetGameObjectDescs() { return m_pGameObjectDescs; }
#endif

        void loadGameObjects() {
            struct HierarchyEntry {
                id_t entityId;
                int32_t transformId;
            };

            m_sceneRegistry.Clear();
            m_ownedComponents.clear();
            m_hierarchyTree = HierarchyTree();

            std::string gameObjectsJsonString = JsonUtils::ReadJsonFile(GetBasePath() + GameObjectsFileName);
            std::string componentsJsonString = JsonUtils::ReadJsonFile(GetBasePath() + ComponentsFileName);

            rapidjson::Document gameObjectsDocument;
            rapidjson::Document componentsDocument;
            gameObjectsDocument.Parse(gameObjectsJsonString.c_str());
            componentsDocument.Parse(componentsJsonString.c_str());

            std::unordered_map<int, rapidjson::Value> componentsMap;
            if (componentsDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < componentsDocument.Size(); i++) {
                    rapidjson::Value componentValue = std::move(componentsDocument[i]);
                    componentsMap[componentValue["id"].GetInt()] = componentValue;
                }
            }

            std::unordered_map<int, id_t> childTransformIdToParentEntityMap;
            std::vector<HierarchyEntry> hierarchyEntries{};
            ComponentFactory componentFactory;

            if (gameObjectsDocument.IsArray()) {
                for (rapidjson::SizeType i = 0; i < gameObjectsDocument.Size(); i++) {
                    const rapidjson::Value &object = gameObjectsDocument[i];

                    const std::string entityName = object.HasMember("name") ? object["name"].GetString() : "Entity";
                    const bool active = !object.HasMember("IsActive") || object["IsActive"].GetBool();

                    const id_t entityId = m_sceneRegistry.CreateEntity(entityName, active);
                    auto transformComponent = std::make_unique<TransformComponent>();
                    TransformComponent *transformComponentPtr = transformComponent.get();
                    int32_t transformId = HierarchyTree::DEFAULT_TRANSFORM_ID;

                    if (object.HasMember("transform")) {
                        const rapidjson::Value &transformJsonObj = object["transform"];
                        if (transformJsonObj.HasMember("id")) {
                            transformId = transformJsonObj["id"].GetInt();
                        }

                        transformComponentPtr->SetTransformId(transformId);

                        if (transformJsonObj.HasMember("childrenIds")) {
                            const rapidjson::Value &childrenIdsArray = transformJsonObj["childrenIds"];
                            for (rapidjson::SizeType j = 0; j < childrenIdsArray.Size(); j++) {
                                const int childrenId = childrenIdsArray[j].GetInt();
                                if (transformId != -1) {
                                    childTransformIdToParentEntityMap[childrenId] = entityId;
                                }
                            }
                        }

                        const rapidjson::Value &translationArray = transformJsonObj["translation"];
                        const rapidjson::Value &scaleArray = transformJsonObj["scale"];
                        const rapidjson::Value &rotationArray = transformJsonObj["rotation"];

                        transformComponentPtr->SetTranslation(glm::vec3{
                                translationArray[0].GetFloat(),
                                translationArray[1].GetFloat(),
                                translationArray[2].GetFloat()});
                        transformComponentPtr->SetScale(glm::vec3{
                                scaleArray[0].GetFloat(),
                                scaleArray[1].GetFloat(),
                                scaleArray[2].GetFloat()});
                        transformComponentPtr->SetRotation(glm::radians(glm::vec3{
                                rotationArray[0].GetFloat(),
                                rotationArray[1].GetFloat(),
                                rotationArray[2].GetFloat()}));
                    }

                    m_ownedComponents.emplace_back(std::move(transformComponent));
                    m_sceneRegistry.AddComponent<TransformComponent>(entityId, transformComponentPtr);
                    hierarchyEntries.push_back(HierarchyEntry{entityId, transformId});

                    if (object.HasMember("componentIds")) {
                        auto componentIdsArray = object["componentIds"].GetArray();
                        for (rapidjson::SizeType j = 0; j < componentIdsArray.Size(); j++) {
                            const int componentId = componentIdsArray[j].GetInt();
                            std::unique_ptr<Component> componentOwner(componentFactory.CreateComponent(componentsMap, componentId));
                            if (componentOwner) {
                                Component *componentPtr = componentOwner.get();
                                m_sceneRegistry.AddComponent(entityId, componentPtr);
                                m_ownedComponents.emplace_back(std::move(componentOwner));
                            }
                        }
                    }
                }
            }

            for (const auto &hierarchyEntry: hierarchyEntries) {
                TransformComponent *childTransform = nullptr;
                if (!m_sceneRegistry.TryGetComponent(hierarchyEntry.entityId, childTransform) || childTransform == nullptr) {
                    continue;
                }

                const auto parentEntry = childTransformIdToParentEntityMap.find(hierarchyEntry.transformId);
                if (parentEntry != childTransformIdToParentEntityMap.end()) {
                    TransformComponent *parentTransform = nullptr;
                    if (m_sceneRegistry.TryGetComponent(parentEntry->second, parentTransform) && parentTransform != nullptr) {
                        parentTransform->AddChild(childTransform);
                    }
                    m_hierarchyTree.AddNode(static_cast<int>(parentEntry->second), static_cast<int>(hierarchyEntry.entityId), hierarchyEntry.transformId);
                } else {
                    m_hierarchyTree.AddNode(HierarchyTree::ROOT_ID, static_cast<int>(hierarchyEntry.entityId), hierarchyEntry.transformId);
                }
            }

            for (const auto entityId: m_sceneRegistry.GetEntityOrder()) {
                for (auto *component: m_sceneRegistry.GetComponents(entityId)) {
                    if (component != nullptr) {
                        component->OnLoad(entityId, m_sceneRegistry);
                    }
                }
            }

            for (const auto entityId: m_sceneRegistry.GetEntityOrder()) {
                for (auto *component: m_sceneRegistry.GetComponents(entityId)) {
                    if (component != nullptr) {
                        component->Loaded(entityId, m_sceneRegistry);
                    }
                }
            }

#ifdef RAY_TRACING
            for (const auto entityId: m_sceneRegistry.View<MeshRendererComponent>()) {
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                if (meshRendererComponent != nullptr && meshRendererComponent->GetModelPtr() != nullptr) {
                    BLAS::modelToBLASInput(meshRendererComponent->GetModelPtr());
                }
            }
            BLAS::buildBLAS(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
#endif
        }

        void loadMaterials() {

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
            shaderModulePointers.push_back(std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(rayGenShaderPath), ShaderCategory::rayGen));

            //Miss Shader
            const std::string rayMissShaderPath = "RayTracing/raytrace.rmiss.spv";
            shaderModulePointers.push_back(std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(rayMissShaderPath), ShaderCategory::rayMiss));
            const std::string rayMiss2ShaderPath = "RayTracing/raytraceShadow.rmiss.spv";
            shaderModulePointers.push_back(std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(rayMiss2ShaderPath), ShaderCategory::rayMiss2));

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
                    shaderModulePointers.push_back(std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(rayClosestShaderName), ShaderCategory::rayClosestHit));
                    if (object.HasMember("rayAnyHitShader")) {
                        const std::string rayAnyHitShaderName = object["rayAnyHitShader"].GetString();
                        shaderModulePointers.push_back(std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(rayAnyHitShaderName), ShaderCategory::rayAnyHit));
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
            for (const auto entityId: m_sceneRegistry.View<MeshRendererComponent, TransformComponent>()) {
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                auto *transformComponent = TryGetSceneComponent<TransformComponent>(entityId);
                if (meshRendererComponent == nullptr || transformComponent == nullptr) {
                    continue;
                }

                auto model = meshRendererComponent->GetModelPtr();
                if (model == nullptr) {
                    continue;
                }

                TLAS::createTLAS(*model,
                                 meshRendererComponent->GetTLASId(),
                                 idShaderOffsetMap[meshRendererComponent->GetMaterialID()],
                                 transformComponent->mat4());
            }
            TLAS::buildTLAS();

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
            accelerationStructureInfo->pAccelerationStructures = &TLAS::tlas;

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
            m_pGameObjectDescs.resize(meshRendererEntities.size());
            for (const auto entityId: meshRendererEntities) {
                GameObjectDesc modelDesc{};
                auto *meshRendererComponent = TryGetSceneComponent<MeshRendererComponent>(entityId);
                if (meshRendererComponent == nullptr || meshRendererComponent->GetModelPtr() == nullptr) {
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
                }
                m_pGameObjectDescs[meshRendererComponent->GetTLASId()] = modelDesc;
            }

            m_pGameObjectDescBuffer = std::make_shared<Buffer>(m_device, sizeof(GameObjectDesc), m_pGameObjectDescs.size(),
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, minStorageBufferOffsetAlignment);
            m_pGameObjectDescBuffer->map(m_pGameObjectDescBuffer->getBufferSize());
            m_pGameObjectDescBuffer->writeToBuffer(m_pGameObjectDescs.data(), m_pGameObjectDescs.size() * sizeof(GameObjectDesc));
            bufferPointers.push_back(m_pGameObjectDescBuffer);

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
                    writeBuffer(1, m_pGameObjectDescBuffer->descriptorInfo(m_pGameObjectDescBuffer->getBufferSize())).
                    writeImages(2, imageInfos).
                    writeImage(3, skyBoxImage->descriptorInfo(*skyBoxSampler)).
                    build(sceneDescriptorSet);
            descriptorSetPointers.push_back(sceneDescriptorSet);
            auto material = std::make_shared<Material>(Material::MaterialId::rayTracing, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers,
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
                    std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(PostVertexShaderName), ShaderCategory::vertex),
                    std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(PostFragmentShaderName), ShaderCategory::fragment),
            };
            std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{postSystemDescriptorSetLayoutPtr};
            std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{postDescriptorSet};
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

            auto postMaterial = std::make_shared<Material>(Material::MaterialId::post, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
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
                    std::make_shared<ShaderModule>(m_shaderBuilder.createShaderModule(RayTracingDenoiseComputeShaderName), ShaderCategory::compute),
            };
            std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayoutPointers{computeSystemDescriptorSetLayoutPtr};
            std::vector<std::shared_ptr<VkDescriptorSet>> descriptorSetPointers{postDescriptorSet};
            std::vector<std::shared_ptr<Image>> imagePointers{};
            std::vector<std::shared_ptr<Sampler>> samplerPointers{};
            std::vector<std::shared_ptr<Buffer>> bufferPointers{globalUboBufferPtr};

            auto computeMaterial = std::make_shared<Material>(Material::MaterialId::compute, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
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

                    m_shaderBuilder.createShaderModule(vertexShaderName);
                    m_shaderBuilder.createShaderModule(fragmentShaderName);
                    std::vector<std::shared_ptr<ShaderModule>> shaderModulePointers{
                            std::make_shared<ShaderModule>(m_shaderBuilder.getShaderModulePointer(vertexShaderName),
                                                           ShaderCategory::vertex),
                            std::make_shared<ShaderModule>(m_shaderBuilder.getShaderModulePointer(fragmentShaderName),
                                                           ShaderCategory::fragment)};

                    const bool tessEnabled = object.HasMember("tessellationControlShader");
                    if (tessEnabled) {
                        const std::string tessellationControlShaderName = object["tessellationControlShader"].GetString();
                        const std::string tessellationEvaluationShaderName = object["tessellationEvaluationShader"].GetString();
                        shaderModulePointers.emplace_back(std::make_shared<ShaderModule>(
                                m_shaderBuilder.getShaderModulePointer(tessellationControlShaderName),
                                ShaderCategory::tessellationControl));
                        shaderModulePointers.emplace_back(std::make_shared<ShaderModule>(
                                m_shaderBuilder.getShaderModulePointer(tessellationEvaluationShaderName),
                                ShaderCategory::tessellationEvaluation));
                    }

                    const bool geomEnabled = object.HasMember("geometryShader");
                    if (geomEnabled) {
                        const std::string geometryShaderName = object["geometryShader"].GetString();
                        shaderModulePointers.emplace_back(
                                std::make_shared<ShaderModule>(m_shaderBuilder.getShaderModulePointer(geometryShaderName),
                                                               ShaderCategory::geometry));
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


                    auto m_material = std::make_shared<Material>(id, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers,
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

                auto uiMaterial = std::make_shared<Material>(Material::MaterialId::gizmos, shaderModulePointers, descriptorSetLayoutPointers, descriptorSetPointers, imagePointers, samplerPointers,
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
        template<typename T>
        T *TryGetSceneComponent(const id_t entityId) {
            T *component = nullptr;
            return m_sceneRegistry.TryGetComponent(entityId, component) ? component : nullptr;
        }

        MyWindow m_window{SCENE_WIDTH + UI_LEFT_WIDTH + UI_LEFT_WIDTH_2, SCENE_HEIGHT, "FeatherVK"};
        Device m_device{m_window};
        Renderer m_renderer{m_window, m_device};
        ShaderBuilder m_shaderBuilder{m_device};
        std::shared_ptr<DescriptorPool> m_globalPool;
        ECS::SceneRegistry m_sceneRegistry;
        HierarchyTree m_hierarchyTree;
        std::vector<std::unique_ptr<Component>> m_ownedComponents;
        Material::Map m_materials;
        std::unordered_map<std::string, TextureCacheEntry> m_textureCache;

#ifdef RAY_TRACING
        std::shared_ptr<Buffer> m_pGameObjectDescBuffer;
        std::vector<GameObjectDesc> m_pGameObjectDescs;
#endif
    };
}











