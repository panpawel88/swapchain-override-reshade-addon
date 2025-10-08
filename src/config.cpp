/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "config.h"

namespace
{
    constexpr const char* CONFIG_SECTION = "SWAPCHAIN_OVERRIDE";
}

Config& Config::get_instance()
{
    static Config instance;
    return instance;
}

void Config::load()
{
    // Read forced resolution
    char resolution_string[32] = {};
    size_t resolution_string_size = sizeof(resolution_string);

    if (reshade::get_config_value(nullptr, CONFIG_SECTION, "ForceSwapchainResolution", resolution_string, &resolution_string_size))
    {
        char* resolution_p = resolution_string;
        const unsigned long width = std::strtoul(resolution_p, &resolution_p, 10);
        const char width_terminator = *resolution_p++;
        const unsigned long height = std::strtoul(resolution_p, &resolution_p, 10);

        if (width != 0 && height != 0 && width_terminator == 'x')
        {
            force_width_ = static_cast<uint32_t>(width);
            force_height_ = static_cast<uint32_t>(height);
        }
    }
    else
    {
        // Set default config value
        reshade::set_config_value(nullptr, CONFIG_SECTION, "ForceSwapchainResolution", "3840x2160");
        force_width_ = 3840;
        force_height_ = 2160;
    }

    // Read scaling filter
    int filter_value = 1; // Default to linear
    if (reshade::get_config_value(nullptr, CONFIG_SECTION, "SwapchainScalingFilter", filter_value))
    {
        switch (filter_value)
        {
        case 0:
            scaling_filter_ = reshade::api::filter_mode::min_mag_mip_point;
            break;
        case 1:
            scaling_filter_ = reshade::api::filter_mode::min_mag_mip_linear;
            break;
        case 2:
            scaling_filter_ = reshade::api::filter_mode::min_mag_linear_mip_point;
            break;
        default:
            scaling_filter_ = reshade::api::filter_mode::min_mag_mip_linear;
            break;
        }
    }
    else
    {
        reshade::set_config_value(nullptr, CONFIG_SECTION, "SwapchainScalingFilter", 1);
    }

    // Read fullscreen mode
    int fullscreen_mode_value = 0; // Default to Unchanged
    if (reshade::get_config_value(nullptr, CONFIG_SECTION, "FullscreenMode", fullscreen_mode_value))
    {
        switch (fullscreen_mode_value)
        {
        case 0:
            fullscreen_mode_ = FullscreenMode::Unchanged;
            break;
        case 1:
            fullscreen_mode_ = FullscreenMode::Borderless;
            break;
        case 2:
            fullscreen_mode_ = FullscreenMode::Exclusive;
            break;
        default:
            fullscreen_mode_ = FullscreenMode::Unchanged;
            break;
        }
    }
    else
    {
        reshade::set_config_value(nullptr, CONFIG_SECTION, "FullscreenMode", 0);
    }

    // Read block fullscreen changes
    if (!reshade::get_config_value(nullptr, CONFIG_SECTION, "BlockFullscreenChanges", block_fullscreen_changes_))
    {
        reshade::set_config_value(nullptr, CONFIG_SECTION, "BlockFullscreenChanges", false);
    }

    // Read target monitor
    if (!reshade::get_config_value(nullptr, CONFIG_SECTION, "TargetMonitor", target_monitor_))
    {
        reshade::set_config_value(nullptr, CONFIG_SECTION, "TargetMonitor", 0);
        target_monitor_ = 0;
    }
}
