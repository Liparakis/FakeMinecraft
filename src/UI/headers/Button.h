/**
 * @file Button.h
 * @brief Header for the Button UI element.
 */
#pragma once

#include "Canvas.h"
#include <string>
#include <functional>

/**
 * @class Button
 * @brief A clickable button with text.
 */
class Button {
private:
    float x, y;             // Center position
    float width, height;    // Dimensions
    std::string label;      // Text label

public:
    Button() : x(0), y(0), width(0), height(0) {}
    Button(float x, float y, float width, float height, std::string label);
    
    void draw() const;
    void handleClick(float mouseX, float mouseY, std::function<void()> onClick);
    bool isHovered(float mouseX, float mouseY) const;
    
    void setLabel(const std::string& newLabel) { label = newLabel; }
    void setPosition(float newX, float newY) { x = newX; y = newY; }
};
