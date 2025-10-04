/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

// Export addon metadata
extern "C" __declspec(dllexport) const char* NAME = "Swapchain Override";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Forces a specific swapchain resolution (configurable) while maintaining application compatibility.\n\n"
    "Configuration via ReShade.ini:\n"
    "[APP]\n"
    "ForceSwapchainResolution=<width>,<height>  (e.g., 3840,2160 for 4K, or 0,0 to disable)\n"
    "SwapchainScalingFilter=<0-2>  (0=Point, 1=Linear, 2=Anisotropic)";
