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
    const Config& config = Config::get_instance();
    bool modified = false;

    // Handle resolution override
    if (config.is_resolution_override_enabled())
    {
        const uint32_t requested_width = desc.back_buffer.texture.width;
        const uint32_t requested_height = desc.back_buffer.texture.height;

        if (requested_width != config.get_force_width() || requested_height != config.get_force_height())
        {
            // Store the original requested size in pending map (keyed by window handle)
            if (hwnd != nullptr)
            {
                SwapchainManager::get_instance().store_pending_info(
                    static_cast<WindowHandle>(hwnd),
                    requested_width,
                    requested_height);
            }

            // Override the swapchain description
            desc.back_buffer.texture.width = config.get_force_width();
            desc.back_buffer.texture.height = config.get_force_height();

            reshade::log::message(reshade::log::level::info,
                ("Swapchain override: Requested size " + std::to_string(requested_width) + "x" + std::to_string(requested_height) +
                " -> Forced size " + std::to_string(config.get_force_width()) + "x" + std::to_string(config.get_force_height())).c_str());

            modified = true;
        }
    }

    // Handle fullscreen mode override
    if (config.is_exclusive_fullscreen_enabled())
    {
        // Don't force fullscreen during creation (causes DXGI_ERROR_INVALID_CALL due to 0/0 refresh rate)
        // Instead, we'll transition to fullscreen after creation in on_init_swapchain

        // Enable mode switching flag to allow fullscreen transition later (DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 0x2)
        constexpr uint32_t DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 0x2;
        if ((desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) == 0)
        {
            reshade::log::message(reshade::log::level::info, "Enabling mode switching for exclusive fullscreen transition");
            desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            modified = true;
        }
    }
    else if (config.is_borderless_fullscreen_enabled())
    {
        if (desc.fullscreen_state)
        {
            reshade::log::message(reshade::log::level::info, "Forcing borderless fullscreen mode (windowed)");
            desc.fullscreen_state = false;
            modified = true;
        }

        // Remove mode switching flag for borderless (we want windowed mode)
        constexpr uint32_t DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 0x2;
        if ((desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) != 0)
        {
            desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            modified = true;
        }
    }

    return modified;
}

void on_init_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr)
        return;

    // Skip if override is disabled
    const Config& config = Config::get_instance();
    if (!config.is_resolution_override_enabled())
        return;

    // Check if we have pending data for this swapchain's window
    // This indicates we actually modified the swapchain in on_create_swapchain
    WindowHandle hwnd = swapchain_ptr->get_hwnd();

    // If override is enabled, we assume the swapchain was potentially modified
    // The SwapchainManager will handle whether to actually initialize based on pending info
    if (hwnd == nullptr)
        return;

    // Initialize the swapchain resources
    SwapchainManager::get_instance().initialize_swapchain(swapchain_ptr);

    // Transition to exclusive fullscreen if configured (only on initial creation, not resize)
    if (!is_resize && config.is_exclusive_fullscreen_enabled())
    {
        device* device_ptr = swapchain_ptr->get_device();
        if (device_ptr == nullptr)
            return;

        const device_api api = device_ptr->get_api();

        // Only apply to DXGI-based APIs (D3D10, D3D11, D3D12)
        if (api == device_api::d3d10 || api == device_api::d3d11 || api == device_api::d3d12)
        {
            // Get native DXGI swapchain handle
            IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain_ptr->get_native());
            if (dxgi_swapchain != nullptr)
            {
                // Transition to exclusive fullscreen
                HRESULT hr = dxgi_swapchain->SetFullscreenState(TRUE, nullptr);
                if (SUCCEEDED(hr))
                {
                    reshade::log::message(reshade::log::level::info, "Successfully transitioned to exclusive fullscreen mode");
                }
                else
                {
                    reshade::log::message(reshade::log::level::error,
                        ("Failed to transition to exclusive fullscreen (HRESULT: 0x" +
                        std::to_string(static_cast<unsigned long>(hr)) + ")").c_str());
                }
            }
        }
        else
        {
            reshade::log::message(reshade::log::level::warning,
                "Exclusive fullscreen mode is only supported for D3D10/D3D11/D3D12 APIs");
        }
    }
}

