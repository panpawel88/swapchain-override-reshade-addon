/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

// ImGui setup: ImTextureID must be defined BEFORE including ANY headers
// because reshade.hpp (included via common.h) pulls in reshade_overlay.hpp which needs ImGui types
#define ImTextureID ImU64
#include <imgui.h>

// Now safe to include headers that pull in ReShade/ImGui
#include "overlay.h"
#include "config.h"
#include "swapchain_manager.h"

using namespace reshade::api;

// Singleton access
OverlayManager& OverlayManager::get_instance()
{
    static OverlayManager instance;
    return instance;
}

// Install overlay
void OverlayManager::install()
{
    reshade::register_overlay(nullptr, on_overlay);
}

// Uninstall overlay
void OverlayManager::uninstall()
{
    reshade::unregister_overlay(nullptr, on_overlay);
}

// Static callback wrapper
void OverlayManager::on_overlay(effect_runtime* runtime)
{
    get_instance().render_overlay(runtime);
}

// Helper function to convert filter mode to string
static const char* filter_mode_to_string(filter_mode mode)
{
    switch (mode)
    {
    case filter_mode::min_mag_mip_point:
        return "Point";
    case filter_mode::min_mag_mip_linear:
        return "Linear";
    case filter_mode::min_mag_linear_mip_point:
        return "Anisotropic";
    default:
        return "Unknown";
    }
}

// Helper function to convert fullscreen mode to string
static const char* fullscreen_mode_to_string(FullscreenMode mode)
{
    switch (mode)
    {
    case FullscreenMode::Unchanged:
        return "Unchanged";
    case FullscreenMode::Borderless:
        return "Borderless";
    case FullscreenMode::Exclusive:
        return "Exclusive";
    default:
        return "Unknown";
    }
}

// Render overlay implementation
void OverlayManager::render_overlay(effect_runtime* runtime)
{
    // Get configuration
    const Config& config = Config::get_instance();

    // Copy swapchain data FIRST to avoid holding mutex during ImGui calls
    struct SwapchainSnapshot
    {
        SwapchainNativeHandle handle;
        uint32_t original_width;
        uint32_t original_height;
        uint32_t actual_width;
        uint32_t actual_height;
        bool override_active;
    };

    std::vector<SwapchainSnapshot> swapchains;
    SwapchainManager::get_instance().for_each_swapchain(
        [&swapchains](SwapchainNativeHandle handle, const SwapchainData& data) {
            swapchains.push_back({
                handle,
                data.original_width,
                data.original_height,
                data.actual_width,
                data.actual_height,
                data.override_active
            });
        });

    // NOW we can safely call ImGui functions without holding any locks

    // Display configuration section
    ImGui::TextUnformatted("Configuration:", nullptr);
    ImGui::Separator();

    // Resolution override
    if (config.is_resolution_override_enabled())
    {
        char res_buffer[64];
        snprintf(res_buffer, sizeof(res_buffer), "  Resolution Override: %ux%u",
                 config.get_force_width(), config.get_force_height());
        ImGui::TextUnformatted(res_buffer, nullptr);
    }
    else
    {
        ImGui::TextUnformatted("  Resolution Override: Disabled", nullptr);
    }

    // Scaling filter
    char filter_buffer[64];
    snprintf(filter_buffer, sizeof(filter_buffer), "  Scaling Filter: %s",
             filter_mode_to_string(config.get_scaling_filter()));
    ImGui::TextUnformatted(filter_buffer, nullptr);

    // Fullscreen mode
    char fs_mode_buffer[64];
    snprintf(fs_mode_buffer, sizeof(fs_mode_buffer), "  Fullscreen Mode: %s",
             fullscreen_mode_to_string(config.get_fullscreen_mode()));
    ImGui::TextUnformatted(fs_mode_buffer, nullptr);

    // Block fullscreen changes
    char block_fs_buffer[64];
    snprintf(block_fs_buffer, sizeof(block_fs_buffer), "  Block Fullscreen Changes: %s",
             config.get_block_fullscreen_changes() ? "Yes" : "No");
    ImGui::TextUnformatted(block_fs_buffer, nullptr);

    // Target monitor
    char monitor_buffer[64];
    snprintf(monitor_buffer, sizeof(monitor_buffer), "  Target Monitor: %d %s",
             config.get_target_monitor(),
             config.get_target_monitor() == 0 ? "(Primary)" : "");
    ImGui::TextUnformatted(monitor_buffer, nullptr);

    // Add spacing
    ImGui::NewLine();

    // Display active swapchains
    ImGui::TextUnformatted("Active Swapchains:", nullptr);
    ImGui::Separator();

    if (swapchains.empty())
    {
        ImGui::TextUnformatted("  No active swapchains", nullptr);
    }
    else
    {
        int index = 1;
        for (const auto& sc : swapchains)
        {
            char header_buffer[128];
            snprintf(header_buffer, sizeof(header_buffer),
                     "  Swapchain #%d (Handle: 0x%llX)",
                     index, static_cast<unsigned long long>(sc.handle));
            ImGui::TextUnformatted(header_buffer, nullptr);

            char requested_buffer[64];
            snprintf(requested_buffer, sizeof(requested_buffer),
                     "    Requested: %ux%u",
                     sc.original_width, sc.original_height);
            ImGui::TextUnformatted(requested_buffer, nullptr);

            char actual_buffer[64];
            snprintf(actual_buffer, sizeof(actual_buffer),
                     "    Actual: %ux%u",
                     sc.actual_width, sc.actual_height);
            ImGui::TextUnformatted(actual_buffer, nullptr);

            char override_buffer[64];
            snprintf(override_buffer, sizeof(override_buffer),
                     "    Override Active: %s",
                     sc.override_active ? "Yes" : "No");
            ImGui::TextUnformatted(override_buffer, nullptr);

            index++;
        }
    }
}
