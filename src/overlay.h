/*
 * Swapchain Override Addon for ReShade
 * Copyright (C) 2025
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.h"

// Overlay manager class for displaying debug information
class OverlayManager
{
public:
    // Singleton access
    static OverlayManager& get_instance();

    // Install/uninstall overlay
    void install();
    void uninstall();

private:
    OverlayManager() = default;
    ~OverlayManager() = default;

    // Delete copy/move constructors
    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;
    OverlayManager(OverlayManager&&) = delete;
    OverlayManager& operator=(OverlayManager&&) = delete;

    // Overlay callback (static wrapper for ReShade)
    static void on_overlay(reshade::api::effect_runtime* runtime);

    // Actual overlay rendering implementation
    void render_overlay(reshade::api::effect_runtime* runtime);
};
