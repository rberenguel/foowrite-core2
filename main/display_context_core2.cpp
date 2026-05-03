#include "display_context.h"
#include "lgfx_config.h"

static LGFX s_display;

void display_init() {
    s_display.init();
    s_display.setRotation(1);  // landscape
}

lgfx::LovyanGFX& display_get() {
    return s_display;
}

void display_set_rotation(int rot) {
    s_display.setRotation(rot);
}

void display_commit() {
    // No-op: Core2 LovyanGFX writes pixels immediately to the SPI display.
}

void display_commit_partial(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {
    // No-op: same reason as display_commit().
}

void display_blit(const uint16_t* src,
                  int src_x, int src_y, int src_w,
                  int dst_x, int dst_y, int w, int h) {
    s_display.pushImage(dst_x, dst_y, w, h,
                        src + src_y * src_w + src_x);
}
