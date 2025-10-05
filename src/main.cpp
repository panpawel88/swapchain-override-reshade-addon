/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include <reshade.hpp>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <mutex>
#include <string>

using namespace reshade::api;

// ============================================================================
// Configuration
// ============================================================================

struct SwapchainConfig
{
    uint32_t force_width = 0;
    uint32_t force_height = 0;
    filter_mode scaling_filter = filter_mode::min_mag_mip_linear;
};

static SwapchainConfig g_config;

static void load_config()
{
    // Read forced resolution
    char resolution_string[32] = {};
    size_t resolution_string_size = sizeof(resolution_string);

    if (reshade::get_config_value(nullptr, "APP", "ForceSwapchainResolution", resolution_string, &resolution_string_size))
    {
        char* resolution_p = resolution_string;
        const unsigned long width = std::strtoul(resolution_p, &resolution_p, 10);
        const char width_terminator = *resolution_p++;
        const unsigned long height = std::strtoul(resolution_p, &resolution_p, 10);

        if (width != 0 && height != 0 && width_terminator == 'x')
        {
            g_config.force_width = static_cast<uint32_t>(width);
            g_config.force_height = static_cast<uint32_t>(height);
        }
    }
    else
    {
        // Set default config value
        reshade::set_config_value(nullptr, "APP", "ForceSwapchainResolution", "3840x2160");
        g_config.force_width = 3840;
        g_config.force_height = 2160;
    }

    // Read scaling filter
    int filter_value = 1; // Default to linear
    if (reshade::get_config_value(nullptr, "APP", "SwapchainScalingFilter", filter_value))
    {
        switch (filter_value)
        {
        case 0:
            g_config.scaling_filter = filter_mode::min_mag_mip_point;
            break;
        case 1:
            g_config.scaling_filter = filter_mode::min_mag_mip_linear;
            break;
        case 2:
            g_config.scaling_filter = filter_mode::min_mag_linear_mip_point;
            break;
        default:
            g_config.scaling_filter = filter_mode::min_mag_mip_linear;
            break;
        }
    }
    else
    {
        reshade::set_config_value(nullptr, "APP", "SwapchainScalingFilter", 1);
    }
}

// ============================================================================
// Swapchain Data Structure
// ============================================================================

struct SwapchainData
{
    uint32_t original_width = 0;
    uint32_t original_height = 0;
    uint32_t actual_width = 0;
    uint32_t actual_height = 0;
    bool override_active = false;

    std::vector<resource> proxy_textures;
    std::vector<resource_view> proxy_rtvs;
    std::vector<resource_view> actual_rtvs;  // Actual back buffer RTVs for comparison

    device* device_ptr = nullptr;

    // Helper: Find proxy RTV index from actual RTV
    int find_proxy_index(resource_view actual_rtv) const
    {
        for (size_t i = 0; i < actual_rtvs.size(); ++i)
        {
            if (actual_rtvs[i].handle == actual_rtv.handle)
                return static_cast<int>(i);
        }
        return -1;
    }

    ~SwapchainData()
    {
        cleanup();
    }

    void cleanup()
    {
        if (device_ptr != nullptr)
        {
            // Destroy proxy resource views
            for (auto rtv : proxy_rtvs)
            {
                if (rtv.handle != 0)
                    device_ptr->destroy_resource_view(rtv);
            }

            // Destroy actual back buffer RTVs
            for (auto rtv : actual_rtvs)
            {
                if (rtv.handle != 0)
                    device_ptr->destroy_resource_view(rtv);
            }

            // Destroy proxy resources
            for (auto tex : proxy_textures)
            {
                if (tex.handle != 0)
                    device_ptr->destroy_resource(tex);
            }
        }

        proxy_rtvs.clear();
        actual_rtvs.clear();
        proxy_textures.clear();
    }
};

// Type aliases for clarity
using SwapchainNativeHandle = uint64_t;  // Native swapchain handle (IDXGISwapChain*, VkSwapchainKHR, etc.)
using WindowHandle = void*;              // Window handle (HWND, etc.)

