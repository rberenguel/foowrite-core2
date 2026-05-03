#pragma once

// Display abstraction — hides the difference between:
//   Core2:     LovyanGFX hardware display (ILI9342C, SPI)
//   Waveshare: LovyanGFX sprite (software framebuffer) + raw SPI flush
//
// Include this instead of lgfx_config.h anywhere display access is needed.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Initialise the display hardware and allocate any framebuffers.
// Must be called before display_get() or display_commit().
void display_init();

// Returns the LovyanGFX canvas for drawing.
// Core2:     reference to the hardware LGFX display.
// Waveshare: reference to a full-screen LGFX_Sprite.
lgfx::LovyanGFX& display_get();

// Set display rotation.
// Core2:     forwards to display.setRotation(rot).
// Waveshare: no-op (rotation is baked into the commit path).
void display_set_rotation(int rot);

// Flush the canvas to the physical screen.
// Core2:     no-op (LovyanGFX writes pixels immediately on every draw call).
// Waveshare: rotate the sprite buffer and push via raw SPI.
void display_commit();

// Flush only the logical rectangle (x, y, w, h) to the physical screen.
// Core2:  no-op.
// Waveshare: rotates and pushes only the subset of the framebuffer that changed,
//            greatly reducing SPI traffic on each keystroke.
void display_commit_partial(int x, int y, int w, int h);
