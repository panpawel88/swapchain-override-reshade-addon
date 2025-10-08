/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "swapchain_manager.h"
#include "config.h"
#include "shader_bytecode.h"

using namespace reshade::api;

// SwapchainData methods
int SwapchainData::find_proxy_index(resource actual_resource) const
{
    for (size_t i = 0; i < actual_back_buffers.size(); ++i)
    {
        if (actual_back_buffers[i].handle == actual_resource.handle)
            return static_cast<int>(i);
    }
    return -1;
}

SwapchainData::~SwapchainData()
{
    cleanup();
}

void SwapchainData::cleanup()
{
    if (device_ptr != nullptr)
    {
        // Destroy pipeline objects
        if (copy_pipeline.handle != 0)
            device_ptr->destroy_pipeline(copy_pipeline);
        if (copy_pipeline_layout.handle != 0)
            device_ptr->destroy_pipeline_layout(copy_pipeline_layout);
        if (copy_sampler.handle != 0)
            device_ptr->destroy_sampler(copy_sampler);

        // Destroy proxy resource views
        for (auto rtv : proxy_rtvs)
        {
            if (rtv.handle != 0)
                device_ptr->destroy_resource_view(rtv);
        }

        // Destroy proxy SRVs
        for (auto srv : proxy_srvs)
        {
            if (srv.handle != 0)
                device_ptr->destroy_resource_view(srv);
        }

        // Destroy proxy resources
        for (auto tex : proxy_textures)
        {
            if (tex.handle != 0)
                device_ptr->destroy_resource(tex);
        }
    }

    copy_pipeline = {};
    copy_pipeline_layout = {};
    copy_sampler = {};

    proxy_rtvs.clear();
    proxy_srvs.clear();
    proxy_textures.clear();
    actual_back_buffers.clear();
}

// SwapchainManager methods
SwapchainManager& SwapchainManager::get_instance()
{
    static SwapchainManager instance;
    return instance;
}

void SwapchainManager::store_pending_info(WindowHandle hwnd, uint32_t width, uint32_t height)
{
    std::lock_guard<std::mutex> lock(pending_mutex_);
    PendingSwapchainInfo info;
    info.original_width = width;
    info.original_height = height;
    pending_swapchains_[hwnd] = info;
}

bool SwapchainManager::retrieve_pending_info(WindowHandle hwnd, uint32_t& out_width, uint32_t& out_height)
{
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_swapchains_.find(hwnd);
    if (it != pending_swapchains_.end())
    {
        out_width = it->second.original_width;
        out_height = it->second.original_height;
        pending_swapchains_.erase(it);
        return true;
    }
    return false;
}

bool SwapchainManager::initialize_swapchain(swapchain* swapchain_ptr)
{
    if (swapchain_ptr == nullptr)
        return false;

    std::lock_guard<std::mutex> lock(swapchain_mutex_);

    device* device_ptr = swapchain_ptr->get_device();
    if (device_ptr == nullptr)
        return false;

    // Get swapchain information
    const uint32_t back_buffer_count = swapchain_ptr->get_back_buffer_count();
    if (back_buffer_count == 0)
        return false;

    // Get the actual swapchain back buffer
    resource actual_back_buffer = swapchain_ptr->get_back_buffer(0);
    resource_desc actual_desc = device_ptr->get_resource_desc(actual_back_buffer);

    // Create or retrieve swapchain data
    SwapchainData* data = nullptr;
    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();

    auto it = swapchain_data_.find(swapchain_handle);
    if (it != swapchain_data_.end())
    {
        // Clean up existing resources on resize
        data = it->second;
        data->cleanup();
    }
    else
    {
        // Create new swapchain data
        data = new SwapchainData();
        swapchain_data_[swapchain_handle] = data;
    }

    data->device_ptr = device_ptr;
    data->actual_width = actual_desc.texture.width;
    data->actual_height = actual_desc.texture.height;
    data->override_active = true;

    // Retrieve the original requested size from pending map
    WindowHandle hwnd = swapchain_ptr->get_hwnd();
    if (!retrieve_pending_info(hwnd, data->original_width, data->original_height))
    {
        // Fallback if we couldn't retrieve original size (shouldn't happen in normal flow)
        reshade::log::message(reshade::log::level::warning,
            "Could not retrieve original swapchain dimensions, using 1920x1080 as fallback");
        data->original_width = 1920;
        data->original_height = 1080;
    }

    // Create proxy resources
    if (!create_proxy_resources(data, swapchain_ptr))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create proxy resources");
        data->cleanup();
        return false;
    }

    // Create copy pipeline
    if (!create_copy_pipeline(data, actual_desc.texture.format))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy pipeline");
        data->cleanup();
        return false;
    }

    reshade::log::message(reshade::log::level::info,
        ("Created " + std::to_string(back_buffer_count) + " proxy textures at " +
        std::to_string(data->original_width) + "x" + std::to_string(data->original_height)).c_str());

    return true;
}

