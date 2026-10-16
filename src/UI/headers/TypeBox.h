/**
 * @file TypeBox.h
 * @brief Header for the TypeBox UI element.
 */
#pragma once
#include <string>
#include <utility>
#include "Canvas.h"

/**
 * @class TypeBox
 * @brief A box where the player can type in text (like for a seed).
 */
class TypeBox {
private:
    std::string text;
    float x, y, width, height;
    bool isActive;

public:
    TypeBox() = default;
    TypeBox(float x, float y, float width, float height);

    void draw() const;
    void handleInput();

    void processKey(int scancode);

    static float approximateTextWidth(const std::string &text, float fontSize);

    void setActive(bool active);
    const std::string& getText() const;
    void setText(std::string str) { text = std::move(str); }
    void clear();
    void setDimensions(float x, float y);
};