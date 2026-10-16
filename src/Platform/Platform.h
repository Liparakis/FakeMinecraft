/**
 * @file Platform.h
 * @brief Window, input, timing and audio on top of SDL2.
 */
#pragma once

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <string>

namespace platform {
    struct MouseState {
        bool leftPressed = false, middlePressed = false, rightPressed = false; ///< Went down this frame.
        bool leftDown = false;   ///< Currently held.
        int x = 0, y = 0;        ///< Cursor position in window pixels, origin top-left.
        int relX = 0, relY = 0;  ///< Motion since the previous frame.
    };

    bool init(int width, int height, const char *title);
    void shutdown();

    /// Processes pending events; returns false once the window was closed or quit() was called.
    bool pumpEvents();
    void quit();

    [[nodiscard]] SDL_Window *window();
    [[nodiscard]] glm::ivec2 drawableSize();
    /// True once after the window's drawable size changed.
    bool consumeResized();

    [[nodiscard]] bool keyDown(SDL_Scancode key);
    [[nodiscard]] const MouseState &mouse();
    void setRelativeMouse(bool enabled);

    /// Milliseconds the previous frame took, and since init.
    [[nodiscard]] float deltaMs();
    [[nodiscard]] float timeMs();
    [[nodiscard]] float fps();
    /// Sleeps so frames take at least 1000/fps ms; fps <= 0 disables the cap.
    void limitFrameRate(int fps);

    void setTitle(const char *title);
    void setFullscreen(bool fullscreen);

    void playSound(const std::string &path, float volume);
    void playMusic(const std::string &path, float volume, bool loop);
}
