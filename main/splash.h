// Copyright 2025 Ruben Berenguel

#pragma once

#include "display_context.h"

// Draw the generative-mountains splash screen.
// Leaves the bottom ~50px clear for the caller to add status text.
void draw_splash(lgfx::LovyanGFX* display);

void draw_bt_icon(lgfx::LovyanGFX* display, uint32_t color);