// Global map to track swapchain data (keyed by native swapchain handle)
static std::unordered_map<SwapchainNativeHandle, SwapchainData*> g_swapchain_data;
static std::mutex g_swapchain_mutex;

// Temporary storage for original dimensions (keyed by window handle)
// Used to pass data from create_swapchain to init_swapchain
struct PendingSwapchainInfo
{
    uint32_t original_width;
    uint32_t original_height;
};
static std::unordered_map<WindowHandle, PendingSwapchainInfo> g_pending_swapchains;
static std::mutex g_pending_mutex;

// ============================================================================
// Event Callbacks
// ============================================================================

static bool on_create_swapchain(device_api api, swapchain_desc& desc, void* hwnd)
{
    // If override is disabled (0,0), don't modify
    if (g_config.force_width == 0 || g_config.force_height == 0)
        return false;

    // Check if the requested size differs from our desired size
    const uint32_t requested_width = desc.back_buffer.texture.width;
    const uint32_t requested_height = desc.back_buffer.texture.height;

    if (requested_width == g_config.force_width && requested_height == g_config.force_height)
    {
        // Sizes match, no override needed
        return false;
    }

    // Store the original requested size in pending map (keyed by window handle)
    // We'll retrieve this in init_swapchain
    if (hwnd != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        PendingSwapchainInfo info;
        info.original_width = requested_width;
        info.original_height = requested_height;
        g_pending_swapchains[static_cast<WindowHandle>(hwnd)] = info;
    }

    // Override the swapchain description
    desc.back_buffer.texture.width = g_config.force_width;
    desc.back_buffer.texture.height = g_config.force_height;

    reshade::log::message(reshade::log::level::info,
        ("Swapchain override: Requested size " + std::to_string(requested_width) + "x" + std::to_string(requested_height) +
        " -> Forced size " + std::to_string(g_config.force_width) + "x" + std::to_string(g_config.force_height)).c_str());

    return true; // Indicate we modified the description
}

