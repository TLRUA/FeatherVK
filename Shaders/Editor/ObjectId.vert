#version 450
#include "UBO.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) flat out int objectId;

layout(push_constant) uniform PushConstantData {
    mat4 modelMatrix;
    mat4 idCarrier;
} push;

void main() {
    gl_Position = ubo.projectionMatrix * ubo.viewMatrix * push.modelMatrix * vec4(position, 1.0);
    objectId = int(push.idCarrier[3][3]);
}