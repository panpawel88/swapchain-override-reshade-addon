/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "common.h"
#include "config.h"
#include "window_hooks.h"
#include "swapchain_manager.h"
#include "event_handlers.h"

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
        reshade::register_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
        reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
        reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
        reshade::register_event<reshade::addon_event::present>(on_present);
        reshade::register_event<reshade::addon_event::set_fullscreen_state>(on_set_fullscreen_state);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        reshade::log::message(reshade::log::level::info, "Swapchain Override addon loaded");
        break;

    case DLL_PROCESS_DETACH:
        // Uninstall WinAPI hooks
        WindowHooks::get_instance().uninstall();

        // Clean up all swapchain data
        SwapchainManager::get_instance().cleanup_all();

        // Unregister event callbacks
        reshade::unregister_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
        reshade::unregister_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
        reshade::unregister_event<reshade::addon_event::present>(on_present);
        reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(on_set_fullscreen_state);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        // Unregister the addon
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
