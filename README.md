## _Fake Minecraft_ ![](https://geps.dev/progress/90)

A 3D voxel-based game inspired by Minecraft, rendered with Vulkan on top of SDL2. This project aims to recreate the core features of Minecraft, including terrain generation, block placement, and more, while allowing for experimentation with custom mechanics and optimizations.

## Features
- _Voxel Terrain: Infinite terrain generation using procedural algorithms._
- _Block Placement & Destruction: Players can interact with the environment by placing and removing blocks._
- _25 Biomes: oceans, rivers, beaches, plains, forests, taiga, snowy biomes, desert, savanna, badlands, swamp, jungle and mountain peaks, on continuous terrain with no cliffs at biome borders._
- _Vulkan Renderer: dynamic shadows, water reflections, 4x MSAA and a day/night cycle._
- _Distant Terrain: the world is visible out to ~16 km (Minecraft render distance 1028). Blocky low-detail terrain fills in beyond the real chunks and fades into the sky._

## Tech

The tools bellow were used to create my game:

- [C++] -  Visual Studio Tools? 🤔
- [Math] - Read a book or something? 🤔
- [Tears] 😭
## Installation (Windows x64 Exclusively)

- [CMake](https://cmake.org/download/) 3.20+ and a MinGW-w64 GCC toolchain (CLion's bundled MinGW works). MSVC is not supported.
- The [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) with `VULKAN_SDK` set (the installer does this). It provides the headers, the loader library and `glslc`.
- A GPU with Vulkan 1.3.

```bat
cmake -S . -B cmake-build-release-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release-mingw
```

The game lands in `build/game/CPP_GAME.exe` (run it from that folder). A Debug build enables the Vulkan validation layer; in Release set `FAKEMC_VALIDATION=1` to turn it on.

Dunno i just use CLion and you should too. Fuck Visual Studio...

## Credits

- Block textures: [Faithful 64x](https://github.com/Faithful-Resource-Pack/Faithful-64x-Java), fetched by `tools/fetch_faithful_textures.py`.
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT), [stb_image / stb_truetype](https://github.com/nothings/stb) (public domain), [OpenSimplexNoise](https://github.com/deerel/OpenSimplexNoise), [SDL2](https://www.libsdl.org/), [glm](https://github.com/g-truc/glm).

## License

MIT
**Free Software, Hell Yeah!**
