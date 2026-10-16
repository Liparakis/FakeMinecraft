#include "Canvas.h"

#include "../Renderer/Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace ui {
    namespace {
        constexpr int ATLAS_SIZE = 512;
        constexpr float BAKE_PIXEL_HEIGHT = 48.0f;
        constexpr int FIRST_CHAR = 32, CHAR_COUNT = 96;
        // The atlas' last texel is forced white so solid shapes can share the text pipeline.
        constexpr float SOLID_UV = (ATLAS_SIZE - 0.5f) / ATLAS_SIZE;

        struct UIVertex {
            float x, y, u, v;
            uint8_t rgba[4];
        };
        static_assert(sizeof(UIVertex) == 20);

        stbtt_bakedchar glyphs[CHAR_COUNT];
        TextureId atlas = 0;
        bool ready = false;
        std::vector<UIVertex> vertices;

        void pushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      const float color[3], const float opacity) {
            const uint8_t rgba[4] = {
                static_cast<uint8_t>(std::clamp(color[0], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(color[1], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(color[2], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(opacity, 0.0f, 1.0f) * 255.0f),
            };
            const UIVertex quad[6] = {
                {x0, y0, u0, v0, {}}, {x1, y0, u1, v0, {}}, {x1, y1, u1, v1, {}},
                {x0, y0, u0, v0, {}}, {x1, y1, u1, v1, {}}, {x0, y1, u0, v1, {}},
            };
            for (UIVertex vtx : quad) {
                std::memcpy(vtx.rgba, rgba, 4);
                vertices.push_back(vtx);
            }
        }

        void solid(float x0, float y0, float x1, float y1, const float color[3], const float opacity) {
            pushQuad(x0, y0, x1, y1, SOLID_UV, SOLID_UV, SOLID_UV, SOLID_UV, color, opacity);
        }
    }

    void init(const std::string &fontPath) {
        std::ifstream file(fontPath, std::ios::binary);
        if (!file) {
            std::cerr << "[UI] Failed to open font " << fontPath << std::endl;
            return;
        }
        const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        std::vector<unsigned char> bitmap(ATLAS_SIZE * ATLAS_SIZE, 0);
        if (stbtt_BakeFontBitmap(ttf.data(), 0, BAKE_PIXEL_HEIGHT, bitmap.data(), ATLAS_SIZE, ATLAS_SIZE,
                                 FIRST_CHAR, CHAR_COUNT, glyphs) <= 0) {
            std::cerr << "[UI] Font atlas too small for " << fontPath << std::endl;
        }
        bitmap.back() = 255;
        atlas = Renderer::get().createTexture(ATLAS_SIZE, ATLAS_SIZE, VK_FORMAT_R8_UNORM, bitmap.data(), true);
        ready = true;
    }

    void drawRect(const float centerX, const float centerY, const float width, const float height, const Brush &brush) {
        const float x0 = centerX - width * 0.5f, x1 = centerX + width * 0.5f;
        const float y0 = centerY - height * 0.5f, y1 = centerY + height * 0.5f;
        if (brush.fill_opacity > 0.0f) solid(x0, y0, x1, y1, brush.fill_color, brush.fill_opacity);

        if (brush.outline_opacity > 0.0f && brush.outline_width > 0.0f) {
            const float t = brush.outline_width * 0.5f;
            solid(x0 - t, y0 - t, x1 + t, y0 + t, brush.outline_color, brush.outline_opacity);
            solid(x0 - t, y1 - t, x1 + t, y1 + t, brush.outline_color, brush.outline_opacity);
            solid(x0 - t, y0 + t, x0 + t, y1 - t, brush.outline_color, brush.outline_opacity);
            solid(x1 - t, y0 + t, x1 + t, y1 - t, brush.outline_color, brush.outline_opacity);
        }
    }

    void drawLine(const float x1, const float y1, const float x2, const float y2, const Brush &brush) {
        const float dx = x2 - x1, dy = y2 - y1;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f || brush.outline_opacity <= 0.0f) return;

        // A line is a thin quad: offset both ends along the perpendicular by half the width.
        const float nx = -dy / length * brush.outline_width * 0.5f;
        const float ny = dx / length * brush.outline_width * 0.5f;
        const uint8_t rgba[4] = {
            static_cast<uint8_t>(brush.outline_color[0] * 255.0f), static_cast<uint8_t>(brush.outline_color[1] * 255.0f),
            static_cast<uint8_t>(brush.outline_color[2] * 255.0f), static_cast<uint8_t>(brush.outline_opacity * 255.0f),
        };
        const UIVertex quad[6] = {
            {x1 + nx, y1 + ny, SOLID_UV, SOLID_UV, {}}, {x2 + nx, y2 + ny, SOLID_UV, SOLID_UV, {}},
            {x2 - nx, y2 - ny, SOLID_UV, SOLID_UV, {}}, {x1 + nx, y1 + ny, SOLID_UV, SOLID_UV, {}},
            {x2 - nx, y2 - ny, SOLID_UV, SOLID_UV, {}}, {x1 - nx, y1 - ny, SOLID_UV, SOLID_UV, {}},
        };
        for (UIVertex vtx : quad) {
            std::memcpy(vtx.rgba, rgba, 4);
            vertices.push_back(vtx);
        }
    }

    void drawText(const float x, const float baselineY, const float size, const std::string &text, const Brush &brush) {
        if (!ready) return;
        const float scale = size / BAKE_PIXEL_HEIGHT;
        float penX = 0.0f, penY = 0.0f;
        for (const char c : text) {
            if (c < FIRST_CHAR || c >= FIRST_CHAR + CHAR_COUNT) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(glyphs, ATLAS_SIZE, ATLAS_SIZE, c - FIRST_CHAR, &penX, &penY, &q, 1);
            pushQuad(x + q.x0 * scale, baselineY + q.y0 * scale, x + q.x1 * scale, baselineY + q.y1 * scale,
                     q.s0, q.t0, q.s1, q.t1, brush.fill_color, brush.fill_opacity);
        }
    }

    void flush(const float screenWidth, const float screenHeight) {
        if (vertices.empty() || !ready) {
            vertices.clear();
            return;
        }
        Renderer &renderer = Renderer::get();
        GpuBuffer buffer;
        uint32_t firstVertex = 0;
        void *dst = renderer.allocateTransient(vertices.size() * sizeof(UIVertex), buffer, firstVertex, sizeof(UIVertex));
        if (dst) {
            std::memcpy(dst, vertices.data(), vertices.size() * sizeof(UIVertex));
            renderer.bindPipeline(PipelineId::UI);
            renderer.bindTexture(atlas);
            const float screen[2] = {screenWidth, screenHeight};
            renderer.pushConstants(screen, sizeof(screen));
            renderer.draw(buffer, static_cast<uint32_t>(vertices.size()), firstVertex);
        }
        vertices.clear();
    }
}
