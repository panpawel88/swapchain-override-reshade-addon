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
#include <Windows.h>
#include <dxgi.h>
#include <safetyhook.hpp>
#include "shader_bytecode.h"

using namespace reshade::api;

// ============================================================================
// Configuration
// ============================================================================

enum class FullscreenMode
{
    Unchanged = 0,   // Don't modify fullscreen behavior (default)
    Borderless = 1,  // Force borderless fullscreen (windowed)
    Exclusive = 2    // Force exclusive fullscreen
};

struct SwapchainConfig
{
    uint32_t force_width = 0;
    uint32_t force_height = 0;
    filter_mode scaling_filter = filter_mode::min_mag_mip_linear;
    FullscreenMode fullscreen_mode = FullscreenMode::Unchanged;
    bool block_fullscreen_changes = false;
    int target_monitor = 0; // 0 = primary, 1+ = secondary monitors
};

static SwapchainConfig g_config;

static void load_config()
{
    // Read forced resolution
    char resolution_string[32] = {};
    size_t resolution_string_size = sizeof(resolution_string);

    if (reshade::get_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "ForceSwapchainResolution", resolution_string, &resolution_string_size))
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
        reshade::set_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "ForceSwapchainResolution", "3840x2160");
        g_config.force_width = 3840;
        g_config.force_height = 2160;
    }

    // Read scaling filter
    int filter_value = 1; // Default to linear
    if (reshade::get_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "SwapchainScalingFilter", filter_value))
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
        reshade::set_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "SwapchainScalingFilter", 1);
    }

    // Read fullscreen mode
    int fullscreen_mode_value = 0; // Default to Unchanged
    if (reshade::get_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "FullscreenMode", fullscreen_mode_value))
    {
        switch (fullscreen_mode_value)
        {
        case 0:
            g_config.fullscreen_mode = FullscreenMode::Unchanged;
            break;
        case 1:
            g_config.fullscreen_mode = FullscreenMode::Borderless;
            break;
        case 2:
            g_config.fullscreen_mode = FullscreenMode::Exclusive;
            break;
        default:
            g_config.fullscreen_mode = FullscreenMode::Unchanged;
            break;
        }
    }
    else
    {
        reshade::set_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "FullscreenMode", 0);
    }

    // Read block fullscreen changes
    if (!reshade::get_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "BlockFullscreenChanges", g_config.block_fullscreen_changes))
    {
        reshade::set_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "BlockFullscreenChanges", false);
    }

    // Read target monitor
    if (!reshade::get_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "TargetMonitor", g_config.target_monitor))
    {
        reshade::set_config_value(nullptr, "SWAPCHAIN_OVERRIDE", "TargetMonitor", 0);
        g_config.target_monitor = 0;
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
    std::vector<resource_view> proxy_srvs;  // Shader resource views for proxy textures
    std::vector<resource> actual_back_buffers;  // Actual back buffer resources for comparison

    // Pipeline objects for fullscreen draw
    pipeline copy_pipeline = {};
    pipeline_layout copy_pipeline_layout = {};
    sampler copy_sampler = {};

    device* device_ptr = nullptr;

    // Helper: Find proxy RTV index from actual back buffer resource
    int find_proxy_index(resource actual_resource) const
    {
        for (size_t i = 0; i < actual_back_buffers.size(); ++i)
        {
            if (actual_back_buffers[i].handle == actual_resource.handle)
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
// WinAPI Hooks for Borderless Fullscreen
// ============================================================================

// SafetyHook inline hook objects
static SafetyHookInline g_create_window_ex_a_hook;
static SafetyHookInline g_create_window_ex_w_hook;
static SafetyHookInline g_set_window_long_a_hook;
static SafetyHookInline g_set_window_long_w_hook;
#ifdef _WIN64
static SafetyHookInline g_set_window_long_ptr_a_hook;
static SafetyHookInline g_set_window_long_ptr_w_hook;
#endif
static SafetyHookInline g_set_window_pos_hook;
static SafetyHookInline g_adjust_window_rect_hook;
static SafetyHookInline g_adjust_window_rect_ex_hook;

// Global state for hook installation tracking
static bool g_hooks_installed = false;
static int g_addon_instance_count = 0;
static std::mutex g_hook_state_mutex;

// Forward declarations of hook functions and helpers
static void uninstall_window_hooks();
static HWND WINAPI hooked_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
static HWND WINAPI hooked_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
static LONG WINAPI hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
static LONG WINAPI hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong);
#ifdef _WIN64
static LONG_PTR WINAPI hooked_SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
static LONG_PTR WINAPI hooked_SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
#endif
static BOOL WINAPI hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
static BOOL WINAPI hooked_AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
static BOOL WINAPI hooked_AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);

// Helper function to install WinAPI hooks
static bool install_window_hooks()
{
    std::lock_guard<std::mutex> lock(g_hook_state_mutex);

    // Increment instance counter
    g_addon_instance_count++;

    // If hooks are already installed, just return success
    if (g_hooks_installed)
    {
        reshade::log::message(reshade::log::level::info,
            ("WinAPI hooks already installed (instance count: " + std::to_string(g_addon_instance_count) + ")").c_str());
        return true;
    }

    // Only install hooks if borderless mode is enabled
    if (g_config.fullscreen_mode != FullscreenMode::Borderless)
    {
        reshade::log::message(reshade::log::level::info, "Borderless mode not enabled, skipping WinAPI hooks");
        return true; // No hooks needed if not in borderless mode
    }

    reshade::log::message(reshade::log::level::info, "Installing WinAPI hooks for borderless fullscreen mode...");

    g_create_window_ex_a_hook = safetyhook::create_inline(reinterpret_cast<void*>(CreateWindowExA), reinterpret_cast<void*>(hooked_CreateWindowExA));
    g_create_window_ex_w_hook = safetyhook::create_inline(reinterpret_cast<void*>(CreateWindowExW), reinterpret_cast<void*>(hooked_CreateWindowExW));
    g_set_window_long_a_hook = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongA), reinterpret_cast<void*>(hooked_SetWindowLongA));
    g_set_window_long_w_hook = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongW), reinterpret_cast<void*>(hooked_SetWindowLongW));
