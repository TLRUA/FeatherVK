#pragma once

#include <algorithm>

#include "../Components/LightComponent.hpp"
#include "../Components/MeshRendererComponent.hpp"
#include "../ECS/SceneRegistry.hpp"

namespace FeatherVK::LightEmitterMeshUtils {
    inline PBR CreateDefaultPrimitivePbr() {
        PBR pbr{};
        pbr.albedo = glm::vec3{1.0f, 1.0f, 1.0f};
        pbr.normal = glm::vec3{0.0f};
        pbr.metallic = 0.0f;
        pbr.roughness = 1.0f;
        pbr.opacity = 1.0f;
        pbr.AO = 1.0f;
        pbr.emissive = glm::vec3{0.0f};
        return pbr;
    }

    inline glm::vec3 CreateLightEmitterEmissive(const LightComponent &lightComponent) {
        const glm::vec3 color{
            std::max(lightComponent.GetColor().x, 0.0f),
            std::max(lightComponent.GetColor().y, 0.0f),
            std::max(lightComponent.GetColor().z, 0.0f)};
        return color * std::max(lightComponent.GetLightIntensity(), 0.0f);
    }

    inline PBR CreateLightEmitterPbr(const MeshRendererComponent &meshRenderer, const LightComponent &lightComponent) {
        PBR pbr = meshRenderer.HasPbrOverride() ? *meshRenderer.GetPbrOverride() : CreateDefaultPrimitivePbr();
        if (pbr.albedo == glm::vec3{-1.0f}) {
            pbr.albedo = glm::vec3{1.0f};
        }
        if (pbr.normal == glm::vec3{-1.0f}) {
            pbr.normal = glm::vec3{0.0f};
        }
        if (pbr.metallic < 0.0f) {
            pbr.metallic = 0.0f;
        }
        if (pbr.roughness < 0.0f) {
            pbr.roughness = 0.35f;
        }
        if (pbr.opacity < 0.0f) {
            pbr.opacity = 1.0f;
        }
        if (pbr.AO < 0.0f) {
            pbr.AO = 1.0f;
        }
        pbr.emissive = CreateLightEmitterEmissive(lightComponent);
        return pbr;
    }

    inline bool IsLightEmitterMesh(ECS::SceneRegistry &sceneRegistry, id_t entityId) {
        return sceneRegistry.HasComponent<LightComponent>(entityId) &&
               sceneRegistry.HasComponent<MeshRendererComponent>(entityId);
    }

    inline void SyncLightEmitterEmissive(ECS::SceneRegistry &sceneRegistry, id_t entityId, const LightComponent &lightComponent) {
        MeshRendererComponent *meshRenderer = nullptr;
        if (!sceneRegistry.TryGetComponent(entityId, meshRenderer) || meshRenderer == nullptr) {
            return;
        }
        meshRenderer->SetPbrOverride(CreateLightEmitterPbr(*meshRenderer, lightComponent));
    }

    inline bool ApplyLightEmitterMeshConstraints(ECS::SceneRegistry &sceneRegistry, id_t entityId) {
        LightComponent *lightComponent = nullptr;
        MeshRendererComponent *meshRenderer = nullptr;
        if (!sceneRegistry.TryGetComponent(entityId, lightComponent) || lightComponent == nullptr ||
            !sceneRegistry.TryGetComponent(entityId, meshRenderer) || meshRenderer == nullptr) {
            return false;
        }

        bool changed = false;
        if (meshRenderer->CastsShadow()) {
            meshRenderer->SetCastShadow(false);
            changed = true;
        }
        if (meshRenderer->ReceivesShadow()) {
            meshRenderer->SetReceiveShadow(false);
            changed = true;
        }

        const PBR emitterPbr = CreateLightEmitterPbr(*meshRenderer, *lightComponent);
        if (!meshRenderer->HasPbrOverride() || meshRenderer->GetPbrOverride()->emissive != emitterPbr.emissive) {
            meshRenderer->SetPbrOverride(emitterPbr);
            changed = true;
        }
        return changed;
    }
}
