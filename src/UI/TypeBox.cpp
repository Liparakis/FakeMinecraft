/**
 * @file TypeBox.cpp
 * @brief Implementation of a text input box.
 */
#include "TypeBox.h"
#include "../Platform/Platform.h"
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <ostream>


TypeBox::TypeBox(float x, float y, float width, float height)
    : x(x), y(y), width(width), height(height), isActive(false), text("") {
}

void TypeBox::draw() const {
    ui::Brush br;
    br.outline_opacity = 1.0f;
    br.fill_opacity = isActive ? 0.7f : 0.3f;
    br.fill_color[0] = 0.2f;
    br.fill_color[1] = 0.2f;
    br.fill_color[2] = 0.2f;

    // Draw the TypeBox background
    ui::drawRect(x, y, width, height, br);

    // Draw the text
    br.fill_opacity = 1.0f;
    br.fill_color[0] = 1.0f;
    br.fill_color[1] = 1.0f;
    br.fill_color[2] = 1.0f;

    float textX = x - width / 2 + 5;
    ui::drawText(textX, y + 5, 16, text, br);
}

/**
 * @brief Handles keyboard input for typing and backspacing.
 * 
 * It includes a small delay so that holding a key doesn't type 
 * too many characters at once.
 */
void TypeBox::handleInput() {
    static std::unordered_map<int, float> keyPressTime;
    static std::unordered_map<int, bool> keyState;
    static bool enterPressed = false;

    const float initialDelay = 500.0f; // Wait half a second before repeating
    const float spamInterval = 2.0f;   // How fast to repeat after the delay

    float currentTime = platform::timeMs();

    if (platform::keyDown(SDL_SCANCODE_RETURN)) {
        if (!enterPressed) {
            enterPressed = true;
            isActive = !isActive;
        }
    } else {
        enterPressed = false;
    }

    if (isActive) {
        for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode) {
            bool isPressed = platform::keyDown(static_cast<SDL_Scancode>(scancode));

            if (isPressed) {
                if (!keyState[scancode]) {
                    keyState[scancode] = true;
                    keyPressTime[scancode] = currentTime;
                    processKey(scancode);
                } else {
                    float elapsedTime = currentTime - keyPressTime[scancode];
                    if (elapsedTime >= initialDelay &&
                        static_cast<int>((elapsedTime - initialDelay) / spamInterval) >
                        static_cast<int>((elapsedTime - initialDelay - spamInterval) / spamInterval)) {
                        processKey(scancode);
                    }
                }
            } else {
                keyState[scancode] = false;
                keyPressTime[scancode] = 0.0f;
            }
        }
    }
}

void TypeBox::processKey(int scancode) {
    float textWidth = approximateTextWidth(text, 14); // Use the approximation
    float maxTextWidth = width - 10; // Leave some padding

    if (scancode == SDL_SCANCODE_BACKSPACE && !text.empty()) {
        text.pop_back();
    }

    if (textWidth >= maxTextWidth) {
        return; // Prevent adding more characters if width limit is reached
    }

    // Handle key presses
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        char c = 'a' + (scancode - SDL_SCANCODE_A);
        text += c;
    } else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        char c = '1' + (scancode - SDL_SCANCODE_1);
        text += c;
    } else if (scancode == SDL_SCANCODE_SPACE) {
        text += ' ';
    }
}

/**
 * @brief Calculates how wide the text is to make sure it fits in the box.
 */
float TypeBox::approximateTextWidth(const std::string &text, float fontSize) {
    const float averageCharWidth = fontSize * 0.6f;
    return averageCharWidth * text.length();
}

void TypeBox::setActive(bool active) {
    isActive = active;
}

const std::string &TypeBox::getText() const {
    return text;
}

void TypeBox::clear() {
    text.clear();
}

void TypeBox::setDimensions(float x, float y) {
    this->x = x;
    this->y = y;
}
