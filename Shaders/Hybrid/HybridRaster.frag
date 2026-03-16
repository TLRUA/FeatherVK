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

layout (location = 0) in vec3 worldPos;
layout (location = 1) in vec3 worldNormal;
layout (location = 2) in vec3 vertexColor;
layout (location = 3) in vec4 baseColorMetallic;
layout (location = 4) in vec4 emissiveRoughnessOpacity;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(worldNormal);
    vec3 baseColor = max(baseColorMetallic.rgb, vec3(0.0));
    float metallic = clamp(baseColorMetallic.a, 0.0, 1.0);
    vec3 emissive = max(emissiveRoughnessOpacity.rgb, vec3(0.0));
    float opacity = clamp(emissiveRoughnessOpacity.a, 0.0, 1.0);

    vec3 color = baseColor * 0.08;
    for (int i = 0; i < ubo.lightNum; ++i) {
        Light light = ubo.lights[i];
        if (light.lightCategory == -1) {
            continue;
        }

        vec3 lightDirection = vec3(0.0, 1.0, 0.0);
        float attenuation = 1.0;
        if (light.lightCategory == 0) {
            vec3 toLight = light.position.xyz - worldPos;
            float distanceToLight = max(length(toLight), 0.0001);
            lightDirection = toLight / distanceToLight;
            attenuation = min(1.0, 1.0 / (distanceToLight * distanceToLight));
        } else {
            lightDirection = normalize(-light.direction.xyz);
        }

        float nDotL = max(dot(normal, lightDirection), 0.0);
        vec3 radiance = light.color.rgb * light.color.a * attenuation;
        float diffuseWeight = mix(1.0, 0.15, metallic);
        color += baseColor * radiance * nDotL * diffuseWeight;
    }

    color += emissive;
    outColor = vec4(max(color, vec3(0.0)), opacity);
}