#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "../Model.hpp"

namespace FeatherVK {
    class MeshRendererComponent {
    public:
        MeshRendererComponent(std::shared_ptr<Model> modelPtr = nullptr, id_t materialID = 0)
            : model(std::move(modelPtr)), materialId(materialID) {}

        id_t GetMaterialID() const { return materialId; }

        void SetMaterialID(id_t id) { materialId = id; }

        std::shared_ptr<Model> GetModelPtr() { return model; }

        std::shared_ptr<Model> GetModelPtr() const { return model; }

        bool IsVisible() const { return visible; }

        bool CastsShadow() const { return castShadow; }

        bool ReceivesShadow() const { return receiveShadow; }

        uint32_t GetRenderLayer() const { return renderLayer; }

        bool visible = true;
        uint32_t renderLayer = 0;
        bool castShadow = true;
        bool receiveShadow = true;

    private:
        id_t materialId{};
        std::shared_ptr<Model> model = nullptr;
    };
}
