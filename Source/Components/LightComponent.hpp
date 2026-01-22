#pragma once

#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../StructureInfos.h"

namespace FeatherVK {
    class LightComponent {
    public:
        LightComponent() = default;

        const glm::vec3 &GetColor() const {
            return color;
        }

        LightCategory GetLightCategory() const {
            return lightCategory;
        }

        float GetLightIntensity() const {
            return lightIntensity;
        }

        void SetLightCategory(LightCategory category) {
            lightCategory = category == LightCategory::DIRECTIONAL_LIGHT ? LightCategory::DIRECTIONAL_LIGHT : LightCategory::POINT_LIGHT;
        }

        const char *GetLightTypeLabel() const {
            return lightCategory == LightCategory::DIRECTIONAL_LIGHT ? "Directional" : "Point";
        }

        glm::vec3 color{1.0f};
        float lightIntensity = 1.0f;
        LightCategory lightCategory = LightCategory::POINT_LIGHT;

    private:
        inline static const std::unordered_map<std::string, LightCategory> lightCategoryMap = {
            {"Point", LightCategory::POINT_LIGHT},
            {"Directional", LightCategory::DIRECTIONAL_LIGHT},
        };
    };
}
