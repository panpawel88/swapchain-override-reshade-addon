/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#include "window_hooks.h"

WindowHooks& WindowHooks::get_instance()
{
    static WindowHooks instance;
    return instance;
}

bool WindowHooks::install()
{
    std::lock_guard<std::mutex> lock(hook_state_mutex_);

    // Increment instance counter
    addon_instance_count_++;

    // If hooks are already installed, just return success
    if (hooks_installed_)
    {
        reshade::log::message(reshade::log::level::info,
            ("WinAPI hooks already installed (instance count: " + std::to_string(addon_instance_count_) + ")").c_str());
        return true;
    }

    // Only install hooks if borderless mode is enabled
    if (Config::get_instance().get_fullscreen_mode() != FullscreenMode::Borderless)
    {
        reshade::log::message(reshade::log::level::info, "Borderless mode not enabled, skipping WinAPI hooks");
        return true; // No hooks needed if not in borderless mode
    }

    reshade::log::message(reshade::log::level::info, "Installing WinAPI hooks for borderless fullscreen mode...");

    create_window_ex_a_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(CreateWindowExA), reinterpret_cast<void*>(hooked_CreateWindowExA));
    create_window_ex_w_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(CreateWindowExW), reinterpret_cast<void*>(hooked_CreateWindowExW));
    set_window_long_a_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongA), reinterpret_cast<void*>(hooked_SetWindowLongA));
    set_window_long_w_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongW), reinterpret_cast<void*>(hooked_SetWindowLongW));
#ifdef _WIN64
    set_window_long_ptr_a_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongPtrA), reinterpret_cast<void*>(hooked_SetWindowLongPtrA));
    set_window_long_ptr_w_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowLongPtrW), reinterpret_cast<void*>(hooked_SetWindowLongPtrW));
#endif
    set_window_pos_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(SetWindowPos), reinterpret_cast<void*>(hooked_SetWindowPos));
    adjust_window_rect_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(AdjustWindowRect), reinterpret_cast<void*>(hooked_AdjustWindowRect));
    adjust_window_rect_ex_hook_ = safetyhook::create_inline(reinterpret_cast<void*>(AdjustWindowRectEx), reinterpret_cast<void*>(hooked_AdjustWindowRectEx));

    bool all_hooks_valid = create_window_ex_a_hook_ && create_window_ex_w_hook_ &&
                           set_window_long_a_hook_ && set_window_long_w_hook_ &&
                           set_window_pos_hook_ &&
                           adjust_window_rect_hook_ && adjust_window_rect_ex_hook_;

#ifdef _WIN64
    all_hooks_valid = all_hooks_valid && set_window_long_ptr_a_hook_ && set_window_long_ptr_w_hook_;
#endif

    if (all_hooks_valid)
    {
        hooks_installed_ = true;
        reshade::log::message(reshade::log::level::info, "WinAPI hooks installed successfully");
        return true;
    }
    else
    {
        reshade::log::message(reshade::log::level::error, "Failed to install one or more WinAPI hooks");
        return false;
    }
}

void WindowHooks::uninstall()
{
    std::lock_guard<std::mutex> lock(hook_state_mutex_);

    // Decrement instance counter
    if (addon_instance_count_ > 0)
    {
        addon_instance_count_--;
    }

    // Only uninstall hooks when the last instance is destroyed
    if (addon_instance_count_ > 0)
    {
        reshade::log::message(reshade::log::level::info,
            ("WinAPI hooks still in use (instance count: " + std::to_string(addon_instance_count_) + ")").c_str());
        return;
    }

    // No more instances, uninstall hooks if they were installed
    if (!hooks_installed_)
    {
        reshade::log::message(reshade::log::level::info, "WinAPI hooks were not installed, nothing to uninstall");
        return;
    }

    reshade::log::message(reshade::log::level::info, "Uninstalling WinAPI hooks (last instance destroyed)...");

    // SafetyHook uses RAII, just reset the hooks
    create_window_ex_a_hook_ = {};
    create_window_ex_w_hook_ = {};
    set_window_long_a_hook_ = {};
    set_window_long_w_hook_ = {};
#ifdef _WIN64
    set_window_long_ptr_a_hook_ = {};
    set_window_long_ptr_w_hook_ = {};
#endif
    set_window_pos_hook_ = {};
    adjust_window_rect_hook_ = {};
    adjust_window_rect_ex_hook_ = {};

    hooks_installed_ = false;

    reshade::log::message(reshade::log::level::info, "WinAPI hooks uninstalled");
}

BOOL CALLBACK WindowHooks::MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
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

bool WindowHooks::get_target_monitor_rect(RECT* out_rect)
{
    HMONITOR target_monitor = nullptr;
    const Config& config = Config::get_instance();

    if (config.get_target_monitor() == 0)
    {
        // Use primary monitor
        target_monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }
    else
    {
        // Enumerate monitors to find the Nth one
        MonitorEnumData data = {};
        data.target_index = config.get_target_monitor();
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
                ("Target monitor " + std::to_string(config.get_target_monitor()) + " not found, using primary monitor").c_str());
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

HWND WINAPI WindowHooks::hooked_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless)
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

    return hooks.create_window_ex_a_hook_.call<HWND>(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

HWND WINAPI WindowHooks::hooked_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless)
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

    return hooks.create_window_ex_w_hook_.call<HWND>(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

LONG WINAPI WindowHooks::hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG>((static_cast<DWORD>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return hooks.set_window_long_a_hook_.call<LONG>(hWnd, nIndex, dwNewLong);
}

LONG WINAPI WindowHooks::hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG>((static_cast<DWORD>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return hooks.set_window_long_w_hook_.call<LONG>(hWnd, nIndex, dwNewLong);
}

#ifdef _WIN64
LONG_PTR WINAPI WindowHooks::hooked_SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG_PTR>((static_cast<ULONG_PTR>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return hooks.set_window_long_ptr_a_hook_.call<LONG_PTR>(hWnd, nIndex, dwNewLong);
}

LONG_PTR WINAPI WindowHooks::hooked_SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless && nIndex == GWL_STYLE)
    {
        // Force popup style, remove borders (cast to unsigned for bit operations)
        dwNewLong = static_cast<LONG_PTR>((static_cast<ULONG_PTR>(dwNewLong) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    }

    return hooks.set_window_long_ptr_w_hook_.call<LONG_PTR>(hWnd, nIndex, dwNewLong);
}
#endif

BOOL WINAPI WindowHooks::hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless)
    {
        // Don't modify if both SWP_NOSIZE and SWP_NOMOVE are set
        if ((uFlags & SWP_NOSIZE) && (uFlags & SWP_NOMOVE))
        {
            return hooks.set_window_pos_hook_.call<BOOL>(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
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

    return hooks.set_window_pos_hook_.call<BOOL>(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI WindowHooks::hooked_AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless)
    {
        // Return the rect unchanged (pretend no window decoration)
        return TRUE;
    }

    return hooks.adjust_window_rect_hook_.call<BOOL>(lpRect, dwStyle, bMenu);
}

BOOL WINAPI WindowHooks::hooked_AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    auto& hooks = WindowHooks::get_instance();

    if (Config::get_instance().get_fullscreen_mode() == FullscreenMode::Borderless)
    {
        // Return the rect unchanged (pretend no window decoration)
        return TRUE;
    }

    return hooks.adjust_window_rect_ex_hook_.call<BOOL>(lpRect, dwStyle, bMenu, dwExStyle);
}
