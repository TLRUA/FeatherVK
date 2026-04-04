#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "../Material.hpp"
#include "../Model.hpp"
#include "../RenderCore/RenderResourceHandle.hpp"

namespace FeatherVK {
    class MeshRendererComponent {
    public:
        inline static constexpr uint32_t DefaultRenderLayer = 0;

        MeshRendererComponent(std::shared_ptr<Model> modelPtr = nullptr, id_t materialID = 0)
            : model(std::move(modelPtr)), materialId(materialID) {}

        id_t GetMaterialID() const { return materialId; }

        void SetMaterialID(id_t id) { materialId = id; }

        std::shared_ptr<Model> GetModelPtr() { return model; }

        std::shared_ptr<Model> GetModelPtr() const { return model; }

        bool IsVisible() const { return visible; }

        void SetVisible(bool value) { visible = value; }

        bool CastsShadow() const { return castShadow; }

        void SetCastShadow(bool value) { castShadow = value; }

        bool ReceivesShadow() const { return receiveShadow; }

        void SetReceiveShadow(bool value) { receiveShadow = value; }

        uint32_t GetRenderLayer() const { return renderLayer; }

        void SetRenderLayer(uint32_t value) { renderLayer = value; }

        bool IsOnDefaultRenderLayer() const { return renderLayer == DefaultRenderLayer; }

        uint32_t GetRayTracingVisibilityMask() const {
            const uint32_t clampedLayer = std::min(renderLayer, 7u);
            return 1u << clampedLayer;
        }

        bool HasPbrOverride() const { return runtimePbrOverride.has_value(); }

        const std::optional<PBR> &GetPbrOverride() const { return runtimePbrOverride; }

        void SetPbrOverride(const PBR &value) { runtimePbrOverride = value; }

        void ClearPbrOverride() { runtimePbrOverride.reset(); }

        RenderCore::RenderResourceHandle GetMeshResourceHandle() const { return meshResourceHandle; }

        void SetMeshResourceHandle(RenderCore::RenderResourceHandle handle) { meshResourceHandle = handle; }

        RenderCore::RenderResourceHandle GetMaterialResourceHandle() const { return materialResourceHandle; }

        void SetMaterialResourceHandle(RenderCore::RenderResourceHandle handle) { materialResourceHandle = handle; }

        RenderCore::RenderResourceHandle GetMaterialInstanceHandle() const { return materialInstanceHandle; }

        void SetMaterialInstanceHandle(RenderCore::RenderResourceHandle handle) { materialInstanceHandle = handle; }

        bool visible = true;
        uint32_t renderLayer = 0;
        bool castShadow = true;
        bool receiveShadow = true;

    private:
        id_t materialId{};
        std::shared_ptr<Model> model = nullptr;
        std::optional<PBR> runtimePbrOverride{};
        RenderCore::RenderResourceHandle meshResourceHandle{};
        RenderCore::RenderResourceHandle materialResourceHandle{};
        RenderCore::RenderResourceHandle materialInstanceHandle{};
    };
}
