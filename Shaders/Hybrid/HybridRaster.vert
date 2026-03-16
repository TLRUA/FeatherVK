#version 450

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    int lightCategory;
};

layout (set = 0, binding = 0) uniform GlobalUbo {
    mat4 viewMatrix;
    mat4 inverseViewMatrix;
    mat4 projectionMatrix;
    mat4 inverseProjectionMatrix;
    float curTime;
    int lightNum;
    Light lights[10];
} ubo;

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec3 smoothedNormal;
layout (location = 4) in vec2 uv;

layout (location = 0) out vec3 worldPos;
layout (location = 1) out vec3 worldNormal;
layout (location = 2) out vec3 vertexColor;
layout (location = 3) out vec4 baseColorMetallic;
layout (location = 4) out vec4 emissiveRoughnessOpacity;

layout (push_constant) uniform PushConstantData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 baseColorMetallic;
    vec4 emissiveRoughnessOpacity;
} push;

void main() {
    vec4 worldPosition = push.modelMatrix * vec4(position, 1.0);
    worldPos = worldPosition.xyz;
    worldNormal = normalize((push.normalMatrix * vec4(normal, 0.0)).xyz);
    vertexColor = color;
    baseColorMetallic = push.baseColorMetallic;
    emissiveRoughnessOpacity = push.emissiveRoughnessOpacity;
    gl_Position = ubo.projectionMatrix * ubo.viewMatrix * worldPosition;
}