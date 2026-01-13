#pragma once

#include <string>

#include "Component.hpp"

namespace FeatherVK {
    class UIComponent : public Component {
    public:
        enum class ElementType {
            Canvas,
            Panel,
            Image,
            Text,
            Button
        };

        UIComponent() : UIComponent(ElementType::Canvas) {}

        explicit UIComponent(ElementType elementType) : m_elementType(elementType) {
            name = "UIComponent";
        }

        explicit UIComponent(const rapidjson::Value &object) {
            name = "UIComponent";
            if (!object.HasMember("uiType")) {
                m_elementType = ElementType::Canvas;
                return;
            }

            const std::string typeString = object["uiType"].GetString();
            if (typeString == "Canvas") {
                m_elementType = ElementType::Canvas;
            } else if (typeString == "Panel") {
                m_elementType = ElementType::Panel;
            } else if (typeString == "Image") {
                m_elementType = ElementType::Image;
            } else if (typeString == "Text") {
                m_elementType = ElementType::Text;
            } else if (typeString == "Button") {
                m_elementType = ElementType::Button;
            } else {
                m_elementType = ElementType::Canvas;
            }
        }

        ElementType GetElementType() const { return m_elementType; }

#ifdef RAY_TRACING
        void SetUI(std::vector<EntityDesc> *, FrameInfo &) override {
#else
        void SetUI(Material::Map *, FrameInfo &) override {
#endif
            ImGui::Text("Type:");
            ImGui::SameLine(90);
            ImGui::Text(GetElementTypeName(m_elementType).c_str());
            ImGui::Text("Render:");
            ImGui::SameLine(90);
            ImGui::Text("Editor marker only");
        }

        static std::string GetElementTypeName(ElementType type) {
            switch (type) {
                case ElementType::Canvas:
                    return "Canvas";
                case ElementType::Panel:
                    return "Panel";
                case ElementType::Image:
                    return "Image";
                case ElementType::Text:
                    return "Text";
                case ElementType::Button:
                    return "Button";
                default:
                    return "Canvas";
            }
        }

    private:
        ElementType m_elementType = ElementType::Canvas;
    };
}
