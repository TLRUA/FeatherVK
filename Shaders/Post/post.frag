#version 450
layout (location = 0) in vec2 outUV;
layout (location = 0) out vec4 fragColor;

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

layout (set = 0, binding = 1) uniform sampler2D rayTracedEffects[2];
layout (set = 0, binding = 2) uniform sampler2D sceneColorImages[2];
layout (set = 0, binding = 3) uniform sampler2D shadowTermImages[2];

layout (push_constant, std430) uniform PushConstant {
    int rayTracingImageIndex;
    bool firstFrame;
    mat4 viewMatrix[2];
} pushConstant;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    int imageIndex = pushConstant.rayTracingImageIndex;
    vec3 sceneColor = texture(sceneColorImages[imageIndex], outUV).rgb;
    vec3 rayTracedEffect = texture(rayTracedEffects[imageIndex], outUV).rgb;
    vec4 shadowTerm = texture(shadowTermImages[imageIndex], outUV);

    float directLightingAmount = smoothstep(0.001, 0.08, luminance(shadowTerm.rgb));
    float shadowVisibility = clamp(shadowTerm.a, 0.0, 1.0);
    float shadowBlend = directLightingAmount * 0.65;
    vec3 shadowedSceneColor = sceneColor * mix(1.0, shadowVisibility, shadowBlend);

    vec3 linearColor = shadowedSceneColor + rayTracedEffect;
    vec3 toneMappedColor = linearColor / (linearColor + vec3(1.0));
    vec3 gammaColor = pow(max(toneMappedColor, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(gammaColor, 1.0);
}