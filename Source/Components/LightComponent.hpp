#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/detail/type_mat3x3.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>

#include "Component.hpp"
#include "TransformComponent.hpp"
#include "../Model.hpp"

namespace FeatherVK {

    class LightComponent : public Component {
    public:
        LightComponent() {
            name = "LightComponent";
        }

        LightComponent(const rapidjson::Value &object) {
            name = "LightComponent";
            auto colorArray = object["color"].GetArray();
            color = glm::vec3(colorArray[0].GetFloat(), colorArray[1].GetFloat(), colorArray[2].GetFloat());
            const auto jsonLightTypeStr = object["category"].GetString();
            lightCategory = lightCategoryMap[jsonLightTypeStr];
            lightTypeStr = jsonLightTypeStr;
            lightIntensity = object["intensity"].GetFloat();
            lightIndex = lightNum;
            lightNum++;
        }

        void Update(const ComponentUpdateInfo &updateInfo) override {
            assert(lightIndex < MAX_LIGHT_NUM && "light count overflow");
            if (updateInfo.transform == nullptr || updateInfo.frameInfo == nullptr) {
                return;
            }

            Light light{};
            TransformComponent *transform = updateInfo.transform;
            if (lightCategory == LightCategory::POINT_LIGHT) {
                light.lightCategory = LightCategory::POINT_LIGHT;
            } else {
                light.lightCategory = LightCategory::DIRECTIONAL_LIGHT;
            }

            light.position = glm::vec4(transform->GetTranslation(), 1.f);
            const auto rotation = transform->GetRotation();
            auto rotateMatrix = glm::rotate(glm::mat4(1.f), rotation.y, {0, 1, 0});
            rotateMatrix = glm::rotate(rotateMatrix, rotation.x, {1, 0, 0});
            rotateMatrix = glm::rotate(rotateMatrix, rotation.z, {0, 0, 1});
            light.color = glm::vec4(color, lightIntensity);
            light.direction = rotateMatrix * glm::vec4(0, 0, 1, 0);
            updateInfo.frameInfo->globalUbo.lights[lightIndex] = light;
        }

        void OnDisable(const ComponentUpdateInfo &updateInfo) override {
            if (updateInfo.frameInfo == nullptr) {
                return;
            }
            updateInfo.frameInfo->globalUbo.lights[lightIndex].lightCategory = LightCategory::NONE;
        }

#ifdef RAY_TRACING
        void SetUI(std::vector<GameObjectDesc> *, FrameInfo &frameInfo) override {
            ImGui::Text("Color:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat3("##Color", &color.x)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Intensity:");
            ImGui::SameLine(90);
            if (ImGui::InputFloat("##Intensity", &lightIntensity)) {
                frameInfo.sceneUpdated = true;
            }

            ImGui::Text("Type:");
            ImGui::SameLine(90);
            ImGui::Text(lightTypeStr.c_str());
        }
#else
        void SetUI(Material::Map *, FrameInfo &) override {
            ImGui::Text("Color:");
            ImGui::SameLine(90);
            ImGui::InputFloat3("##Color", &color.x);

            ImGui::Text("Intensity:");
            ImGui::SameLine(90);
            ImGui::InputFloat("##Intensity", &lightIntensity);

            ImGui::Text("Type:");
            ImGui::SameLine(90);
            ImGui::Text(lightTypeStr.c_str());
        }
#endif

        static int GetLightNum() {
            return lightNum;
        }

        const glm::vec3 getColor() const {
            return color;
        }

        int getLightCategory() const {
            return lightCategory;
        }

        float getLightIntensity() const {
            return lightIntensity;
        }

        int getLightIndex() const {
            return lightIndex;
        }

        int getLightNum() {
            return lightNum;
        }

    private:
        inline static int lightNum = 0;

        int lightIndex = 0;
        float lightIntensity = 1.0f;
        int lightCategory = 0;
        std::string lightTypeStr;
        glm::vec3 color{};

        std::unordered_map<std::string, int> lightCategoryMap = {
                {"Point", LightCategory::POINT_LIGHT},
                {"Directional", LightCategory::DIRECTIONAL_LIGHT},
        };
    };
}