void on_bind_render_targets_and_depth_stencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    if (cmd_list == nullptr || rtvs == nullptr || count == 0)
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    SwapchainManager& manager = SwapchainManager::get_instance();
    std::lock_guard<std::mutex> lock(manager.get_swapchain_mutex());

    // Create a modifiable copy of RTVs
    std::vector<resource_view> modified_rtvs(rtvs, rtvs + count);
    bool needs_rebind = false;

    // Check each RTV being bound
    for (uint32_t i = 0; i < count; ++i)
    {
        if (modified_rtvs[i].handle == 0)
            continue;

        // Get the underlying resource from the RTV
        resource rtv_resource = device_ptr->get_resource_from_view(modified_rtvs[i]);
        if (rtv_resource.handle == 0)
            continue;

        // Find if this device has an active swapchain override
        SwapchainData* data = manager.find_active_data_for_device(device_ptr);
        if (data == nullptr)
            continue;

        // Find if this resource matches any actual back buffer resource
        int proxy_index = data->find_proxy_index(rtv_resource);
        if (proxy_index >= 0)
        {
            // Substitute with proxy RTV
            modified_rtvs[i] = data->proxy_rtvs[proxy_index];
            needs_rebind = true;

            reshade::log::message(reshade::log::level::debug,
                ("Redirected back buffer RTV to proxy RTV " + std::to_string(proxy_index)).c_str());
        }
    }

    // If we modified any RTVs, rebind with the proxy RTVs
    if (needs_rebind)
    {
        cmd_list->bind_render_targets_and_depth_stencil(count, modified_rtvs.data(), dsv);
    }
}

void on_bind_viewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    if (cmd_list == nullptr || viewports == nullptr || count == 0)
        return;

    // Skip if override is disabled
    const Config& config = Config::get_instance();
    if (!config.is_resolution_override_enabled())
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    SwapchainManager& manager = SwapchainManager::get_instance();
    std::lock_guard<std::mutex> lock(manager.get_swapchain_mutex());

    // Find if we have an active override for this device
    SwapchainData* active_data = manager.find_active_data_for_device(device_ptr);
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

void on_bind_scissor_rects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    if (cmd_list == nullptr || rects == nullptr || count == 0)
        return;

    // Skip if override is disabled
    const Config& config = Config::get_instance();
    if (!config.is_resolution_override_enabled())
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    SwapchainManager& manager = SwapchainManager::get_instance();
    std::lock_guard<std::mutex> lock(manager.get_swapchain_mutex());

    // Find if we have an active override for this device
    SwapchainData* active_data = manager.find_active_data_for_device(device_ptr);
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