#ifdef _WIN64
    g_set_window_long_ptr_a_hook = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongPtrA), reinterpret_cast<void*>(hooked_SetWindowLongPtrA));
    g_set_window_long_ptr_w_hook = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongPtrW), reinterpret_cast<void*>(hooked_SetWindowLongPtrW));
#endif
    g_set_window_pos_hook = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowPos), reinterpret_cast<void*>(hooked_SetWindowPos));
    g_adjust_window_rect_hook = safetyhook::create_inline(reinterpret_cast<void*>(AdjustWindowRect), reinterpret_cast<void*>(hooked_AdjustWindowRect));
    g_adjust_window_rect_ex_hook = safetyhook::create_inline(reinterpret_cast<void*>(AdjustWindowRectEx), reinterpret_cast<void*>(hooked_AdjustWindowRectEx));

    bool all_hooks_valid = g_create_window_ex_a_hook && g_create_window_ex_w_hook &&
                           g_set_window_long_a_hook && g_set_window_long_w_hook &&
                           g_set_window_pos_hook &&
                           g_adjust_window_rect_hook && g_adjust_window_rect_ex_hook;

#ifdef _WIN64
    all_hooks_valid = all_hooks_valid && g_set_window_long_ptr_a_hook && g_set_window_long_ptr_w_hook;
#endif

    if (all_hooks_valid)
    {
        g_hooks_installed = true;
        reshade::log::message(reshade::log::level::info, "WinAPI hooks installed successfully");
        return true;
    }
    else
    {
        reshade::log::message(reshade::log::level::error, "Failed to install one or more WinAPI hooks");
        // Note: uninstall_window_hooks() will be called without the lock, so we release it here
        // by letting this function return, then the caller can handle cleanup
        return false;
    }
}

// Helper function to uninstall WinAPI hooks
static void uninstall_window_hooks()
{
    std::lock_guard<std::mutex> lock(g_hook_state_mutex);

    // Decrement instance counter
    if (g_addon_instance_count > 0)
    {
        g_addon_instance_count--;
    }

    // Only uninstall hooks when the last instance is destroyed
    if (g_addon_instance_count > 0)
    {
        reshade::log::message(reshade::log::level::info,
            ("WinAPI hooks still in use (instance count: " + std::to_string(g_addon_instance_count) + ")").c_str());
        return;
    }

    // No more instances, uninstall hooks if they were installed
    if (!g_hooks_installed)
    {
        reshade::log::message(reshade::log::level::info, "WinAPI hooks were not installed, nothing to uninstall");
        return;
    }

    reshade::log::message(reshade::log::level::info, "Uninstalling WinAPI hooks (last instance destroyed)...");

    // SafetyHook uses RAII, just reset the hooks
    g_create_window_ex_a_hook = {};
    g_create_window_ex_w_hook = {};
    g_set_window_long_a_hook = {};
    g_set_window_long_w_hook = {};
#ifdef _WIN64
    g_set_window_long_ptr_a_hook = {};
    g_set_window_long_ptr_w_hook = {};
#endif
    g_set_window_pos_hook = {};
    g_adjust_window_rect_hook = {};
    g_adjust_window_rect_ex_hook = {};

    g_hooks_installed = false;

    reshade::log::message(reshade::log::level::info, "WinAPI hooks uninstalled");
}

