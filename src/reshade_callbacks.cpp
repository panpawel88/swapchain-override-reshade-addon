/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "reshade_callbacks.h"
#include "config.h"
#include "swapchain_manager.h"

using namespace reshade::api;

bool on_create_swapchain(device_api api, swapchain_desc& desc, void* hwnd)
{
    return SwapchainManager::get_instance().handle_create_swapchain(api, desc, hwnd);
}

void on_init_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    SwapchainManager::get_instance().handle_init_swapchain(swapchain_ptr, is_resize);
}

void on_bind_render_targets_and_depth_stencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    SwapchainManager::get_instance().handle_bind_render_targets(cmd_list, count, rtvs, dsv);
}

void on_bind_viewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    SwapchainManager::get_instance().handle_bind_viewports(cmd_list, first, count, viewports);
}

void on_bind_scissor_rects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    SwapchainManager::get_instance().handle_bind_scissor_rects(cmd_list, first, count, rects);
}

void on_present(command_queue* queue, swapchain* swapchain_ptr, const rect*, const rect*, uint32_t, const rect*)
{
    SwapchainManager::get_instance().handle_present(queue, swapchain_ptr);
}

bool on_set_fullscreen_state(swapchain* swapchain_ptr, bool fullscreen, void* hmonitor)
{
    return SwapchainManager::get_instance().handle_set_fullscreen_state(swapchain_ptr, fullscreen, hmonitor);
}

void on_destroy_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    SwapchainManager::get_instance().handle_destroy_swapchain(swapchain_ptr, is_resize);
}
