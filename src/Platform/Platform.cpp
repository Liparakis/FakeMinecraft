#include "Platform.h"

#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_syswm.h>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace platform {
    namespace {
        SDL_Window *gWindow = nullptr;
        bool gRunning = true;
        bool gResized = false;
        bool gAudio = false;
        MouseState gMouse;

        Uint64 gStartTicks = 0, gFrameStart = 0;
        float gDeltaMs = 16.0f;
        float gFps = 0.0f;
        int gFpsFrames = 0;
        float gFpsAccumMs = 0.0f;

        std::unordered_map<std::string, Mix_Chunk *> gSounds;
        Mix_Music *gMusic = nullptr;

        float ticksToMs(const Uint64 ticks) {
            return static_cast<float>(static_cast<double>(ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));
        }
    }

    bool init(const int width, const int height, const char *title) {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
            std::cerr << "[Platform] SDL_Init failed: " << SDL_GetError() << std::endl;
            return false;
        }

        // Automated screenshot runs must not steal focus from whatever the user is doing,
        // so the window starts hidden and is shown without activation.
        const bool background = std::getenv("FAKEMC_SCREENSHOT") != nullptr;
        gWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                   (background ? SDL_WINDOW_HIDDEN : 0));
        if (!gWindow) {
            std::cerr << "[Platform] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            return false;
        }
        if (background) {
            SDL_SysWMinfo info;
            SDL_VERSION(&info.version);
            if (SDL_GetWindowWMInfo(gWindow, &info)) {
                ShowWindow(info.info.win.window, SW_SHOWNOACTIVATE);
                SetWindowPos(info.info.win.window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }

        // Audio is optional: the game still runs on machines without an output device.
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
            Mix_Init(MIX_INIT_OGG);
            Mix_AllocateChannels(32);
            gAudio = true;
        } else {
            std::cerr << "[Platform] Audio disabled: " << Mix_GetError() << std::endl;
        }

        gStartTicks = gFrameStart = SDL_GetPerformanceCounter();
        return true;
    }

    void shutdown() {
        if (gAudio) {
            for (auto &[path, chunk] : gSounds) Mix_FreeChunk(chunk);
            gSounds.clear();
            if (gMusic) Mix_FreeMusic(gMusic);
            Mix_CloseAudio();
            Mix_Quit();
        }
        if (gWindow) SDL_DestroyWindow(gWindow);
        gWindow = nullptr;
        SDL_Quit();
    }

    bool pumpEvents() {
        const Uint64 now = SDL_GetPerformanceCounter();
        gDeltaMs = ticksToMs(now - gFrameStart);
        gFrameStart = now;

        gFpsAccumMs += gDeltaMs;
        ++gFpsFrames;
        if (gFpsAccumMs >= 250.0f) {
            gFps = static_cast<float>(gFpsFrames) * 1000.0f / gFpsAccumMs;
            gFpsFrames = 0;
            gFpsAccumMs = 0.0f;
        }

        gMouse.leftPressed = gMouse.middlePressed = gMouse.rightPressed = false;
        gMouse.relX = gMouse.relY = 0;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    gRunning = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) gResized = true;
                    break;
                case SDL_MOUSEMOTION:
                    gMouse.relX += e.motion.xrel;
                    gMouse.relY += e.motion.yrel;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) gMouse.leftPressed = true;
                    if (e.button.button == SDL_BUTTON_MIDDLE) gMouse.middlePressed = true;
                    if (e.button.button == SDL_BUTTON_RIGHT) gMouse.rightPressed = true;
                    break;
                default:
                    break;
            }
        }

        const Uint32 buttons = SDL_GetMouseState(&gMouse.x, &gMouse.y);
        gMouse.leftDown = (buttons & SDL_BUTTON_LMASK) != 0;
        return gRunning;
    }

    void quit() { gRunning = false; }

    SDL_Window *window() { return gWindow; }

    glm::ivec2 drawableSize() {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(gWindow, &w, &h);
        return {w, h};
    }

    bool consumeResized() {
        const bool r = gResized;
        gResized = false;
        return r;
    }

    bool keyDown(const SDL_Scancode key) { return SDL_GetKeyboardState(nullptr)[key] != 0; }

    const MouseState &mouse() { return gMouse; }

    void setRelativeMouse(const bool enabled) { SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE); }

    float deltaMs() { return gDeltaMs; }

    float timeMs() { return ticksToMs(SDL_GetPerformanceCounter() - gStartTicks); }

    float fps() { return gFps; }

    void limitFrameRate(const int fps) {
        if (fps <= 0) return;
        const float targetMs = 1000.0f / static_cast<float>(fps);
        float elapsed = ticksToMs(SDL_GetPerformanceCounter() - gFrameStart);
        // SDL_Delay is coarse, so sleep most of the gap and spin the last millisecond.
        if (targetMs - elapsed > 2.0f) SDL_Delay(static_cast<Uint32>(targetMs - elapsed - 1.0f));
        while (elapsed < targetMs) elapsed = ticksToMs(SDL_GetPerformanceCounter() - gFrameStart);
    }

    void setTitle(const char *title) { SDL_SetWindowTitle(gWindow, title); }

    void setFullscreen(const bool fullscreen) {
        SDL_SetWindowFullscreen(gWindow, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }

    void playSound(const std::string &path, const float volume) {
        if (!gAudio) return;
        auto it = gSounds.find(path);
        if (it == gSounds.end()) {
            Mix_Chunk *chunk = Mix_LoadWAV(path.c_str());
            if (!chunk) {
                std::cerr << "[Platform] Failed to load sound " << path << ": " << Mix_GetError() << std::endl;
                return;
            }
            it = gSounds.emplace(path, chunk).first;
        }
        Mix_VolumeChunk(it->second, static_cast<int>(volume * MIX_MAX_VOLUME));
        Mix_PlayChannel(-1, it->second, 0);
    }

    void playMusic(const std::string &path, const float volume, const bool loop) {
        if (!gAudio) return;
        if (gMusic) Mix_FreeMusic(gMusic);
        gMusic = Mix_LoadMUS(path.c_str());
        if (!gMusic) {
            std::cerr << "[Platform] Failed to load music " << path << ": " << Mix_GetError() << std::endl;
            return;
        }
        Mix_VolumeMusic(static_cast<int>(volume * MIX_MAX_VOLUME));
        Mix_PlayMusic(gMusic, loop ? -1 : 1);
    }
}