// Helper structure for monitor enumeration
struct MonitorEnumData
{
    int target_index;
    int current_index;
    HMONITOR found_monitor;
};

// Callback for EnumDisplayMonitors
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    auto* data = reinterpret_cast<MonitorEnumData*>(dwData);
    if (data->current_index == data->target_index)
    {
        data->found_monitor = hMonitor;
        return FALSE; // Stop enumeration
    }
    data->current_index++;
    return TRUE; // Continue enumeration
}

// Helper function to get target monitor info for borderless fullscreen
static bool get_target_monitor_rect(RECT* out_rect)
{
    HMONITOR target_monitor = nullptr;

    if (g_config.target_monitor == 0)
    {
        // Use primary monitor
        target_monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }
    else
    {
        // Enumerate monitors to find the Nth one
        MonitorEnumData data = {};
        data.target_index = g_config.target_monitor;
        data.current_index = 0;
        data.found_monitor = nullptr;

        EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));

        if (data.found_monitor != nullptr)
        {
            target_monitor = data.found_monitor;
        }
        else
        {
            // Fall back to primary if target monitor not found
            reshade::log::message(reshade::log::level::warning,
                ("Target monitor " + std::to_string(g_config.target_monitor) + " not found, using primary monitor").c_str());
            target_monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        }
    }

    if (target_monitor != nullptr)
    {
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfo(target_monitor, &mi))
        {
            *out_rect = mi.rcMonitor;
            return true;
        }
    }

    return false;
}

