#version 450

layout(location = 0) in ivec4 aPos;

layout(push_constant) uniform Push {
    mat4 lightSpace;
    ivec4 chunkOrigin; // Chunk vertices are chunk-local in x and z (Renderer::pushChunkOrigin).
} push;

void main() {
    gl_Position = push.lightSpace * vec4(vec3(aPos.xyz + push.chunkOrigin.xyz), 1.0);
}
