/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "swapchain_manager.h"
#include "config.h"
#include "shader_bytecode.h"

using namespace reshade::api;

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
