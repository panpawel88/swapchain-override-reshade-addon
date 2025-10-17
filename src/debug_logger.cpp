/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "debug_logger.h"
#include <iomanip>
#include <dxgi.h>

DebugLogger& DebugLogger::get_instance()
{
    static DebugLogger instance;
    return instance;
}

void DebugLogger::initialize()
{
    start_time_ = std::chrono::high_resolution_clock::now();
    sequence_counter_ = 0;
}

double DebugLogger::get_timestamp() const
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_);
    return duration.count() / 1000000.0;
}

uint32_t DebugLogger::get_next_sequence()
{
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    return ++sequence_counter_;
}

std::string DebugLogger::format_event_header(const char* event_name) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "[" << std::setw(8) << std::setfill('0') << get_timestamp() << "] ";
    oss << "[" << std::setw(3) << std::setfill('0') << sequence_counter_ << "] ";
    oss << "=== " << event_name << " ===";
    return oss.str();
}

std::string DebugLogger::format_hresult(HRESULT hr) const
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << hr;

    // Add common DXGI error names
    switch (hr)
    {
    case 0x887A0001: oss << " (DXGI_ERROR_INVALID_CALL)"; break;
    case 0x887A0002: oss << " (DXGI_ERROR_NOT_FOUND)"; break;
    case 0x887A0003: oss << " (DXGI_ERROR_MORE_DATA)"; break;
    case 0x887A0004: oss << " (DXGI_ERROR_UNSUPPORTED)"; break;
    case 0x887A0005: oss << " (DXGI_ERROR_DEVICE_REMOVED)"; break;
    case 0x887A0006: oss << " (DXGI_ERROR_DEVICE_HUNG)"; break;
    case 0x887A0007: oss << " (DXGI_ERROR_DEVICE_RESET)"; break;
    case 0x887A000A: oss << " (DXGI_ERROR_WAS_STILL_DRAWING)"; break;
    case 0x887A000B: oss << " (DXGI_ERROR_FRAME_STATISTICS_DISJOINT)"; break;
    case 0x887A000C: oss << " (DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE)"; break;
    case 0x887A0020: oss << " (DXGI_ERROR_DRIVER_INTERNAL_ERROR)"; break;
    case 0x887A0021: oss << " (DXGI_ERROR_NONEXCLUSIVE)"; break;
    case 0x887A0022: oss << " (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)"; break;
    case 0x887A0026: oss << " (DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED)"; break;
    case 0x887A0027: oss << " (DXGI_ERROR_REMOTE_OUTOFMEMORY)"; break;
    case S_OK: oss << " (S_OK)"; break;
    case E_INVALIDARG: oss << " (E_INVALIDARG)"; break;
    case E_OUTOFMEMORY: oss << " (E_OUTOFMEMORY)"; break;
    default: break;
    }

    return oss.str();
}

const char* DebugLogger::device_api_to_string(reshade::api::device_api api) const
{
    using namespace reshade::api;
    switch (api)
    {
    case device_api::d3d9: return "D3D9";
    case device_api::d3d10: return "D3D10";
    case device_api::d3d11: return "D3D11";
    case device_api::d3d12: return "D3D12";
    case device_api::opengl: return "OpenGL";
    case device_api::vulkan: return "Vulkan";
    default: return "Unknown";
    }
}

const char* DebugLogger::format_to_string(reshade::api::format format) const
{
    using namespace reshade::api;
    switch (format)
    {
    case format::r8g8b8a8_unorm: return "R8G8B8A8_UNORM";
    case format::r8g8b8a8_unorm_srgb: return "R8G8B8A8_UNORM_SRGB";
    case format::b8g8r8a8_unorm: return "B8G8R8A8_UNORM";
    case format::b8g8r8a8_unorm_srgb: return "B8G8R8A8_UNORM_SRGB";
    case format::r10g10b10a2_unorm: return "R10G10B10A2_UNORM";
    case format::r16g16b16a16_float: return "R16G16B16A16_FLOAT";
    default: return "Other";
    }
}

