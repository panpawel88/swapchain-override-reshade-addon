# Swapchain Override Addon for ReShade

A minimal "Hello World" ReShade addon template. This serves as a starting point for creating custom ReShade addons.

## Features

- Minimal working ReShade addon structure
- Logs "Hello World" message on initialization
- Event callback system example
- Clean CMake build configuration
- Cross-platform compatible (Windows)

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

### 3. Install the Addon

After building, you'll find:
- `swapchain_override_addon64.dll` (or `.addon`) in the build directory

**Installation Steps:**

1. Locate your ReShade installation directory
2. Find the addon folder (usually in the same directory as the game executable or configured in ReShade.ini)
3. Copy the `.addon` file to the ReShade addon directory
4. Launch your game/application with ReShade

The addon will automatically be loaded by ReShade and will log "Hello World! ReShade addon initialized successfully." to the ReShade log.

## Project Structure

```
swapchain-override-addon/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── src/
│   ├── addon.cpp          # Addon metadata (NAME, DESCRIPTION)
│   └── main.cpp           # Main implementation with DllMain and callbacks
└── external/
    └── reshade/           # ReShade headers (git submodule)
```

## How It Works

### Addon Registration

The addon uses `DllMain` to register itself with ReShade:

```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        reshade::register_addon(hModule);
        reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init);
        // ... register other events
        break;
    // ...
    }
}
```

### Event Callbacks

The addon registers callbacks for ReShade events:

- `init_effect_runtime` - Called when ReShade initializes
- `destroy_effect_runtime` - Called when ReShade shuts down
- `present` - Called every frame (currently a placeholder)

## Extending the Addon

This template provides a foundation for building custom ReShade addons. You can:

1. **Add more event callbacks** - See ReShade documentation for available events
2. **Implement swapchain manipulation** - Override display modes, resolution, etc.
3. **Add ImGui UI** - Create custom overlay interfaces
4. **Hook graphics API calls** - Modify rendering pipeline

### Available ReShade Events

Common events you can hook:
- `reshade::addon_event::present` - Frame presentation
- `reshade::addon_event::init_effect_runtime` - Runtime initialization
- `reshade::addon_event::destroy_effect_runtime` - Runtime shutdown
- `reshade::addon_event::create_swapchain` - Swapchain creation
- And many more...

See the [ReShade API documentation](https://crosire.github.io/reshade-docs/) for a complete list.

## ReShade API Resources

- [Official ReShade Repository](https://github.com/crosire/reshade)
- [ReShade API Documentation](https://crosire.github.io/reshade-docs/)
- [ReShade Examples](https://github.com/crosire/reshade/tree/main/examples)
- [ReShade Website](https://reshade.me/)

## License

MIT License - Feel free to use this template for your own addons.

## Notes

- The addon architecture (32-bit vs 64-bit) must match your target application
- ReShade must be built with addon support enabled
- Check ReShade logs for addon loading status and any error messages
- The `.addon` file extension is required for ReShade to recognize and load the module
