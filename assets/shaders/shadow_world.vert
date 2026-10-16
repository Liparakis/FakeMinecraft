#version 450

layout(location = 0) in ivec4 aPos;

layout(push_constant) uniform Push {
    mat4 lightSpace;
} push;

void main() {
    gl_Position = push.lightSpace * vec4(vec3(aPos.xyz), 1.0);
}
