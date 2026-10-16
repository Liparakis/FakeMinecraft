#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform Push {
    vec4 blockPos;
} push;

void main() {
    gl_Position = frame.projection * frame.view * vec4(aPos + push.blockPos.xyz, 1.0);
}
