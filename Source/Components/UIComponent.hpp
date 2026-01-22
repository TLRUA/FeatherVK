#pragma once

#include <string>

namespace FeatherVK {
    class UIComponent {
    public:
        enum class ElementType {
            Canvas,
            Panel,
            Image,
            Text,
            Button
        };

        UIComponent() : UIComponent(ElementType::Canvas) {}

        explicit UIComponent(ElementType elementType) : m_elementType(elementType) {}

        ElementType GetElementType() const { return m_elementType; }

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