bool SwapchainManager::create_proxy_resources(SwapchainData* data, swapchain* swapchain_ptr)
{
    device* device_ptr = data->device_ptr;
    const uint32_t back_buffer_count = swapchain_ptr->get_back_buffer_count();

    // Get actual back buffer descriptor
    resource actual_back_buffer = swapchain_ptr->get_back_buffer(0);
    resource_desc actual_desc = device_ptr->get_resource_desc(actual_back_buffer);

    // Create proxy textures at original resolution
    data->proxy_textures.resize(back_buffer_count);
    data->proxy_rtvs.resize(back_buffer_count);
    data->actual_back_buffers.resize(back_buffer_count);

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
            return false;
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
            return false;
        }
    }

    // Create shader resource views for proxy textures (for sampling during copy)
    data->proxy_srvs.resize(back_buffer_count);
    for (uint32_t i = 0; i < back_buffer_count; ++i)
    {
        resource_view_desc srv_desc = {};
        srv_desc.type = resource_view_type::texture_2d;
        srv_desc.format = actual_desc.texture.format;
        srv_desc.texture.first_level = 0;
        srv_desc.texture.level_count = 1;

        if (!device_ptr->create_resource_view(data->proxy_textures[i], resource_usage::shader_resource, srv_desc, &data->proxy_srvs[i]))
        {
            reshade::log::message(reshade::log::level::error,
                ("Failed to create proxy SRV " + std::to_string(i)).c_str());
            return false;
        }
    }

    // Store actual back buffer resources for comparison during bind interception
    for (uint32_t i = 0; i < back_buffer_count; ++i)
    {
        data->actual_back_buffers[i] = swapchain_ptr->get_back_buffer(i);
    }

    return true;
}

bool SwapchainManager::create_copy_pipeline(SwapchainData* data, format format)
{
    device* device_ptr = data->device_ptr;

    // Create copy pipeline (fullscreen triangle + texture sample)
    // Pipeline layout: sampler in slot 0, SRV in slot 0
    pipeline_layout_param layout_params[2];
    layout_params[0] = descriptor_range { 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::sampler };
    layout_params[1] = descriptor_range { 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::shader_resource_view };

    if (!device_ptr->create_pipeline_layout(2, layout_params, &data->copy_pipeline_layout))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy pipeline layout");
        return false;
    }

    // Create shaders from embedded bytecode
    shader_desc vs_desc = { shader_bytecode::fullscreen_vs, shader_bytecode::fullscreen_vs_size };
    shader_desc ps_desc = { shader_bytecode::copy_ps, shader_bytecode::copy_ps_size };

    // Build pipeline subobjects
    std::vector<pipeline_subobject> subobjects;
    subobjects.push_back({ pipeline_subobject_type::vertex_shader, 1, &vs_desc });
    subobjects.push_back({ pipeline_subobject_type::pixel_shader, 1, &ps_desc });

    if (!device_ptr->create_pipeline(data->copy_pipeline_layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &data->copy_pipeline))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy pipeline");
        return false;
    }

    // Create sampler with the configured filter mode
    sampler_desc sampler_desc = {};
    sampler_desc.filter = Config::get_instance().get_scaling_filter();
    sampler_desc.address_u = texture_address_mode::clamp;
    sampler_desc.address_v = texture_address_mode::clamp;
    sampler_desc.address_w = texture_address_mode::clamp;

    if (!device_ptr->create_sampler(sampler_desc, &data->copy_sampler))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy sampler");
        return false;
    }

    return true;
}

