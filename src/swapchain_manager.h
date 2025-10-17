/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"

// Pending swapchain info structure (used to pass data from create to init)
struct PendingSwapchainInfo
{
    uint32_t original_width;
    uint32_t original_height;
};

// Swapchain data structure (per-swapchain resource management)
struct SwapchainData
{
    uint32_t original_width = 0;
    uint32_t original_height = 0;
    uint32_t actual_width = 0;
    uint32_t actual_height = 0;
    bool override_active = false;

    std::vector<reshade::api::resource> proxy_textures;
    std::vector<reshade::api::resource_view> proxy_rtvs;
    std::vector<reshade::api::resource_view> proxy_srvs;  // Shader resource views for proxy textures
    std::vector<reshade::api::resource> actual_back_buffers;  // Actual back buffer resources for comparison

    // Pipeline objects for fullscreen draw
    reshade::api::pipeline copy_pipeline = {};
    reshade::api::pipeline_layout copy_pipeline_layout = {};
    reshade::api::sampler copy_sampler = {};

    reshade::api::device* device_ptr = nullptr;

    // Helper: Find proxy RTV index from actual back buffer resource
    int find_proxy_index(reshade::api::resource actual_resource) const;

    ~SwapchainData();

    void cleanup();
};

class SwapchainManager
{
public:
    // Singleton access
    static SwapchainManager& get_instance();

    // Install/uninstall ReShade event callbacks
    void install();
    void uninstall();

    // Cleanup
    void cleanup_all();

    // Query methods for overlay/debugging (public, thread-safe)
    template<typename Func>
    void for_each_swapchain(Func callback) const
    {
        std::lock_guard<std::mutex> lock(swapchain_mutex_);
        for (const auto& [handle, data] : swapchain_data_)
        {
            if (data != nullptr)
            {
                callback(handle, *data);
            }
        }
    }

private:
    SwapchainManager() = default;
    ~SwapchainManager() = default;

    // Delete copy/move constructors
    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;
    SwapchainManager(SwapchainManager&&) = delete;
    SwapchainManager& operator=(SwapchainManager&&) = delete;

    // Pending swapchain info management (internal)
    void store_pending_info(WindowHandle hwnd, uint32_t width, uint32_t height);
    bool retrieve_pending_info(WindowHandle hwnd, uint32_t& out_width, uint32_t& out_height);

    // Swapchain resource management (internal)
    bool initialize_swapchain(reshade::api::swapchain* swapchain_ptr);
    void destroy_swapchain(SwapchainNativeHandle swapchain_handle);

    // Query methods (internal)
    SwapchainData* get_data(SwapchainNativeHandle swapchain_handle);
    SwapchainData* find_active_data_for_device(reshade::api::device* device_ptr);

    // High-level event handlers (internal - called by static callback wrappers)
    void handle_init_device(reshade::api::device* device);
    bool handle_create_swapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);
    void handle_init_swapchain(reshade::api::swapchain* swapchain_ptr, bool is_resize);
    void handle_bind_render_targets(reshade::api::command_list* cmd_list, uint32_t count,
                                     const reshade::api::resource_view* rtvs, reshade::api::resource_view dsv);
    void handle_bind_viewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                               const reshade::api::viewport* viewports);
    void handle_bind_scissor_rects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                                    const reshade::api::rect* rects);
    void handle_present(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain_ptr);
    void handle_finish_present(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain_ptr);
    bool handle_set_fullscreen_state(reshade::api::swapchain* swapchain_ptr, bool fullscreen, void* hmonitor);
    void handle_destroy_swapchain(reshade::api::swapchain* swapchain_ptr, bool is_resize);

    // Helper methods
    bool create_proxy_resources(SwapchainData* data, reshade::api::swapchain* swapchain_ptr);
    bool create_copy_pipeline(SwapchainData* data, reshade::api::format format);

    // ReShade event callback implementations (static wrappers)
    static void on_init_device(reshade::api::device* device);
    static bool on_create_swapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);
    static void on_init_swapchain(reshade::api::swapchain* swapchain_ptr, bool is_resize);
    static void on_bind_render_targets_and_depth_stencil(reshade::api::command_list* cmd_list, uint32_t count,
                                                          const reshade::api::resource_view* rtvs, reshade::api::resource_view dsv);
    static void on_bind_viewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                                   const reshade::api::viewport* viewports);
    static void on_bind_scissor_rects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                                       const reshade::api::rect* rects);
    static void on_present(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain_ptr,
                           const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect,
                           uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);
    static void on_finish_present(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain_ptr);
    static bool on_set_fullscreen_state(reshade::api::swapchain* swapchain_ptr, bool fullscreen, void* hmonitor);
    static void on_destroy_swapchain(reshade::api::swapchain* swapchain_ptr, bool is_resize);

    // Data storage
    std::unordered_map<SwapchainNativeHandle, std::unique_ptr<SwapchainData>> swapchain_data_;
    mutable std::mutex swapchain_mutex_;

    std::unordered_map<WindowHandle, PendingSwapchainInfo> pending_swapchains_;
    mutable std::mutex pending_mutex_;
};
