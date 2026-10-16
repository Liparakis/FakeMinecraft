# Vulkan + MinGW port

Goal: the game renders through Vulkan only, builds with MinGW GCC only, and no
longer depends on SGG, OpenGL or GLEW. Gameplay, world generation and the look
of every pass (shadows, reflections, water, billboards, sky, UI) stay the same.

Done when: a clean MinGW build runs at spawn and the screenshot matches
`reference_gl_spawn.png` in content (terrain, water reflection, shadows, menu),
with no Vulkan validation errors in a debug build.

## Layers

| Module | Replaces | Owns |
| --- | --- | --- |
| `src/Platform/Platform.*` | SGG window, input, timing, audio | SDL2 window, event pump, keyboard/mouse state, frame timing and FPS cap, SDL_mixer sounds and music |
| `src/Renderer/Vulkan/VkContext.*` | GL context | instance, device, swapchain, frames in flight, VMA, buffers/images, deferred destruction, one-shot uploads |
| `src/Renderer/Renderer.*` | per-pass GL state in GameObject/Planet/Sky | render targets, descriptor sets, pipelines, the frame's passes |
| `src/UI/Canvas.*` | `graphics::drawRect/drawText/drawLine/Brush` | 2D batch with a stb_truetype font atlas |

## Decisions

- **Vulkan 1.3 core**: dynamic rendering and synchronization2, so there are no
  render pass or framebuffer objects. Target GPU reports 1.4.
- **Clip-space conventions stay GL-like.** The bundled glm (0.9.6) has no
  zero-to-one depth mode, so game code keeps GL projections and every matrix
  handed to a shader goes through `toVulkanClip` (z' = z/2 + w/2). The reverse-Z
  trick (near=10000, far=0.1, depth test GREATER_OR_EQUAL, clear 0) still yields
  depth 1 at the eye and 0 far away.
  Every 3D pass uses a negative-height viewport, so winding (front face CW) and
  screen orientation match GL. Offscreen targets sampled later (shadow map,
  reflection) are therefore stored top-down; shaders flip `v` when they sample them.
- **Memory**: VMA. Chunk meshes go in host-visible, persistently mapped buffers
  (`VMA_MEMORY_USAGE_AUTO` + sequential write), so uploads are a memcpy on the
  render thread with no staging copy. Textures use a staging buffer and a
  one-shot command buffer.
- **Lifetime**: 2 frames in flight. Buffers released while a frame may still use
  them go to that frame slot's deletion queue and are destroyed after its fence.
- **Vertex formats** are re-laid out to 16 bytes (world/fluid) and 20 bytes
  (billboard) so every attribute is naturally aligned; the constructors the
  mesher calls are unchanged.
- **Descriptors**: set 0 = frame UBO + texture array + shadow map + reflection
  (one set for the main pass, one for the reflection pass, per frame). Set 1 =
  one sampled texture (sun, moon, font atlas). Shadow pipelines use push
  constants only.
- **Pass order per frame**: shadow → reflection → main (sky, world, billboards,
  water, outline) with optional 4x MSAA resolved into the swapchain image → UI.
- **Dropped**: hardware occlusion queries. They added a bounding-box draw per
  chunk and read back results; frustum and distance culling stay.
- **Shaders**: GLSL 450 compiled to SPIR-V by `glslc` at build time.

## Build

CLion's bundled MinGW GCC + Ninja. SDL2, SDL2_mixer and their codec DLLs are
linked directly from `dependencies/bin`; Vulkan from the SDK's `vulkan-1.lib`
(found through `VULKAN_SDK`).

## Verification (2026-10-16, RX 6700 XT, 1280x720, spawn, 3.7k chunks loaded)

| Check | Result |
| --- | --- |
| Validation layer, Release + `FAKEMC_VALIDATION=1`, menu and playing | 0 messages |
| Debug build (validation on), resize to 1000x600 with MSAA off, clean exit | 0 messages, exit code 0 (no VMA leak asserts) |
| FPS uncapped, menu open (same view as the GL baseline) | median 181 (166-187); GL build: 108 (98-125) |
| FPS uncapped, playing | median 179 (169-202) |
| Working set | 6.8 GB; GL build: 7.0 GB |

The first Vulkan build ran at 33 FPS: chunk meshes lived in host-visible
memory, and about 65% of them fell back to system RAM once the 256 MB
mappable VRAM heap filled (measured with an allocation probe). Meshes are now
device-local and filled by staging copies recorded before the first pass.

Automated checks run the game in the background without taking focus:
`FAKEMC_SCREENSHOT=<file.bmp>` saves a frame after `FAKEMC_SCREENSHOT_AT`
seconds and quits; `FAKEMC_SCREENSHOT_PLAYING`, `FAKEMC_CAMERA="x,y,z,yaw,pitch"`,
`FAKEMC_RESIZE=WxH`, `FAKEMC_MSAA=0` and `FAKEMC_FPS_CAP` set up the scene.

Not verified automatically: mouse/keyboard interaction (block breaking and
placing, menus, fullscreen toggle) and sound.
