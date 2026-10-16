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
`FAKEMC_RESIZE=WxH`, `FAKEMC_MINIMIZE=1`, `FAKEMC_MSAA=0` and `FAKEMC_FPS_CAP` set up the scene.

Not verified automatically: mouse/keyboard interaction (block breaking and
placing, menus, fullscreen toggle) and sound.

## World loading (2026-10-16)

Measured with the `[LOAD]` line the game prints once per world (spawn, render
distance 30 = 3,721 chunks, menu open, 16 hardware threads):

| | Before | After |
| --- | --- | --- |
| Nearest 9x9 chunks drawable | 0.68-0.79 s | 0.09-0.18 s |
| All 3,721 chunks drawable | 29.8-32.1 s | 2.1-3.2 s (depends on other load on the machine) |
| Meshes built | 7,441 (each chunk meshed twice) | 3,721 (once each) |
| Working set at spawn | 6.8 GB | 2.4-2.8 GB, flat while flying at 120 blocks/s |

What changed:

- The thread pool was never fed (the dispatcher checked the wrong queue), so
  every chunk was generated, lit and meshed on one thread. The chunk thread now
  only dispatches; up to 2x the worker count of builds run nearest-first on a
  pool with `hardware_concurrency - 2` workers.
- Each chunk has a `meshMutex` held while it is meshed, uploaded (try-lock from
  the render thread) or reset; it is never taken while holding `chunkMutex`.
- A chunk is lit when built and meshed once all four neighbours are lit (or out
  of range), so its one mesh already has their lava light. Border faces next to a
  missing neighbour derive its sky light from block data, which is what the
  neighbour computes itself. Only chunks that were meshed before a neighbour
  existed (camera moved) are remeshed, and only if lava light crosses the border.
- Sub-mesh buffers are per-thread scratch; merged meshes are freed after upload.
  Chunks above/below the single world layer share one read-only air block instead
  of 256 KB each. Unreferenced block data is freed every second.
- Mesher: a per-id lookup table replaces registry lookups in the face loops, and
  every pass stops at the chunk's highest non-air block. The sky-light pass stops
  at the first blocker. Generation fills columns in spans (checksums of generated
  chunks are byte-identical to before).

## Frame time (2026-10-16, 4x MSAA, 1280x720, uncapped)

A/B against the previous commit, same views, alternating runs:

| View | Before | After |
| --- | --- | --- |
| Spawn, forests to the horizon | 204-213 FPS | 490-520 FPS |
| Low over water next to a forest canopy | 194-202 FPS | ~370 FPS |

`FAKEMC_PROFILE=1` prints GPU time per pass (timestamp queries) and CPU time per
section every 2 s. At spawn the GPU frame went from 4.6 ms to ~1.4-1.7 ms:

- **Shadow map cache** (1.3 ms → ~0). Re-rendered only when the snapped light
  matrix changes (0.5° sun steps, 16-block camera steps), a caster near the
  camera is (re)uploaded or removed, or shadows are toggled. The quantized sun
  direction must stay exactly unit length, or float noise changes the matrix
  every frame.
- **Opaque/cutout split.** Leaves go in a separate alpha-tested mesh; opaque
  terrain uses a shader without `discard`, so hidden fragments are rejected
  before shading. Reflection clipping moved to `gl_ClipDistance`.
- **Fast leaves by default** (cutout 1.6 → 0.4 ms). Faces between two leaf blocks
  are skipped; "Fancy Leaves" in Video Settings restores them (remeshes the world).
- **Hardware PCF.** Four filtered depth-compare taps replace 16 point samples.

Not pursued: culling faces next to enclosed caves for distant chunks removed only
10% of triangles and made meshing 65% slower. The remaining cost is geometry
(~3.7 M opaque triangles per frame; quarter resolution barely changes it), so the
next levers are per-direction face culling with indirect draws, or downsampled
meshes for distant chunks.

## Distant terrain (2026-10-16)

Real (block) chunks now cover a radius of 16 chunks around the player (512
blocks). Beyond them, `src/World/Lod/LodTerrain` draws blocky heightfield tiles
out to 16,448 blocks, the distance Minecraft reaches at render distance 1028.

- **Tiles.** A quadtree of 32x32-sample tiles. Level 0 samples every 4 blocks,
  and each level doubles that up to level 8 (every 1,024 blocks). A tile splits
  while the camera is closer than 4x its size, so a cell spans ~4 px on screen at
  any distance (720p, 70� FOV). With 2.7x larger steps (1.5x) mountains read as
  giant cubes. Two worker threads build tiles
  nearest-first straight from the terrain function: height, biome, water and a
  raised canopy for forests. No block data exists for them. While a tile builds,
  its nearest built ancestor (or built children) draws in its place.
- **Colours.** Each biome's surface and canopy colours are the average colours
  of its block textures (`TextureManager::averageColor`). Canopies are darkened
  to 75% for the shadowed gaps between real leaves. Step walls keep the top
  colour, and only cliffs show rock, so hills don't stripe with dirt. Each cell
  gets �7% brightness noise in place of texture detail.
- **Seam with real chunks.** `Planet` writes a 128x128 mask of which chunks are
  drawn (chunk coordinate mod 128), and `lod.frag` discards fragments over them.
  LOD tiles are skipped only when they lie fully inside the real-chunk circle,
  less one chunk of margin, and every chunk under them is drawn. Otherwise
  chunks still loading at the front edge while moving leave a strip of sky. Tile edges carry
  skirts so neighbouring levels don't show cracks.
- **Fog.** Every world shader fades to the sky colour by horizontal distance,
  from 8% of the view distance (~1.3 km) to its end, as haze. The far plane sits at 1.5x the view
  distance. With "Distant Terrain" off (Video Settings), the fog ends at the
  real-chunk radius instead.
- **Water.** LOD water and real water share one model in `common.glsl`:
  the water colour mixed with the reflection map by a Schlick fresnel term. Real
  water is 71% opaque, so LOD water blends in its biome's sea-floor colour at the
  same ratio and only that part reflects; otherwise the real/LOD edge shows. The
  reflection pass draws real chunks within 8 chunks, then a coarser LOD cut
  (split at 1x tile size, ~16x fewer tiles) beyond them. Without it, water under
  distant shores reflected bare sky and showed as a bright band below the
  horizon. The coarse cut costs ~0.07 ms; the full-detail one cost 0.72 ms.
- **Not drawn far away:** trees as individual blocks, caves, structures and
  player edits.

Cost (4x MSAA, 1280x720, uncapped, `FAKEMC_PROFILE=1`), 260 blocks up looking
at the horizon: the distant pass takes 0.48 ms of a 1.45 ms GPU frame, and the
CPU frame is 1.66 ms (~600 FPS). The previous commit, with 30 real chunks and
~960 blocks visible, ran this view at ~387 FPS. The gain comes from drawing
fewer real chunks. A/B runs alternating both builds put the LOD build at
1.3-2x the previous commit across spawn and this view (the machine was noisy). `FAKEMC_RENDER_DISTANCE=N` sets the
real-chunk radius for tests.
