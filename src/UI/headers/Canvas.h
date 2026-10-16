/**
 * @file Canvas.h
 * @brief Immediate-mode 2D drawing for the HUD and menus (rectangles, lines, text).
 *
 * Coordinates are window pixels with the origin at the top-left. Shapes are
 * collected during the frame and drawn in the renderer's UI pass by flush().
 */
#pragma once

#include <string>

namespace ui {
    struct Brush {
        float fill_color[3] = {1.f, 1.f, 1.f};
        float fill_opacity = 1.0f;
        float outline_color[3] = {1.f, 1.f, 1.f};
        float outline_opacity = 1.0f;
        float outline_width = 1.0f;
    };

    /// Loads the font and builds its atlas. Call after the renderer is initialised.
    void init(const std::string &fontPath);

    /// Filled rectangle centred on (centerX, centerY), with an outline when the brush has one.
    void drawRect(float centerX, float centerY, float width, float height, const Brush &brush);
    /// Line in the brush's outline colour and width.
    void drawLine(float x1, float y1, float x2, float y2, const Brush &brush);
    /// Text in the brush's fill colour; (x, baselineY) is the start of the baseline, size is the pixel height.
    void drawText(float x, float baselineY, float size, const std::string &text, const Brush &brush);

    /// Records everything drawn since the last flush. Must run inside the UI pass.
    void flush(float screenWidth, float screenHeight);
}