void SwapchainManager::destroy_swapchain(SwapchainNativeHandle swapchain_handle)
{
    std::lock_guard<std::mutex> lock(swapchain_mutex_);

    auto it = swapchain_data_.find(swapchain_handle);
    if (it != swapchain_data_.end())
    {
        delete it->second;
        swapchain_data_.erase(it);
        reshade::log::message(reshade::log::level::info, "Cleaned up swapchain override data");
    }
}

SwapchainData* SwapchainManager::get_data(SwapchainNativeHandle swapchain_handle)
{
    // Note: Caller must hold swapchain_mutex_
    auto it = swapchain_data_.find(swapchain_handle);
    if (it != swapchain_data_.end())
        return it->second;
    return nullptr;
}

SwapchainData* SwapchainManager::find_active_data_for_device(device* device_ptr)
{
    // Note: Caller must hold swapchain_mutex_
    for (auto& pair : swapchain_data_)
    {
        if (pair.second->override_active && pair.second->device_ptr == device_ptr)
        {
            return pair.second;
        }
    }
    return nullptr;
}

void SwapchainManager::cleanup_all()
{
    std::lock_guard<std::mutex> lock(swapchain_mutex_);
    for (auto& pair : swapchain_data_)
    {
        delete pair.second;
    }
    swapchain_data_.clear();

    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
    pending_swapchains_.clear();
}

void SwapchainManager::handle_bind_render_targets(command_list* cmd_list, uint32_t count,
                                                   const resource_view* rtvs, resource_view dsv)
{
    if (cmd_list == nullptr || rtvs == nullptr || count == 0)
        return;

    // Get the device to lookup swapchain data
    device* device_ptr = cmd_list->get_device();
    if (device_ptr == nullptr)
        return;

    std::lock_guard<std::mutex> lock(swapchain_mutex_);

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
        SwapchainData* data = find_active_data_for_device(device_ptr);
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

void SwapchainManager::handle_bind_viewports(command_list* cmd_list, uint32_t first, uint32_t count,
                                              const viewport* viewports)
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

    std::lock_guard<std::mutex> lock(swapchain_mutex_);

    // Find if we have an active override for this device
    SwapchainData* active_data = find_active_data_for_device(device_ptr);
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

void SwapchainManager::handle_bind_scissor_rects(command_list* cmd_list, uint32_t first, uint32_t count,
                                                  const rect* rects)
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

    std::lock_guard<std::mutex> lock(swapchain_mutex_);

    // Find if we have an active override for this device
    SwapchainData* active_data = find_active_data_for_device(device_ptr);
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

void SwapchainManager::handle_present(command_queue* queue, swapchain* swapchain_ptr)
{
    if (swapchain_ptr == nullptr || queue == nullptr)
        return;

    std::lock_guard<std::mutex> lock(swapchain_mutex_);

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    SwapchainData* data = get_data(swapchain_handle);

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

bool SwapchainManager::handle_create_swapchain(device_api api, swapchain_desc& desc, void* hwnd)
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
                store_pending_info(
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
        // Instead, we'll transition to fullscreen after creation in handle_init_swapchain

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

void SwapchainManager::handle_init_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr)
        return;

    // Skip if override is disabled
    const Config& config = Config::get_instance();
    if (!config.is_resolution_override_enabled())
        return;

    // Check if we have pending data for this swapchain's window
    // This indicates we actually modified the swapchain in handle_create_swapchain
    WindowHandle hwnd = swapchain_ptr->get_hwnd();

    // If override is enabled, we assume the swapchain was potentially modified
    // The SwapchainManager will handle whether to actually initialize based on pending info
    if (hwnd == nullptr)
        return;

    // Initialize the swapchain resources
    initialize_swapchain(swapchain_ptr);

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

bool SwapchainManager::handle_set_fullscreen_state(swapchain* swapchain_ptr, bool fullscreen, void* hmonitor)
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

void SwapchainManager::handle_destroy_swapchain(swapchain* swapchain_ptr, bool is_resize)
{
    if (swapchain_ptr == nullptr || is_resize)
        return; // Don't destroy on resize, only on actual destruction

    const SwapchainNativeHandle swapchain_handle = swapchain_ptr->get_native();
    destroy_swapchain(swapchain_handle);
}