static void on_init_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr)
        return;

    // Skip if override is disabled
    if (g_config.force_width == 0 || g_config.force_height == 0)
        return;

    // Check if we have pending data for this swapchain's window
    // This indicates we actually modified the swapchain in on_create_swapchain
    WindowHandle hwnd = swapchain_ptr->get_hwnd();
    bool was_modified = false;

    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        was_modified = (hwnd != nullptr && g_pending_swapchains.find(hwnd) != g_pending_swapchains.end());
    }

    if (!was_modified)
        return; // This swapchain wasn't modified, no override needed

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    device* device_ptr = swapchain_ptr->get_device();
    if (device_ptr == nullptr)
        return;

    // Get swapchain information
    const uint32_t back_buffer_count = swapchain_ptr->get_back_buffer_count();
    if (back_buffer_count == 0)
        return;

    // Get the actual swapchain back buffer
    resource actual_back_buffer = swapchain_ptr->get_back_buffer(0);
    resource_desc actual_desc = device_ptr->get_resource_desc(actual_back_buffer);

    // Create or retrieve swapchain data
    SwapchainData* data = nullptr;
    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();

    auto it = g_swapchain_data.find(swapchain_handle);
    if (it != g_swapchain_data.end())
    {
        // Clean up existing resources on resize
        data = it->second;
        data->cleanup();
    }
    else
    {
        // Create new swapchain data
        data = new SwapchainData();
        g_swapchain_data[swapchain_handle] = data;
    }

    data->device_ptr = device_ptr;
    data->actual_width = actual_desc.texture.width;
    data->actual_height = actual_desc.texture.height;
    data->override_active = true;

    // Retrieve the original requested size from pending map
    bool found_original_size = false;

    if (hwnd != nullptr)
    {
        std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
        auto pending_it = g_pending_swapchains.find(hwnd);
        if (pending_it != g_pending_swapchains.end())
        {
            data->original_width = pending_it->second.original_width;
            data->original_height = pending_it->second.original_height;
            found_original_size = true;

            // Remove from pending map as we've consumed it
            g_pending_swapchains.erase(pending_it);
        }
    }

    // Fallback if we couldn't retrieve original size (shouldn't happen in normal flow)
    if (!found_original_size)
    {
        reshade::log::message(reshade::log::level::warning,
            "Could not retrieve original swapchain dimensions, using 1920x1080 as fallback");
        data->original_width = 1920;
        data->original_height = 1080;
    }

    // Create proxy textures at original resolution
    data->proxy_textures.resize(back_buffer_count);
    data->proxy_rtvs.resize(back_buffer_count);
    data->actual_rtvs.resize(back_buffer_count);

    for (uint32_t i = 0; i < back_buffer_count; ++i)
    {
        // Create proxy texture descriptor (same format as actual back buffer)
        resource_desc proxy_desc = actual_desc;
        proxy_desc.texture.width = data->original_width;
        proxy_desc.texture.height = data->original_height;
        proxy_desc.usage = resource_usage::render_target | resource_usage::copy_source | resource_usage::shader_resource;

        // Create the proxy texture
        if (!device_ptr->create_resource(proxy_desc, nullptr, resource_usage::render_target, &data->proxy_textures[i]))
        {
            reshade::log::message(reshade::log::level::error,
                ("Failed to create proxy texture " + std::to_string(i)).c_str());
            data->cleanup();
            return;
        }

        // Create render target view for the proxy texture
        resource_view_desc rtv_desc = {};
        rtv_desc.type = resource_view_type::texture_2d;
        rtv_desc.format = actual_desc.texture.format;
        rtv_desc.texture.first_level = 0;
        rtv_desc.texture.level_count = 1;

        if (!device_ptr->create_resource_view(data->proxy_textures[i], resource_usage::render_target, rtv_desc, &data->proxy_rtvs[i]))
        {
            reshade::log::message(reshade::log::level::error,
                ("Failed to create proxy RTV " + std::to_string(i)).c_str());
            data->cleanup();
            return;
        }
    }

    // Get actual back buffer RTVs for comparison during bind interception
    for (uint32_t i = 0; i < back_buffer_count; ++i)
    {
        resource back_buffer_resource = swapchain_ptr->get_back_buffer(i);

        // Get the default RTV for the actual back buffer
        resource_view_desc rtv_desc = {};
        rtv_desc.type = resource_view_type::texture_2d;
        rtv_desc.format = actual_desc.texture.format;
        rtv_desc.texture.first_level = 0;
        rtv_desc.texture.level_count = 1;

        if (!device_ptr->create_resource_view(back_buffer_resource, resource_usage::render_target, rtv_desc, &data->actual_rtvs[i]))
        {
            reshade::log::message(reshade::log::level::error,
                ("Failed to create actual back buffer RTV " + std::to_string(i)).c_str());
            data->cleanup();
            return;
        }
    }

    reshade::log::message(reshade::log::level::info,
        ("Created " + std::to_string(back_buffer_count) + " proxy textures at " +
        std::to_string(data->original_width) + "x" + std::to_string(data->original_height)).c_str());
}

static void on_bind_render_targets_and_depth_stencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    if (cmd_list == nullptr || rtvs == nullptr || count == 0)
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    // Create a modifiable copy of RTVs
    std::vector<resource_view> modified_rtvs(rtvs, rtvs + count);
    bool needs_rebind = false;

    // Check each RTV being bound
    for (uint32_t i = 0; i < count; ++i)
    {
        if (modified_rtvs[i].handle == 0)
            continue;

        // Search through all swapchains to find if this RTV matches an actual back buffer RTV
        for (auto& pair : g_swapchain_data)
        {
            SwapchainData* data = pair.second;
            if (!data->override_active)
                continue;

            // Check if this device matches
            if (data->device_ptr != device_ptr)
                continue;

            // Find if this RTV matches any actual back buffer RTV
            int proxy_index = data->find_proxy_index(modified_rtvs[i]);
            if (proxy_index >= 0)
            {
                // Substitute with proxy RTV
                modified_rtvs[i] = data->proxy_rtvs[proxy_index];
                needs_rebind = true;

                reshade::log::message(reshade::log::level::debug,
                    ("Redirected back buffer RTV to proxy RTV " + std::to_string(proxy_index)).c_str());
                break; // Found and substituted, move to next RTV
            }
        }
    }

    // If we modified any RTVs, rebind with the proxy RTVs
    if (needs_rebind)
    {
        cmd_list->bind_render_targets_and_depth_stencil(count, modified_rtvs.data(), dsv);
    }
}

