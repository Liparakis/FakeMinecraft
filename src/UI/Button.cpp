#include "Button.h"

Button::Button(float x, float y, float width, float height, std::string label)
    : x(x), y(y), width(width), height(height), label(std::move(label)) {
}

void Button::draw() const {
    ui::Brush brush;
    brush.fill_color[0] = 0.5f;
    brush.fill_color[1] = 0.5f;
    brush.fill_color[2] = 0.5f; // Grey background
    brush.fill_opacity = 1.0f;
    brush.outline_opacity = 1.0f;
    brush.outline_width = 2.0f;
    
    // Draw background
    ui::drawRect(x, y, width, height, brush);
    
    // Draw text (centered)
    ui::Brush textBrush;
    textBrush.fill_color[0] = 1.0f;
    textBrush.fill_color[1] = 1.0f;
    textBrush.fill_color[2] = 1.0f; // White text
    
    // Simple centering approximation
    float textX = x - (label.length() * 4.0f); 
    float textY = y + 4.0f; // Offset for baseline
    
    ui::drawText(textX, textY, 15.0f, label.c_str(), textBrush);
}

bool Button::isHovered(float mouseX, float mouseY) const {
    float halfW = width / 2.0f;
    float halfH = height / 2.0f;
    return (mouseX >= x - halfW && mouseX <= x + halfW &&
            mouseY >= y - halfH && mouseY <= y + halfH);
}

void Button::handleClick(float mouseX, float mouseY, std::function<void()> onClick) {
    if (isHovered(mouseX, mouseY) && onClick) {
        onClick();
    }
}
