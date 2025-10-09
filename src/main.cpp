/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

// Include imgui.h BEFORE common.h to ensure IMGUI_VERSION_NUM is defined
// when reshade::register_addon() initializes the ImGui function table
#define ImTextureID ImU64
#include <imgui.h>

#include "common.h"
#include "config.h"
#include "window_hooks.h"
#include "swapchain_manager.h"
#include "overlay.h"

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Register this module as a ReShade addon
        if (!reshade::register_addon(hModule))
            return FALSE;

        // Load configuration
        Config::get_instance().load();

        // Install WinAPI hooks if borderless fullscreen mode is enabled
        WindowHooks::get_instance().install();

        // Register event callbacks
        SwapchainManager::get_instance().install();

        // Register debug overlay
        OverlayManager::get_instance().install();

        reshade::log::message(reshade::log::level::info, "Swapchain Override addon loaded");
        break;

    case DLL_PROCESS_DETACH:
        // Unregister debug overlay
        OverlayManager::get_instance().uninstall();

        // Uninstall WinAPI hooks
        WindowHooks::get_instance().uninstall();

        // Clean up all swapchain data
        SwapchainManager::get_instance().cleanup_all();

        // Unregister event callbacks
        SwapchainManager::get_instance().uninstall();

        // Unregister the addon
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
