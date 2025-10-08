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

    // Pending swapchain info management
    void store_pending_info(WindowHandle hwnd, uint32_t width, uint32_t height);
    bool retrieve_pending_info(WindowHandle hwnd, uint32_t& out_width, uint32_t& out_height);

    // Swapchain resource management
    bool initialize_swapchain(reshade::api::swapchain* swapchain_ptr);
    void destroy_swapchain(SwapchainNativeHandle swapchain_handle);

    // Query methods
    SwapchainData* get_data(SwapchainNativeHandle swapchain_handle);
    SwapchainData* find_active_data_for_device(reshade::api::device* device_ptr);

    // Cleanup
    void cleanup_all();

    // Mutex accessors (needed by event handlers)
    std::mutex& get_swapchain_mutex() { return swapchain_mutex_; }

private:
    SwapchainManager() = default;
    ~SwapchainManager() = default;

    // Delete copy/move constructors
    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;
    SwapchainManager(SwapchainManager&&) = delete;
    SwapchainManager& operator=(SwapchainManager&&) = delete;

    // Helper methods
    bool create_proxy_resources(SwapchainData* data, reshade::api::swapchain* swapchain_ptr);
    bool create_copy_pipeline(SwapchainData* data, reshade::api::format format);

    // Data storage
    std::unordered_map<SwapchainNativeHandle, SwapchainData*> swapchain_data_;
    std::mutex swapchain_mutex_;

    std::unordered_map<WindowHandle, PendingSwapchainInfo> pending_swapchains_;
    std::mutex pending_mutex_;
};
