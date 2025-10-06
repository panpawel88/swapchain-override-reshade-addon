# Swapchain Override Addon for ReShade

![Build Status](https://github.com/panpawel88/swapchain-override-reshade-addon/actions/workflows/build.yml/badge.svg)
![Release](https://github.com/panpawel88/swapchain-override-reshade-addon/actions/workflows/release.yml/badge.svg)

A ReShade addon that forces a specific swapchain resolution while maintaining application compatibility through proxy textures. This allows you to run games at higher resolutions than they natively support.

> **⚠️ Development Status:** This project is still under active development and not fully functional. Some features may not work as expected or may cause issues with certain games. Use at your own risk.

## Use Cases

- **Legacy Games** - Run older games that don't support modern high resolutions (4K, 8K)
- **Resolution Unlocking** - Force games with hardcoded resolution limits to render at higher resolutions
- **Fullscreen Mode Control** - Force borderless or exclusive fullscreen regardless of game settings
- **Multi-Monitor Gaming** - Force borderless fullscreen on a specific monitor
- **Upscaling Testing** - Test how games look at different resolutions without native support
- **Screenshot Enhancement** - Capture high-resolution screenshots from games with limited resolution options
- **Supersampling** - Force higher internal rendering resolution for improved image quality

## Features

- **Forced Resolution Override** - Override swapchain resolution to any configured size (default 4K)
- **Fullscreen Mode Override** - Force exclusive fullscreen or borderless fullscreen mode
- **Multi-Monitor Support** - Configure which monitor to use for borderless fullscreen
- **Application Compatibility** - Uses proxy textures so applications render unaware of resolution changes
- **Automatic Scaling** - Scales proxy textures to actual swapchain on present
- **Configurable Settings** - Customize resolution, fullscreen mode, and scaling filter via ReShade.ini
- **Thread-Safe** - Proper mutex protection for concurrent operations
- **Clean Resource Management** - Automatic cleanup of proxy textures and render targets
- **Automated CI/CD** - GitHub Actions for builds and releases

## Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (MSVC, GCC, or Clang)
- Git (for submodules)
- ReShade with addon support installed

## Building

### 1. Clone and Initialize Submodules

```bash
git clone <your-repo-url>
cd swapchain-override-addon
git submodule update --init --recursive
```

### 2. Build with CMake

#### Windows (MSVC)

```bash
# 64-bit build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 32-bit build
cmake -B build32 -G "Visual Studio 17 2022" -A Win32
cmake --build build32 --config Release
```

#### Windows (Ninja)

```bash
# 64-bit build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 32-bit build (if needed)
cmake -B build32 -G "Ninja Multi-Config" -A Win32
cmake --build build32 --config Release
```

### 3. Download Pre-built Releases (Recommended)

You can download pre-built `.addon` files from the [Releases](https://github.com/panpawel88/swapchain-override-reshade-addon/releases) page.

### 4. Install the Addon

After building or downloading, you'll find:
- `swapchain_override_addon64.addon` for 64-bit applications
- `swapchain_override_addon32.addon` for 32-bit applications

**Installation Steps:**

1. Locate your ReShade installation directory
2. Find the addon folder (usually in the same directory as the game executable or configured in ReShade.ini)
3. Copy the `.addon` file to the ReShade addon directory
4. Configure the addon settings in `ReShade.ini` (see Configuration section below)
5. Launch your game/application with ReShade

The addon will automatically be loaded by ReShade and override the swapchain resolution according to your configuration.

## Configuration

Add these settings to your `ReShade.ini` file under the `[APP]` section:

```ini
[APP]
# Resolution Override
ForceSwapchainResolution=3840x2160
SwapchainScalingFilter=1

# Fullscreen Mode Override
FullscreenMode=0
BlockFullscreenChanges=0
TargetMonitor=0
```

### Configuration Options

#### Resolution Override

**ForceSwapchainResolution**
- Format: `<width>x<height>`
- Default: `3840x2160` (4K)
- Example values:
  - `1920x1080` - Full HD
  - `2560x1440` - QHD
  - `3840x2160` - 4K UHD
  - `7680x4320` - 8K UHD
  - `0x0` - Disable override

**SwapchainScalingFilter**
- Type: Integer (0-2)
- Default: `1` (Linear)
- Values:
  - `0` - Point sampling (nearest neighbor, sharp but pixelated)
  - `1` - Linear filtering (smooth scaling, recommended)
  - `2` - Anisotropic filtering (highest quality for textures)

#### Fullscreen Mode Override

**FullscreenMode**
- Type: Integer (0-2)
- Default: `0` (Unchanged)
- Values:
  - `0` - Unchanged (no fullscreen mode override, default behavior)
  - `1` - Borderless (force borderless fullscreen / windowed fullscreen)
  - `2` - Exclusive (force exclusive fullscreen)
- **Note:** Borderless mode uses WinAPI hooks and may conflict with anti-cheat systems or other overlays

**BlockFullscreenChanges**
- Type: Boolean (0 or 1)
- Default: `0` (Disabled)
- Values:
  - `0` - Allow application to change fullscreen state at runtime
  - `1` - Block all fullscreen state changes (prevents Alt+Enter toggles)
- **Use case:** Prevent games from changing fullscreen mode when you want to maintain a specific mode

**TargetMonitor**
- Type: Integer (0+)
- Default: `0` (Primary monitor)
- Values:
  - `0` - Primary monitor
  - `1` - First secondary monitor
  - `2` - Second secondary monitor
  - etc.
- **Note:** Only applies when `FullscreenMode=1` (Borderless). Falls back to primary if specified monitor doesn't exist
- **Monitor order:** Monitors are enumerated left-to-right as they appear in Windows display settings

## Project Structure

```
swapchain-override-addon/
├── .github/
│   └── workflows/
│       ├── build.yml      # CI workflow for automated builds
│       └── release.yml    # Release workflow for creating releases
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── src/
│   ├── addon.cpp          # Addon metadata (NAME, DESCRIPTION)
│   └── main.cpp           # Main implementation with DllMain and callbacks
└── external/
    └── reshade/           # ReShade headers (git submodule)
```

## How It Works

This addon intercepts the swapchain creation process and implements a transparent proxy texture system:

### 1. Swapchain Creation Override

When an application requests a swapchain at its original resolution, the addon:
- Captures the original requested dimensions (e.g., 1920x1080)
- Overrides the swapchain creation to use the configured resolution (e.g., 3840x2160)
- Creates the actual swapchain at the higher resolution

### 2. Proxy Texture System

To maintain application compatibility:
- Creates proxy textures at the application's original requested size
- Creates matching render target views (RTVs) for the proxy textures
- The application renders to these proxy textures, completely unaware of the override

### 3. Automatic Scaling on Present

Every frame, before presentation:
- Transitions proxy textures to the appropriate resource state
- Scales and copies the proxy texture content to the actual swapchain buffer
- Uses the configured scaling filter (point, linear, or anisotropic)
- Transitions resources back for the next frame

### 4. Resource Management

- Per-swapchain data structures track proxy textures and original dimensions
- Automatic cleanup when swapchains are destroyed
- Thread-safe operations with mutex protection
- Proper handling of edge cases (e.g., when requested size matches desired size)

### Technical Details

**Events Hooked:**
- `init_swapchain` - Creates proxy textures and RTVs after swapchain creation
- `create_swapchain` - Overrides resolution before swapchain creation
- `destroy_swapchain` - Cleans up proxy textures and associated resources
- `present` - Performs scaling and copy operations each frame

**Resource States:**
- Proxy textures are transitioned to `resource_usage::copy_source`
- Swapchain back buffers are transitioned to `resource_usage::copy_dest`
- Proper state restoration ensures compatibility with ReShade effects

## ReShade API Resources

- [Official ReShade Repository](https://github.com/crosire/reshade)
- [ReShade API Documentation](https://crosire.github.io/reshade-docs/)
- [ReShade Examples](https://github.com/crosire/reshade/tree/main/examples)
- [ReShade Website](https://reshade.me/)

## License

MIT License - Feel free to use this template for your own addons.

## Notes & Limitations

- The addon architecture (32-bit vs 64-bit) must match your target application
- ReShade must be built with addon support enabled
- Performance impact depends on resolution difference and scaling filter used
- Some games may not work properly if they have hardcoded resolution assumptions
- The addon only affects swapchain resolution, not game UI scaling
- Check ReShade logs for addon loading status and any error messages
- The `.addon` file extension is required for ReShade to recognize and load the module

### Fullscreen Mode Override Limitations

- **Borderless fullscreen mode uses WinAPI hooks** which may:
  - Conflict with anti-cheat systems (may be detected as suspicious)
  - Conflict with other overlays or recording software
  - Not work with games using non-standard window creation
- **Borderless fullscreen typically has**:
  - Slightly higher input latency than exclusive fullscreen
  - DWM composition overhead (unavoidable on Windows 10+)
  - Better compatibility with overlays and Alt+Tab
- **Exclusive fullscreen** provides best performance but less compatibility with overlays
- Monitor enumeration order may vary between systems for multi-monitor setups

## Troubleshooting

**Game crashes or fails to start:**
- Try lowering the forced resolution
- Ensure the `.addon` file architecture matches your game (32-bit vs 64-bit)
- Check ReShade logs for error messages

**Performance issues:**
- Lower the forced resolution
- Try using point sampling (filter value 0) instead of linear/anisotropic
- Ensure your GPU has sufficient VRAM for the higher resolution buffers

**Game UI is too small:**
- This addon doesn't scale UI elements, only the rendering resolution
- Some games have separate UI scaling options you may need to adjust

**Resolution not changing:**
- Verify `ReShade.ini` is in the correct location and properly formatted
- Check that the configuration is under the `[APP]` section
- Restart the application after changing configuration

**Fullscreen mode not working:**
- For borderless mode: Check ReShade logs for WinAPI hook installation messages
- Some games with custom window management may resist borderless modifications
- Anti-cheat systems may block WinAPI hooks - try exclusive mode instead
- Ensure `FullscreenMode` is set to `1` (borderless) or `2` (exclusive), not `0`

**Game crashes when using borderless mode:**
- Try exclusive fullscreen mode (`FullscreenMode=2`) instead
- Disable the fullscreen override (`FullscreenMode=0`) and use only resolution override
- Check for conflicts with other overlays or recording software
