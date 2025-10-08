/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"

// ReShade event callback declarations
// These are the functions that get registered with ReShade's event system

bool on_create_swapchain(
    reshade::api::device_api api,
    reshade::api::swapchain_desc& desc,
    void* hwnd);

void on_init_swapchain(
    reshade::api::swapchain* swapchain_ptr,
    bool is_resize);

void on_bind_render_targets_and_depth_stencil(
    reshade::api::command_list* cmd_list,
    uint32_t count,
    const reshade::api::resource_view* rtvs,
    reshade::api::resource_view dsv);

void on_bind_viewports(
    reshade::api::command_list* cmd_list,
    uint32_t first,
    uint32_t count,
    const reshade::api::viewport* viewports);

void on_bind_scissor_rects(
    reshade::api::command_list* cmd_list,
    uint32_t first,
    uint32_t count,
    const reshade::api::rect* rects);

void on_present(
    reshade::api::command_queue* queue,
    reshade::api::swapchain* swapchain_ptr,
    const reshade::api::rect* source_rect,
    const reshade::api::rect* dest_rect,
    uint32_t dirty_rect_count,
    const reshade::api::rect* dirty_rects);

bool on_set_fullscreen_state(
    reshade::api::swapchain* swapchain_ptr,
    bool fullscreen,
    void* hmonitor);

void on_destroy_swapchain(
    reshade::api::swapchain* swapchain_ptr,
    bool is_resize);