// Hook implementations
static HWND WINAPI hooked_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
    {
        // Modify to borderless fullscreen
        dwStyle = (dwStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE;
        dwExStyle = dwExStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);

        // Get target monitor dimensions
        RECT monitor_rect;
        if (get_target_monitor_rect(&monitor_rect))
        {
            X = monitor_rect.left;
            Y = monitor_rect.top;
            nWidth = monitor_rect.right - monitor_rect.left;
            nHeight = monitor_rect.bottom - monitor_rect.top;

            reshade::log::message(reshade::log::level::debug,
                ("CreateWindowExA: Forcing borderless fullscreen " + std::to_string(nWidth) + "x" + std::to_string(nHeight)).c_str());
        }
    }

    return g_create_window_ex_a_hook.call<HWND>(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

static HWND WINAPI hooked_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
    {
        // Modify to borderless fullscreen
        dwStyle = (dwStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE;
        dwExStyle = dwExStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);

        // Get target monitor dimensions
        RECT monitor_rect;
        if (get_target_monitor_rect(&monitor_rect))
        {
            X = monitor_rect.left;
            Y = monitor_rect.top;
            nWidth = monitor_rect.right - monitor_rect.left;
            nHeight = monitor_rect.bottom - monitor_rect.top;

            reshade::log::message(reshade::log::level::debug,
                ("CreateWindowExW: Forcing borderless fullscreen " + std::to_string(nWidth) + "x" + std::to_string(nHeight)).c_str());
        }
    }

    return g_create_window_ex_w_hook.call<HWND>(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

static LONG WINAPI hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG>((static_cast<DWORD>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return g_set_window_long_a_hook.call<LONG>(hWnd, nIndex, dwNewLong);
}

static LONG WINAPI hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG>((static_cast<DWORD>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return g_set_window_long_w_hook.call<LONG>(hWnd, nIndex, dwNewLong);
}

#ifdef _WIN64
static LONG_PTR WINAPI hooked_SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG_PTR>((static_cast<ULONG_PTR>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return g_set_window_long_ptr_a_hook.call<LONG_PTR>(hWnd, nIndex, dwNewLong);
}

static LONG_PTR WINAPI hooked_SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG_PTR>((static_cast<ULONG_PTR>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return g_set_window_long_ptr_w_hook.call<LONG_PTR>(hWnd, nIndex, dwNewLong);
}
#endif

static BOOL WINAPI hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
    {
        // Don't modify if both SWP_NOSIZE and SWP_NOMOVE are set
        if ((uFlags & SWP_NOSIZE) && (uFlags & SWP_NOMOVE))
        {
            return g_set_window_pos_hook.call<BOOL>(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
        }

        // Get target monitor dimensions
        RECT monitor_rect;
        if (get_target_monitor_rect(&monitor_rect))
        {
            X = monitor_rect.left;
            Y = monitor_rect.top;
            cx = monitor_rect.right - monitor_rect.left;
            cy = monitor_rect.bottom - monitor_rect.top;

            // Remove flags that would prevent our changes
            uFlags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
    }

    return g_set_window_pos_hook.call<BOOL>(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static BOOL WINAPI hooked_AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
    {
        // Return the rect unchanged (pretend no window decoration)
        return TRUE;
    }

    return g_adjust_window_rect_hook.call<BOOL>(lpRect, dwStyle, bMenu);
}

static BOOL WINAPI hooked_AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
    {
        // Return the rect unchanged (pretend no window decoration)
        return TRUE;
    }

    return g_adjust_window_rect_ex_hook.call<BOOL>(lpRect, dwStyle, bMenu, dwExStyle);
}

// ============================================================================
// Event Callbacks
// ============================================================================

static bool on_create_swapchain(device_api api, swapchain_desc& desc, void* hwnd)
{
    bool modified = false;

    // Handle resolution override
    if (g_config.force_width != 0 && g_config.force_height != 0)
    {
        const uint32_t requested_width = desc.back_buffer.texture.width;
        const uint32_t requested_height = desc.back_buffer.texture.height;

        if (requested_width != g_config.force_width || requested_height != g_config.force_height)
        {
            // Store the original requested size in pending map (keyed by window handle)
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

            modified = true;
        }
    }

    // Handle fullscreen mode override
    if (g_config.fullscreen_mode == FullscreenMode::Exclusive)
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
    else if (g_config.fullscreen_mode == FullscreenMode::Borderless)
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
            data->cleanup();
            return;
        }
    }

    // Store actual back buffer resources for comparison during bind interception
    for (uint32_t i = 0; i < back_buffer_count; ++i)
    {
        data->actual_back_buffers[i] = swapchain_ptr->get_back_buffer(i);
    }

    // Create copy pipeline (fullscreen triangle + texture sample)
    // Pipeline layout: sampler in slot 0, SRV in slot 0
    pipeline_layout_param layout_params[2];
    layout_params[0] = descriptor_range { 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::sampler };
    layout_params[1] = descriptor_range { 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::shader_resource_view };

    if (!device_ptr->create_pipeline_layout(2, layout_params, &data->copy_pipeline_layout))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy pipeline layout");
        data->cleanup();
        return;
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
        data->cleanup();
        return;
    }

    // Create sampler with the configured filter mode
    sampler_desc sampler_desc = {};
    sampler_desc.filter = g_config.scaling_filter;
    sampler_desc.address_u = texture_address_mode::clamp;
    sampler_desc.address_v = texture_address_mode::clamp;
    sampler_desc.address_w = texture_address_mode::clamp;

    if (!device_ptr->create_sampler(sampler_desc, &data->copy_sampler))
    {
        reshade::log::message(reshade::log::level::error, "Failed to create copy sampler");
        data->cleanup();
        return;
    }

    reshade::log::message(reshade::log::level::info,
        ("Created " + std::to_string(back_buffer_count) + " proxy textures at " +
        std::to_string(data->original_width) + "x" + std::to_string(data->original_height)).c_str());

    // Transition to exclusive fullscreen if configured (only on initial creation, not resize)
    if (!is_resize && g_config.fullscreen_mode == FullscreenMode::Exclusive)
    {
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

        // Get the underlying resource from the RTV
        resource rtv_resource = device_ptr->get_resource_from_view(modified_rtvs[i]);
        if (rtv_resource.handle == 0)
            continue;

        // Search through all swapchains to find if this resource matches an actual back buffer
        for (auto& pair : g_swapchain_data)
        {
            SwapchainData* data = pair.second;
            if (!data->override_active)
                continue;

            // Check if this device matches
            if (data->device_ptr != device_ptr)
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

static bool on_set_fullscreen_state(swapchain* swapchain_ptr, bool fullscreen, void* hmonitor)
{
    if (swapchain_ptr == nullptr)
        return false;

    // If exclusive mode is forced:
    // - Allow transitions TO fullscreen (including our own internal transition)
    // - Block transitions to windowed
    if (g_config.fullscreen_mode == FullscreenMode::Exclusive)
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
    if (g_config.fullscreen_mode == FullscreenMode::Borderless)
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
    if (g_config.block_fullscreen_changes)
    {
        reshade::log::message(reshade::log::level::debug,
            ("Blocked fullscreen state change attempt (requested: " + std::string(fullscreen ? "fullscreen" : "windowed") + ")").c_str());
        return true; // Return true to block the change
    }

    return false; // Allow the change
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

        // Install WinAPI hooks if borderless fullscreen mode is enabled
        install_window_hooks();

        // Register event callbacks
        reshade::register_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
        reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
        reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
        reshade::register_event<reshade::addon_event::present>(on_present);
        reshade::register_event<reshade::addon_event::set_fullscreen_state>(on_set_fullscreen_state);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        reshade::log::message(reshade::log::level::info, "Swapchain Override addon loaded");
        break;

    case DLL_PROCESS_DETACH:
        // Uninstall WinAPI hooks
        uninstall_window_hooks();

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
        reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(on_set_fullscreen_state);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);

        // Unregister the addon
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