std::string DebugLogger::resource_usage_to_string(reshade::api::resource_usage usage) const
{
    using namespace reshade::api;
    std::ostringstream oss;

    if (usage == resource_usage::undefined) return "undefined";

    bool first = true;
    auto append_flag = [&](const char* name) {
        if (!first) oss << " | ";
        oss << name;
        first = false;
    };

    if ((usage & resource_usage::depth_stencil) == resource_usage::depth_stencil)
        append_flag("depth_stencil");
    if ((usage & resource_usage::render_target) == resource_usage::render_target)
        append_flag("render_target");
    if ((usage & resource_usage::shader_resource) == resource_usage::shader_resource)
        append_flag("shader_resource");
    if ((usage & resource_usage::unordered_access) == resource_usage::unordered_access)
        append_flag("unordered_access");
    if ((usage & resource_usage::copy_dest) == resource_usage::copy_dest)
        append_flag("copy_dest");
    if ((usage & resource_usage::copy_source) == resource_usage::copy_source)
        append_flag("copy_source");

    return oss.str();
}

void DebugLogger::log_device_info(reshade::api::device* device) const
{
    if (device == nullptr)
        return;

    const reshade::api::device_api api = device->get_api();
    reshade::log::message(reshade::log::level::info,
        ("  Device API: " + std::string(device_api_to_string(api))).c_str());
}

void DebugLogger::log_swapchain_desc(const reshade::api::swapchain_desc& desc, void* hwnd) const
{
    std::ostringstream info;
    info << "  Resolution: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height << "\n";
    info << "  Format: " << format_to_string(desc.back_buffer.texture.format) << "\n";
    info << "  Fullscreen State: " << (desc.fullscreen_state ? "true (exclusive)" : "false (windowed)") << "\n";
    info << "  Present Flags: 0x" << std::hex << std::uppercase << desc.present_flags << std::dec << "\n";
    info << "  Back Buffer Count: " << desc.back_buffer_count << "\n";
    if (hwnd != nullptr)
        info << "  HWND: 0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(hwnd);

    reshade::log::message(reshade::log::level::info, info.str().c_str());
}

std::string DebugLogger::decode_window_style(DWORD style) const
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << style << " (";

    bool first = true;
    auto append_flag = [&](const char* name) {
        if (!first) oss << " | ";
        oss << name;
        first = false;
    };

    if (style & WS_POPUP) append_flag("WS_POPUP");
    if (style & WS_CHILD) append_flag("WS_CHILD");
    if (style & WS_MINIMIZE) append_flag("WS_MINIMIZE");
    if (style & WS_VISIBLE) append_flag("WS_VISIBLE");
    if (style & WS_DISABLED) append_flag("WS_DISABLED");
    if (style & WS_CLIPSIBLINGS) append_flag("WS_CLIPSIBLINGS");
    if (style & WS_CLIPCHILDREN) append_flag("WS_CLIPCHILDREN");
    if (style & WS_MAXIMIZE) append_flag("WS_MAXIMIZE");
    if (style & WS_CAPTION) append_flag("WS_CAPTION");
    if (style & WS_BORDER) append_flag("WS_BORDER");
    if (style & WS_DLGFRAME) append_flag("WS_DLGFRAME");
    if (style & WS_VSCROLL) append_flag("WS_VSCROLL");
    if (style & WS_HSCROLL) append_flag("WS_HSCROLL");
    if (style & WS_SYSMENU) append_flag("WS_SYSMENU");
    if (style & WS_THICKFRAME) append_flag("WS_THICKFRAME");
    if (style & WS_MINIMIZEBOX) append_flag("WS_MINIMIZEBOX");
    if (style & WS_MAXIMIZEBOX) append_flag("WS_MAXIMIZEBOX");

    oss << ")";
    return oss.str();
}

