/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"
#include <string>
#include <chrono>
#include <sstream>

class DebugLogger
{
public:
    // Singleton access
    static DebugLogger& get_instance();

    // Initialize the logger (sets start time)
    void initialize();

    // Get current timestamp relative to start (in seconds with milliseconds)
    double get_timestamp() const;

    // Get and increment sequence number
    uint32_t get_next_sequence();

    // Format event header: [timestamp] [seq] === EVENT_NAME ===
    std::string format_event_header(const char* event_name) const;

    // Format HRESULT with common DXGI error code names
    std::string format_hresult(HRESULT hr) const;

    // Format device API enum to string
    const char* device_api_to_string(reshade::api::device_api api) const;

    // Format format enum to string
    const char* format_to_string(reshade::api::format format) const;

    // Format resource usage flags to string
    std::string resource_usage_to_string(reshade::api::resource_usage usage) const;

    // Log device info
    void log_device_info(reshade::api::device* device) const;

    // Log swapchain description
    void log_swapchain_desc(const reshade::api::swapchain_desc& desc, void* hwnd) const;

    // Log window state (Win32 specific)
    void log_window_state(HWND hwnd) const;

    // Log DXGI state (D3D10/11/12 specific)
    void log_dxgi_state(void* swapchain_native, reshade::api::device_api api) const;

    // Log monitor info
    void log_monitor_info(HMONITOR hmonitor) const;

    // Decode window style flags
    std::string decode_window_style(DWORD style) const;

    // Decode window ex style flags
    std::string decode_window_ex_style(DWORD ex_style) const;

private:
    DebugLogger() = default;
    ~DebugLogger() = default;

    // Delete copy/move constructors
    DebugLogger(const DebugLogger&) = delete;
    DebugLogger& operator=(const DebugLogger&) = delete;
    DebugLogger(DebugLogger&&) = delete;
    DebugLogger& operator=(DebugLogger&&) = delete;

    std::chrono::high_resolution_clock::time_point start_time_;
    uint32_t sequence_counter_ = 0;
    mutable std::mutex sequence_mutex_;
};
