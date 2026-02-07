#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "RHI/RHICommands.hpp"
#include "Utils/Utils.hpp"
#include "Material.hpp"

namespace FeatherVK {

#define MAX_LIGHT_NUM 10
#define MAX_SHADOW_NUM 3

    namespace ECS {
        class SceneRegistry;
    }

    enum LightCategory {
        NONE = -1,
        POINT_LIGHT = 0,
        DIRECTIONAL_LIGHT = 1
    };

    struct Light {
        glm::vec4 position{};
        glm::vec4 direction{};
        glm::vec4 color{};
        // 0: point lights, 1: directional lights
        alignas(16) LightCategory lightCategory;
    };

    struct ViewportRect {
        float x{0.0f};
        float y{0.0f};
        float width{0.0f};
        float height{0.0f};

        [[nodiscard]] float Right() const { return x + width; }
        [[nodiscard]] float Bottom() const { return y + height; }
        [[nodiscard]] bool Contains(float px, float py) const {
            return px >= x && px < Right() && py >= y && py < Bottom();
        }
    };

#ifdef RAY_TRACING
    struct GlobalUbo {
        glm::mat4 viewMatrix{1.f};
        glm::mat4 inverseViewMatrix{1.f};
        glm::mat4 projectionMatrix{1.f};
        glm::mat4 inverseProjectionMatrix{1.f};
        float curTime;
        int lightNum;
        alignas(16) Light lights[MAX_LIGHT_NUM];
    };

    struct EntityDesc {
        uint64_t vertexBufferAddress{}; // 0~8
        uint64_t indexBufferAddress{};  // 8~16
        PBR pbr{};                      // 16~80
        glm::i32vec2 textureEntry{};    // 80~88
        int32_t padding0{};             // 88~92
        int32_t padding1{};             // 92~96
    };

    static_assert(offsetof(EntityDesc, textureEntry) == 80, "EntityDesc textureEntry offset must match ray tracing shader.");
    static_assert(sizeof(EntityDesc) == 96, "EntityDesc size must match ray tracing shader ArrayStride.");
#else
    struct GlobalUbo {
        glm::mat4 viewMatrix{1.f};
        glm::mat4 inverseViewMatrix{1.f};
        glm::mat4 projectionMatrix{1.f};
        glm::vec4 ambientColor{1, 1, 1, 0.005f};
        Light lights[MAX_LIGHT_NUM];
        alignas(16) int lightNum;
        alignas(16) glm::mat4 lightProjectionViewMatrix;
        alignas(16) float curTime;
        alignas(16) glm::mat4 shadowViewMatrix[6];
        alignas(16) glm::mat4 shadowProjMatrix;
    };
#endif

    struct FrameInfo {
        int frameIndex;
        float frameTime;
        float totalTime;
        VkCommandBuffer commandBuffer;
        RHI::RHICommandList *commandList;
        ECS::SceneRegistry *sceneRegistry;
        Material::Map &materials;
        GlobalUbo &globalUbo;
        VkExtent2D extent;
        VkExtent2D sceneRenderExtent;
        ViewportRect scenePanelRect;
        ViewportRect sceneViewportRect;
        id_t selectedEntityId;
        bool sceneUpdated;
#ifdef RAY_TRACING
        std::shared_ptr<Buffer> pEntityDescBuffer;
        std::vector<EntityDesc> pEntityDescs;
#endif
    };

    struct RendererInfo {
        float aspectRatio;
        float fovY;
        float near;
        float far;
    };

    struct ShadowUbo {
        glm::mat4 viewProjectionMatrix;
    };
}