static void on_bind_viewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    if (cmd_list == nullptr || viewports == nullptr || count == 0)
        return;

    // Skip if override is disabled
    if (g_config.force_width == 0 || g_config.force_height == 0)
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    // Find if we have an active override for this device
    SwapchainData* active_data = nullptr;
    for (auto& pair : g_swapchain_data)
    {
        if (pair.second->override_active && pair.second->device_ptr == device_ptr)
        {
            active_data = pair.second;
            break;
        }
    }

    if (active_data == nullptr)
        return; // No active override for this device

    // Calculate scaling factors
    const float scale_x = static_cast<float>(active_data->original_width) / static_cast<float>(active_data->actual_width);
    const float scale_y = static_cast<float>(active_data->original_height) / static_cast<float>(active_data->actual_height);

    bool needs_rebind = false;

    // Create a mutable copy
    std::vector<viewport> modified_viewports(viewports, viewports + count);

    for (uint32_t i = 0; i < count; ++i)
    {
        // Check if viewport dimensions match the forced size (or close to it)
        // We use a tolerance because viewport might be slightly different
        const bool matches_forced_width = (modified_viewports[i].width >= active_data->actual_width * 0.9f);
        const bool matches_forced_height = (modified_viewports[i].height >= active_data->actual_height * 0.9f);

        if (matches_forced_width && matches_forced_height)
        {
            // Scale viewport to original size
            modified_viewports[i].x *= scale_x;
            modified_viewports[i].y *= scale_y;
            modified_viewports[i].width *= scale_x;
            modified_viewports[i].height *= scale_y;
            needs_rebind = true;
        }
    }

    if (needs_rebind)
    {
        // Rebind with the modified viewports
        cmd_list->bind_viewports(first, count, modified_viewports.data());
    }
}

static void on_bind_scissor_rects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    if (cmd_list == nullptr || rects == nullptr || count == 0)
        return;

    // Skip if override is disabled
    if (g_config.force_width == 0 || g_config.force_height == 0)
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    // Find if we have an active override for this device
    SwapchainData* active_data = nullptr;
    for (auto& pair : g_swapchain_data)
    {
        if (pair.second->override_active && pair.second->device_ptr == device_ptr)
        {
            active_data = pair.second;
            break;
        }
    }

    if (active_data == nullptr)
        return; // No active override for this device

    // Calculate scaling factors
    const float scale_x = static_cast<float>(active_data->original_width) / static_cast<float>(active_data->actual_width);
    const float scale_y = static_cast<float>(active_data->original_height) / static_cast<float>(active_data->actual_height);

    bool needs_rebind = false;

    // Create a mutable copy
    std::vector<rect> modified_rects(rects, rects + count);

    for (uint32_t i = 0; i < count; ++i)
    {
        const int32_t width = modified_rects[i].right - modified_rects[i].left;
        const int32_t height = modified_rects[i].bottom - modified_rects[i].top;

        // Check if scissor rect dimensions match the forced size (or close to it)
        const bool matches_forced_width = (width >= static_cast<int32_t>(active_data->actual_width * 0.9f));
        const bool matches_forced_height = (height >= static_cast<int32_t>(active_data->actual_height * 0.9f));

        if (matches_forced_width && matches_forced_height)
        {
            // Scale scissor rect to original size
            modified_rects[i].left = static_cast<int32_t>(modified_rects[i].left * scale_x);
            modified_rects[i].top = static_cast<int32_t>(modified_rects[i].top * scale_y);
            modified_rects[i].right = static_cast<int32_t>(modified_rects[i].right * scale_x);
            modified_rects[i].bottom = static_cast<int32_t>(modified_rects[i].bottom * scale_y);
            needs_rebind = true;
        }
    }

    if (needs_rebind)
    {
        // Rebind with the modified scissor rects
        cmd_list->bind_scissor_rects(first, count, modified_rects.data());
    }
}

