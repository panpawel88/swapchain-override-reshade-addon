/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"

enum class FullscreenMode
{
    Unchanged = 0,   // Don't modify fullscreen behavior (default)
    Borderless = 1,  // Force borderless fullscreen (windowed)
    Exclusive = 2    // Force exclusive fullscreen
};

class Config
{
public:
    // Singleton access
    static Config& get_instance();

    // Load configuration from ReShade.ini
    void load();

    // Getters
    uint32_t get_force_width() const { return force_width_; }
    uint32_t get_force_height() const { return force_height_; }
    reshade::api::filter_mode get_scaling_filter() const { return scaling_filter_; }
    FullscreenMode get_fullscreen_mode() const { return fullscreen_mode_; }
    bool get_block_fullscreen_changes() const { return block_fullscreen_changes_; }
    int get_target_monitor() const { return target_monitor_; }

    // Convenience methods
    bool is_override_enabled() const { return force_width_ != 0 && force_height_ != 0; }

private:
    Config() = default;
    ~Config() = default;

    // Delete copy/move constructors
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Configuration values
    uint32_t force_width_ = 0;
    uint32_t force_height_ = 0;
    reshade::api::filter_mode scaling_filter_ = reshade::api::filter_mode::min_mag_mip_linear;
    FullscreenMode fullscreen_mode_ = FullscreenMode::Unchanged;
    bool block_fullscreen_changes_ = false;
    int target_monitor_ = 0; // 0 = primary, 1+ = secondary monitors
};
