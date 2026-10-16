/**
 * @file Slider.cpp
 * @brief Implementation of a UI slider for adjusting values.
 */
#include "Slider.h"
#include <algorithm>
#include "Canvas.h"

/**
 * @brief Renders the slider bar and handle.
 */
void Slider::draw() const {
    ui::Brush brush;
    brush.outline_opacity = 0.0f;

    brush.fill_color[0] = 0.6f;
    brush.fill_color[1] = 0.6f;
    brush.fill_color[2] = 0.6f;

    ui::drawRect(x + width / 2, y, width, height, brush);

    brush.outline_opacity = 1.0f;
    brush.outline_width = 3.0f;
    brush.outline_color[0] = 0.2f;
    brush.outline_color[1] = 0.2f;
    brush.outline_color[2] = 0.2f;

    ui::drawRect(x + width / 2, y, width, height, brush);
    float handleX = x + (currentValue - minValue) / (maxValue - minValue) * width;

    brush.outline_opacity = 0.0f;
    brush.fill_color[0] = 0.4f;
    brush.fill_color[1] = 0.3f;
    brush.fill_color[2] = 0.1f;

    ui::drawRect(handleX, y, height * 1.0f, height * 1.0f, brush);
    brush.outline_opacity = 1.0f;
    brush.outline_width = 3.0f;
    brush.outline_color[0] = 0.1f;
    brush.outline_color[1] = 0.05f;
    brush.outline_color[2] = 0.0f;

    ui::drawRect(handleX, y, height * 1.0f, height * 1.0f, brush);
}


/**
 * @brief Updates the slider's value based on mouse movement while dragging.
 */
void Slider::update(float mouseX, int& value) {
    if (isDragging) {
        mouseX = std::max(x, std::min(x + width, mouseX));
        currentValue = minValue + (mouseX - x) / width * (maxValue - minValue);
        value = currentValue;
    }
}

bool Slider::isMouseOverHandle(float mouseX, float mouseY) const {
    float handleX = x + (currentValue - minValue) / (maxValue - minValue) * width;
    return (mouseX >= handleX - height / 2 && mouseX <= handleX + height / 2 &&
            mouseY >= y - height / 2 && mouseY <= y + height * 1.5f);
}

void Slider::startDragging(float mouseX, float mouseY) {
    if (isMouseOverHandle(mouseX, mouseY)) {
        isDragging = true;
    }
}

void Slider::stopDragging() {
    isDragging = false;
}

void Slider::setDimensions(float x, float y) {
    this->x = x;
    this->y = y;
}