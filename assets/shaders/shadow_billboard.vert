#version 450

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform Push {
    mat4 lightSpace;
} push;

void main() {
    gl_Position = push.lightSpace * vec4(aPos, 1.0);
}
