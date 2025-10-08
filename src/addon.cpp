/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

// Export addon metadata
extern "C" __declspec(dllexport) const char* NAME = "Swapchain Override";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Forces a specific swapchain resolution and fullscreen mode (configurable) while maintaining application compatibility.\n\n"
    "Configuration via ReShade.ini:\n"
    "[SWAPCHAIN_OVERRIDE]\n"
    "ForceSwapchainResolution=<width>x<height>  (e.g., 3840x2160 for 4K, or 0x0 to disable)\n"
    "SwapchainScalingFilter=<0-2>  (0=Point, 1=Linear, 2=Anisotropic)\n"
    "FullscreenMode=<0-2>  (0=Unchanged, 1=Borderless, 2=Exclusive)\n"
    "BlockFullscreenChanges=<0-1>  (0=Allow, 1=Block Alt+Enter toggles)\n"
    "TargetMonitor=<0+>  (0=Primary, 1+=Secondary monitors)";
