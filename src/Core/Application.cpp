/**
 * @file Application.cpp
 * @brief Entry point for the game.
 *
 * This file contains the main function which starts our engine by
 * initializing the central GameObject.
 */
#include "GameObject.h"
#include <exception>
#include <iostream>

int main() {
    try {
        GameObject &gObject = GameObject::getInstance(1280, 720, "");
        gObject.init();
    } catch (const std::exception &e) {
        // Window builds have no console, so fatal errors are also shown in a dialog.
        std::cerr << e.what() << std::endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fake Minecraft", e.what(), nullptr);
        return 1;
    }
    return 0;
}