static void on_present(command_queue* queue, swapchain* swapchain_ptr, const rect*, const rect*, uint32_t, const rect*)
{
    if (swapchain_ptr == nullptr || queue == nullptr)
        return;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    auto it = g_swapchain_data.find(swapchain_handle);

    if (it == g_swapchain_data.end() || !it->second->override_active)
        return; // No override for this swapchain

    SwapchainData* data = it->second;
    device* device_ptr = swapchain_ptr->get_device();

    if (device_ptr == nullptr)
        return;

    // Get current back buffer index
    const uint32_t current_index = swapchain_ptr->get_current_back_buffer_index();
    if (current_index >= data->proxy_textures.size())
        return;

    resource proxy_texture = data->proxy_textures[current_index];
    resource actual_back_buffer = swapchain_ptr->get_back_buffer(current_index);

    if (proxy_texture.handle == 0 || actual_back_buffer.handle == 0)
        return;

    // Get an immediate command list to perform the copy
    command_list* cmd_list = queue->get_immediate_command_list();
    if (cmd_list == nullptr)
        return;

    // Barrier: Transition proxy texture to copy source
    resource_usage proxy_old_state = resource_usage::render_target;
    resource_usage proxy_new_state = resource_usage::copy_source;
    cmd_list->barrier(1, &proxy_texture, &proxy_old_state, &proxy_new_state);

    // Barrier: Transition actual back buffer to copy dest
    resource_usage dest_old_state = resource_usage::present;
    resource_usage dest_new_state = resource_usage::copy_dest;
    cmd_list->barrier(1, &actual_back_buffer, &dest_old_state, &dest_new_state);

    // Copy with scaling from proxy to actual back buffer
    cmd_list->copy_texture_region(
        proxy_texture, 0, nullptr,  // Source: entire proxy texture
        actual_back_buffer, 0, nullptr,  // Dest: entire actual back buffer
        g_config.scaling_filter
    );

    // Barrier: Transition resources back
    proxy_old_state = resource_usage::copy_source;
    proxy_new_state = resource_usage::render_target;
    cmd_list->barrier(1, &proxy_texture, &proxy_old_state, &proxy_new_state);

    dest_old_state = resource_usage::copy_dest;
    dest_new_state = resource_usage::present;
    cmd_list->barrier(1, &actual_back_buffer, &dest_old_state, &dest_new_state);
}

static void on_destroy_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr || is_resize)
        return; // Don't destroy on resize, only on actual destruction

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    auto it = g_swapchain_data.find(swapchain_handle);

    if (it != g_swapchain_data.end())
    {
        delete it->second;
        g_swapchain_data.erase(it);

        reshade::log::message(reshade::log::level::info, "Cleaned up swapchain override data");
    }
}

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
        load_config();

        // Register event callbacks
        reshade::register_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
        reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
        reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
        reshade::register_event<reshade::addon_event::present>(on_present);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        reshade::log::message(reshade::log::level::info, "Swapchain Override addon loaded");
        break;

    case DLL_PROCESS_DETACH:
        // Clean up all swapchain data
        {
            std::lock_guard<std::mutex> lock(g_swapchain_mutex);
            for (auto& pair : g_swapchain_data)
            {
                delete pair.second;
            }
            g_swapchain_data.clear();
        }

        // Clean up pending swapchains map
        {
            std::lock_guard<std::mutex> lock(g_pending_mutex);
            g_pending_swapchains.clear();
        }

        // Unregister event callbacks
        reshade::unregister_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
        reshade::unregister_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
        reshade::unregister_event<reshade::addon_event::present>(on_present);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        // Unregister the addon
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
