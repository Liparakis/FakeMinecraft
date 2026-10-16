#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} push;

layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = push.color;
}
