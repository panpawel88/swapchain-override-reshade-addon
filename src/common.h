/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

// ReShade API
#include <reshade.hpp>

// Windows headers
#include <Windows.h>
#include <dxgi.h>

// Standard library
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <mutex>
#include <string>

// Type aliases for clarity
using SwapchainNativeHandle = uint64_t;  // Native swapchain handle (IDXGISwapChain*, VkSwapchainKHR, etc.)
using WindowHandle = void*;              // Window handle (HWND, etc.)

// Forward declarations
class Config;
class WindowHooks;
class SwapchainManager;
class OverlayManager;
struct SwapchainData;
