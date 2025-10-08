/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"
#include "config.h"
#include <safetyhook.hpp>

class WindowHooks
{
public:
    // Singleton access
    static WindowHooks& get_instance();

    // Install/uninstall WinAPI hooks
    bool install();
    void uninstall();

private:
    WindowHooks() = default;
    ~WindowHooks() = default;

    // Delete copy/move constructors
    WindowHooks(const WindowHooks&) = delete;
    WindowHooks& operator=(const WindowHooks&) = delete;
    WindowHooks(WindowHooks&&) = delete;
    WindowHooks& operator=(WindowHooks&&) = delete;

    // SafetyHook inline hook objects
    SafetyHookInline create_window_ex_a_hook_;
    SafetyHookInline create_window_ex_w_hook_;
    SafetyHookInline set_window_long_a_hook_;
    SafetyHookInline set_window_long_w_hook_;
#ifdef _WIN64
    SafetyHookInline set_window_long_ptr_a_hook_;
    SafetyHookInline set_window_long_ptr_w_hook_;
#endif
    SafetyHookInline set_window_pos_hook_;
    SafetyHookInline adjust_window_rect_hook_;
    SafetyHookInline adjust_window_rect_ex_hook_;

    // Hook state tracking
    bool hooks_installed_ = false;
    int addon_instance_count_ = 0;
    std::mutex hook_state_mutex_;

    // Hook function implementations
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

    // Helper structures and functions
    struct MonitorEnumData
    {
        int target_index;
        int current_index;
        HMONITOR found_monitor;
    };

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
    static bool get_target_monitor_rect(RECT* out_rect);
};