std::string DebugLogger::decode_window_ex_style(DWORD ex_style) const
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << ex_style << " (";

    bool first = true;
    auto append_flag = [&](const char* name) {
        if (!first) oss << " | ";
        oss << name;
        first = false;
    };

    if (ex_style & WS_EX_TOPMOST) append_flag("WS_EX_TOPMOST");
    if (ex_style & WS_EX_APPWINDOW) append_flag("WS_EX_APPWINDOW");
    if (ex_style & WS_EX_TOOLWINDOW) append_flag("WS_EX_TOOLWINDOW");
    if (ex_style & WS_EX_WINDOWEDGE) append_flag("WS_EX_WINDOWEDGE");
    if (ex_style & WS_EX_CLIENTEDGE) append_flag("WS_EX_CLIENTEDGE");
    if (ex_style & WS_EX_LAYERED) append_flag("WS_EX_LAYERED");

    oss << ")";
    return oss.str();
}

void DebugLogger::log_window_state(HWND hwnd) const
{
    if (hwnd == nullptr || !IsWindow(hwnd))
        return;

    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    DWORD ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    RECT rect;
    GetWindowRect(hwnd, &rect);

    std::ostringstream info;
    info << "  HWND: 0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(hwnd) << std::dec << "\n";
    info << "  Window Style: " << decode_window_style(style) << "\n";
    info << "  Window Ex Style: " << decode_window_ex_style(ex_style) << "\n";
    info << "  Window Rect: (" << rect.left << "," << rect.top << ")-(" << rect.right << "," << rect.bottom << ")";

    reshade::log::message(reshade::log::level::info, info.str().c_str());
}

void DebugLogger::log_dxgi_state(void* swapchain_native, reshade::api::device_api api) const
{
    using namespace reshade::api;

    // Only works for DXGI-based APIs
    if (api != device_api::d3d10 && api != device_api::d3d11 && api != device_api::d3d12)
        return;

    IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain_native);
    if (dxgi_swapchain == nullptr)
        return;

    BOOL is_fullscreen = FALSE;
    IDXGIOutput* output = nullptr;
    HRESULT hr = dxgi_swapchain->GetFullscreenState(&is_fullscreen, &output);

    std::ostringstream info;
    if (SUCCEEDED(hr))
    {
        info << "  DXGI Fullscreen State: " << (is_fullscreen ? "true (exclusive)" : "false (windowed)") << "\n";

        if (output != nullptr)
        {
            DXGI_OUTPUT_DESC output_desc;
            if (SUCCEEDED(output->GetDesc(&output_desc)))
            {
                // Convert wide string to narrow string
                char device_name[32];
                WideCharToMultiByte(CP_UTF8, 0, output_desc.DeviceName, -1, device_name, sizeof(device_name), nullptr, nullptr);

                info << "  Fullscreen Output: " << device_name << " (";
                info << (output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left) << "x";
                info << (output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top) << ")";
            }
            output->Release();
        }
    }
    else
    {
        info << "  DXGI Fullscreen State: Query failed (" << format_hresult(hr) << ")";
    }

    reshade::log::message(reshade::log::level::info, info.str().c_str());
}

void DebugLogger::log_monitor_info(HMONITOR hmonitor) const
{
    if (hmonitor == nullptr)
        return;

    MONITORINFOEXA monitor_info;  // Use ANSI version
    monitor_info.cbSize = sizeof(MONITORINFOEXA);

    if (GetMonitorInfoA(hmonitor, &monitor_info))  // Use ANSI version
    {
        const RECT& rect = monitor_info.rcMonitor;

        std::ostringstream info;
        info << "  Monitor: " << monitor_info.szDevice << " (";
        info << (rect.right - rect.left) << "x" << (rect.bottom - rect.top) << ") at (";
        info << rect.left << "," << rect.top << ")";

        reshade::log::message(reshade::log::level::info, info.str().c_str());
    }
}
