# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a ReShade addon that intercepts swapchain creation to force a specific resolution while maintaining application compatibility through a proxy texture system. The addon allows games to run at higher resolutions than they natively support by transparently redirecting rendering to proxy textures at the application's requested resolution, then scaling to the actual swapchain.

## Build Commands

### Building the Addon

**64-bit build:**
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**32-bit build:**
```bash
cmake -B build32 -G "Visual Studio 17 2022" -A Win32
cmake --build build32 --config Release
```

**With Ninja:**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Build output:** The addon files are created at `build/swapchain_override_addon64.addon` (or `addon32` for 32-bit).

### Submodule Management

Before building, initialize ReShade submodule:
```bash
git submodule update --init --recursive
```

The build will fail if `external/reshade/include/reshade.hpp` is missing.

## Architecture Overview

### Core Components

1. **Swapchain Interception (main.cpp)**
   - `on_create_swapchain`: Intercepts swapchain creation requests and overrides resolution before creation
   - `on_init_swapchain`: Creates proxy textures/RTVs after swapchain is created
   - Stores original requested dimensions in `g_pending_swapchains` (keyed by HWND), then retrieves in `init_swapchain`

2. **Proxy Texture System**
   - Creates textures at the application's original requested resolution
   - Creates matching render target views (RTVs) and shader resource views (SRVs)
   - Maintains `SwapchainData` structure per-swapchain with proxy resources
   - Uses `g_swapchain_data` map (keyed by native swapchain handle) for resource tracking

3. **Render Target Redirection**
   - `on_bind_render_targets_and_depth_stencil`: Intercepts RTV binds and substitutes actual back buffer RTVs with proxy RTVs
   - Searches through all swapchains to match resource handles via `SwapchainData::find_proxy_index`
   - `on_bind_viewports` and `on_bind_scissor_rects`: Scales viewport/scissor dimensions to match proxy texture resolution

4. **Scaling and Present (on_present)**
   - Uses fullscreen draw with shaders instead of copy_texture_region for better scaling quality
   - Transitions proxy texture to shader_resource, actual back buffer to render_target
   - Binds copy pipeline with sampler and proxy SRV
   - Draws fullscreen triangle to scale proxy content to actual back buffer
   - Shaders: `fullscreen_vs.hlsl` (generates fullscreen triangle), `copy_ps.hlsl` (samples and copies)

5. **Pipeline Objects (created in on_init_swapchain)**
   - `copy_pipeline`: Graphics pipeline for scaled copy operation
   - `copy_pipeline_layout`: Descriptor layout (sampler in slot 0, SRV in slot 0)
   - `copy_sampler`: Sampler using configured filter mode

### Thread Safety

- All swapchain data access protected by `g_swapchain_mutex`
- Pending swapchain data protected by `g_pending_mutex`
- Critical for concurrent operations across multiple swapchains/threads

### Resource Management

- `SwapchainData` destructor calls `cleanup()` to destroy all GPU resources
- Resources destroyed in correct order: pipeline objects → resource views → resources
- Cleanup happens on swapchain destruction (not resize)
- All allocations/deletions in `on_init_swapchain` and `on_destroy_swapchain`

## Configuration System

Configuration read from `ReShade.ini` in the `[APP]` section:

- `ForceSwapchainResolution`: Format `<width>x<height>` (e.g., "3840x2160"), default 3840x2160
  - Parsed in `load_config()` using `std::strtoul` with 'x' separator
  - Set to "0x0" to disable override

- `SwapchainScalingFilter`: Integer 0-2, default 1
  - 0 = Point (min_mag_mip_point)
  - 1 = Linear (min_mag_mip_linear)
  - 2 = Anisotropic (min_mag_linear_mip_point)

Config is loaded once at DLL_PROCESS_ATTACH.

## Shader Compilation

- HLSL shaders in `shaders/` directory compiled via CMake custom commands
- Uses `fxc.exe` (DirectX shader compiler) to generate `.cso` bytecode files
- CMake script `cmake/GenerateShaderHeader.cmake` embeds bytecode into `shader_bytecode.h`
- Bytecode arrays (`fullscreen_vs`, `copy_ps`) referenced in pipeline creation
- Shaders target shader model 4.0 (`vs_4_0`, `ps_4_0`)

## ReShade API Event Flow

1. **Application requests swapchain** → `create_swapchain` event
2. **Override resolution** → Store original dims in `g_pending_swapchains`
3. **Swapchain created** → `init_swapchain` event
4. **Create proxy resources** → Retrieve original dims from `g_pending_swapchains`
5. **Application renders** → `bind_render_targets_and_depth_stencil`, `bind_viewports`, `bind_scissor_rects` events
6. **Redirect to proxy** → Substitute RTVs, scale viewports/scissors
7. **Application presents** → `present` event
8. **Scale to actual** → Fullscreen draw from proxy SRV to back buffer RTV
9. **Swapchain destroyed** → `destroy_swapchain` event → Cleanup

## Important Implementation Details

### Handle Comparison Strategy
- **RTV interception**: Compares underlying resource handles (via `get_resource_from_view`) instead of RTV handles directly
- This is critical because the application creates its own RTVs for back buffers, but we need to detect when those RTVs reference the actual (overridden) back buffer resources
- See commit 5102150 for the fix that switched from RTV handle comparison to resource handle comparison

### Viewport/Scissor Scaling
- Uses tolerance-based matching (90% of forced resolution) to detect full-screen viewports/scissors
- Scales dimensions using ratio: `original_size / actual_size`
- Only modifies viewports/scissors that match the forced resolution (leaves others untouched)

### State Transitions
- Proxy textures: `render_target` → `shader_resource` → `render_target`
- Back buffers: `present` → `render_target` → `present`
- Proper state management ensures compatibility with ReShade effects and other addons

### Architecture Detection
- CMake sets output name suffix based on pointer size: `addon64` vs `addon32`
- Post-build step copies `.dll` to `.addon` (required extension for ReShade)
- Addon architecture must match target application architecture

## Common Development Tasks

### Adding New Configuration Options
1. Add field to `SwapchainConfig` struct
2. Parse in `load_config()` using `reshade::get_config_value`/`reshade::set_config_value`
3. Use value in relevant event callback

### Modifying Scaling Behavior
- Edit `copy_ps.hlsl` for pixel shader logic (currently simple texture sample)
- Modify sampler creation in `on_init_swapchain` for filter mode changes
- Pipeline is created per-swapchain, so changes require swapchain recreation

### Debugging
- ReShade log messages use `reshade::log::message` with severity levels
- Check log for "Swapchain override: Requested size..." to verify override activation
- "Redirected back buffer RTV to proxy RTV" indicates successful RTV substitution (debug level)
- Debug logging at key points: config load, swapchain init, RTV redirection, present

## File Structure

- `src/main.cpp`: Core addon implementation (all event callbacks, DllMain)
- `src/addon.cpp`: Addon metadata exports (NAME, DESCRIPTION)
- `shaders/`: HLSL shader sources for scaling operation
- `external/reshade/`: ReShade headers (git submodule)
- `cmake/GenerateShaderHeader.cmake`: Converts compiled shaders to C++ header
- `CMakeLists.txt`: Build configuration with shader compilation and addon packaging
