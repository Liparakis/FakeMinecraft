#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} push;

void main() {
    gl_Position = frame.projection * frame.view * push.model * vec4(aPos, 1.0);
}