void on_present(command_queue* queue, swapchain* swapchain_ptr, const rect*, const rect*, uint32_t, const rect*)
{
    if (swapchain_ptr == nullptr || queue == nullptr)
        return;

    SwapchainManager& manager = SwapchainManager::get_instance();
    std::lock_guard<std::mutex> lock(manager.get_swapchain_mutex());

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    SwapchainData* data = manager.get_data(swapchain_handle);

    if (data == nullptr || !data->override_active)
        return; // No override for this swapchain

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

    // Get an immediate command list to perform the scaled copy via fullscreen draw
    command_list* cmd_list = queue->get_immediate_command_list();
    if (cmd_list == nullptr)
        return;

    resource_view proxy_srv = data->proxy_srvs[current_index];
    if (proxy_srv.handle == 0)
        return;

    // Create RTV for the actual back buffer (if not already created)
    resource_view actual_rtv = {};
    resource_desc actual_bb_desc = device_ptr->get_resource_desc(actual_back_buffer);
    resource_view_desc rtv_desc = {};
    rtv_desc.type = resource_view_type::texture_2d;
    rtv_desc.format = actual_bb_desc.texture.format;
    rtv_desc.texture.first_level = 0;
    rtv_desc.texture.level_count = 1;

    if (!device_ptr->create_resource_view(actual_back_buffer, resource_usage::render_target, rtv_desc, &actual_rtv))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create back buffer RTV for present");
        return;
    }

    // Barrier: Transition proxy texture to shader resource
    resource_usage proxy_old_state = resource_usage::render_target;
    resource_usage proxy_new_state = resource_usage::shader_resource;
    cmd_list->barrier(1, &proxy_texture, &proxy_old_state, &proxy_new_state);

    // Barrier: Transition actual back buffer to render target
    resource_usage dest_old_state = resource_usage::present;
    resource_usage dest_new_state = resource_usage::render_target;
    cmd_list->barrier(1, &actual_back_buffer, &dest_old_state, &dest_new_state);

    // Bind pipeline and render states
    cmd_list->bind_pipeline(pipeline_stage::all_graphics, data->copy_pipeline);

    // Bind descriptors (sampler and SRV)
    const sampler samplers[] = { data->copy_sampler };
    const resource_view srvs[] = { proxy_srv };

    cmd_list->push_descriptors(shader_stage::pixel, data->copy_pipeline_layout, 0,
        descriptor_table_update { {}, 0, 0, 1, descriptor_type::sampler, samplers });
    cmd_list->push_descriptors(shader_stage::pixel, data->copy_pipeline_layout, 1,
        descriptor_table_update { {}, 0, 0, 1, descriptor_type::shader_resource_view, srvs });

    // Bind render target
    cmd_list->bind_render_targets_and_depth_stencil(1, &actual_rtv, {});

    // Set viewport to cover entire back buffer
    viewport vp = {};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(data->actual_width);
    vp.height = static_cast<float>(data->actual_height);
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    cmd_list->bind_viewports(0, 1, &vp);

    // Draw fullscreen triangle (3 vertices, 1 instance)
    cmd_list->draw(3, 1, 0, 0);

    // Barrier: Transition resources back
    proxy_old_state = resource_usage::shader_resource;
    proxy_new_state = resource_usage::render_target;
    cmd_list->barrier(1, &proxy_texture, &proxy_old_state, &proxy_new_state);

    dest_old_state = resource_usage::render_target;
    dest_new_state = resource_usage::present;
    cmd_list->barrier(1, &actual_back_buffer, &dest_old_state, &dest_new_state);

    // Clean up temporary RTV
    device_ptr->destroy_resource_view(actual_rtv);
}

bool on_set_fullscreen_state(swapchain* swapchain_ptr, bool fullscreen, void* hmonitor)
{
    if (swapchain_ptr == nullptr)
        return false;

    const Config& config = Config::get_instance();

    // If exclusive mode is forced:
    // - Allow transitions TO fullscreen (including our own internal transition)
    // - Block transitions to windowed
    if (config.is_exclusive_fullscreen_enabled())
    {
        if (!fullscreen)
        {
            reshade::log::message(reshade::log::level::debug,
                "Blocking windowed transition to maintain exclusive fullscreen mode");
            return true; // Block the change to windowed
        }
        else
        {
            // Allow transition to fullscreen (this is what we want)
            return false;
        }
    }

    // If borderless mode is forced:
    // - Allow transitions to windowed
    // - Block transitions to fullscreen
    if (config.is_borderless_fullscreen_enabled())
    {
        if (fullscreen)
        {
            reshade::log::message(reshade::log::level::debug,
                "Blocking fullscreen transition to maintain borderless fullscreen mode");
            return true; // Block the change to exclusive fullscreen
        }
        else
        {
            // Allow transition to windowed (this is what we want)
            return false;
        }
    }

    // FullscreenMode::Unchanged - respect block_fullscreen_changes setting
    if (config.get_block_fullscreen_changes())
    {
        reshade::log::message(reshade::log::level::debug,
            ("Blocked fullscreen state change attempt (requested: " + std::string(fullscreen ? "fullscreen" : "windowed") + ")").c_str());
        return true; // Return true to block the change
    }

    return false; // Allow the change
}

void on_destroy_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr || is_resize)
        return; // Don't destroy on resize, only on actual destruction

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    SwapchainManager::get_instance().destroy_swapchain(swapchain_handle);
}
