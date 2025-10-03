/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include <reshade.hpp>

using namespace reshade::api;

// Callback function called on every frame presentation
static void on_present(command_queue* queue, swapchain* swapchain, const rect*, const rect*, uint32_t, const rect*)
{
    // This is called every frame - you can add frame-based logic here
    // For now, this is just a placeholder for the hello world addon
}

// Callback function called when the addon is initialized with an effect runtime
static void on_init(effect_runtime* runtime)
{
    // Log a hello world message when the addon initializes
    reshade::log_message(reshade::log_level::info, "Hello World! ReShade addon initialized successfully.");
}

// Callback function called when the addon is about to be unloaded
static void on_destroy(effect_runtime* runtime)
{
    // Log when the addon is shutting down
    reshade::log_message(reshade::log_level::info, "Hello World addon is shutting down.");
}

// DLL entry point - required for all ReShade addons
BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Register this module as a ReShade addon
        if (!reshade::register_addon(hModule))
            return FALSE;

        // Register event callbacks
        reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy);
        reshade::register_event<reshade::addon_event::present>(on_present);
        break;

    case DLL_PROCESS_DETACH:
        // Unregister event callbacks when the addon is being unloaded
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(on_init);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(on_destroy);
        reshade::unregister_event<reshade::addon_event::present>(on_present);

        // Unregister the addon
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
