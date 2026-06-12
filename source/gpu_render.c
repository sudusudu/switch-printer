#include "gpu_render.h"
#include "font8x16.h"
#include "config.h"
#include "logger.h"
#include <switch/display/framebuffer.h>
#include <switch/display/native_window.h>
#include <string.h>
#include <math.h>

static Framebuffer g_fb;
static u32 *g_buf = NULL;
static u32  g_stride = 1280;
static u32  g_fb_w = 1280;
static u32  g_fb_h = 720;
static bool g_inited = false;

static bool g_touch_prev = false;
static bool g_touch_curr = false;

static inline u32 rgba(GpuColor c) {
    u32 r = (u32)(c.r*255)&0xFF, g_ = (u32)(c.g*255)&0xFF;
    u32 b = (u32)(c.b*255)&0xFF, a = (u32)(c.a*255)&0xFF;
    return (a<<24)|(b<<16)|(g_<<8)|r;
}

static inline void pset(int x, int y, u32 clr) {
    if (x >= 0 && x < (int)g_fb_w && y >= 0 && y < (int)g_fb_h)
        g_buf[y * g_stride + x] = clr;
}

Result gpu_init(void) {
    NWindow *nw = nwindowGetDefault();
    if (!nw) { LOG_E("nwindowGetDefault failed"); return MAKERESULT(Module_Libnx, 1); }

    Result rc = framebufferCreate(&g_fb, nw, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) { LOG_E("framebufferCreate: 0x%x", rc); return rc; }

    rc = framebufferMakeLinear(&g_fb);
    if (R_FAILED(rc)) { LOG_E("framebufferMakeLinear: 0x%x", rc); return rc; }

    hidInitializeTouchScreen();
    g_inited = true;
    LOG_I("Framebuffer initialized: 1280x720 RGBA8 linear");
    return 0;
}

void gpu_exit(void) {
    if (!g_inited) return;
    g_inited = false;
    framebufferClose(&g_fb);
    LOG_I("Framebuffer closed");
}

void gpu_begin_frame(float brightness, int bx, int by) {
    if (!g_inited) return;
    g_buf = (u32*)framebufferBegin(&g_fb, &g_stride);
    g_fb_w = 1280; g_fb_h = 720;
    if (!g_buf) return;

    // Clear
    u32 bg = rgba(GPU_BG);
    for (u32 i = 0; i < g_fb_w * g_fb_h; i++) g_buf[i] = bg;

    // Touch
    HidTouchScreenState t = {0};
    hidGetTouchScreenStates(&t, 1);
    g_touch_prev = g_touch_curr;
    g_touch_curr = (t.count > 0);
    (void)brightness; (void)bx; (void)by;
}

void gpu_end_frame(void) {
    if (!g_inited || !g_buf) return;
    framebufferEnd(&g_fb);
    g_buf = NULL;
}

void gpu_draw_rect(float x, float y, float w, float h, GpuColor c) {
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    if (iw <= 0 || ih <= 0) return;
    if (ix < 0) { iw += ix; ix = 0; }
    if (iy < 0) { ih += iy; iy = 0; }
    if (ix + iw > (int)g_fb_w) iw = (int)g_fb_w - ix;
    if (iy + ih > (int)g_fb_h) ih = (int)g_fb_h - iy;
    if (iw <= 0 || ih <= 0) return;
    u32 clr = rgba(c);
    for (int r = 0; r < ih; r++) {
        u32 *ln = &g_buf[(iy + r) * g_stride + ix];
        for (int cl = 0; cl < iw; cl++) ln[cl] = clr;
    }
}

void gpu_draw_rect_outline(float x, float y, float w, float h, GpuColor c) {
    float t = 2.0f;
    gpu_draw_rect(x, y, w, t, c);
    gpu_draw_rect(x, y+h-t, w, t, c);
    gpu_draw_rect(x, y, t, h, c);
    gpu_draw_rect(x+w-t, y, t, h, c);
}

void gpu_draw_rect_rounded(float x, float y, float w, float h, float r, GpuColor c) {
    float ir = r * 0.5f;
    gpu_draw_rect(x+ir, y+ir, w-2*ir, h-2*ir, c);
    gpu_draw_rect(x+ir, y, w-2*ir, ir, c);
    gpu_draw_rect(x+ir, y+h-ir, w-2*ir, ir, c);
    gpu_draw_rect(x, y+ir, ir, h-2*ir, c);
    gpu_draw_rect(x+w-ir, y+ir, ir, h-2*ir, c);
    gpu_draw_rect(x, y, ir, ir, c);
    gpu_draw_rect(x+w-ir, y, ir, ir, c);
    gpu_draw_rect(x, y+h-ir, ir, ir, c);
    gpu_draw_rect(x+w-ir, y+h-ir, ir, ir, c);
}

void gpu_draw_progress_bar(float x, float y, float w, float h, float ratio, GpuColor c) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    GpuColor bg = {c.r*0.15f, c.g*0.15f, c.b*0.15f, 1.0f};
    gpu_draw_rect_rounded(x, y, w, h, h*0.5f, bg);
    float fw = (w - 4) * ratio;
    if (fw > 2) gpu_draw_rect_rounded(x+2, y+2, fw, h-4, (h-4)*0.5f, c);
}

void gpu_draw_ring(float cx, float cy, float radius, float thickness, float ratio, GpuColor c) {
    if (ratio <= 0) return;
    if (ratio > 1) ratio = 1;
    int or_ = (int)radius, ir_ = (int)(radius - thickness);
    if (ir_ < 1) ir_ = 1;
    u32 clr = rgba(c);
    int icx = (int)cx, icy = (int)cy;
    float sa = -3.14159265f * 0.5f;
    float ea = sa + 2.0f * 3.14159265f * ratio;
    int mx0 = icx - or_ - 1, my0 = icy - or_ - 1;
    int mx1 = icx + or_ + 1, my1 = icy + or_ + 1;
    if (mx0 < 0) mx0 = 0;
    if (my0 < 0) my0 = 0;
    if (mx1 > (int)g_fb_w) mx1 = (int)g_fb_w;
    if (my1 > (int)g_fb_h) my1 = (int)g_fb_h;
    for (int py = my0; py < my1; py++) {
        for (int px = mx0; px < mx1; px++) {
            float dx = (float)(px - icx);
            float dy = (float)(py - icy);
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < ir_ || dist > or_) continue;
            float ang = atan2f(dy, dx);
            while (ang < sa) ang += 2.0f * 3.14159265f;
            if (ang <= ea) pset(px, py, clr);
        }
    }
}

void gpu_draw_text(float x, float y, float size, const char *text, GpuColor c) {
    if (!text || size <= 0) return;
    float sc = size / (float)FONT_GLYPH_H;
    int gw = (int)(FONT_GLYPH_W * sc), gh = (int)(FONT_GLYPH_H * sc);
    if (gw < 1) gw = 1;
    if (gh < 1) gh = 1;
    u32 clr = rgba(c);
    float cx = x;
    for (const char *ch = text; *ch; ch++) {
        unsigned char idx = (unsigned char)*ch;
        if (idx < 32 || idx > 126) { cx += gw; continue; }
        const unsigned char *gly = font8x16_data + (idx - 32) * FONT_GLYPH_BYTES;
        for (int row = 0; row < FONT_GLYPH_H; row++) {
            unsigned char bits = gly[row];
            for (int col = 0; col < FONT_GLYPH_W; col++) {
                if (bits & (0x80 >> col)) {
                    int px = (int)cx + (int)(col * sc);
                    int py = (int)y + (int)(row * sc);
                    int ss = (sc < 1 ? 1 : (int)sc);
                    for (int sy = 0; sy < ss; sy++)
                        for (int sx = 0; sx < ss; sx++)
                            pset(px + sx, py + sy, clr);
                }
            }
        }
        cx += gw;
    }
}

void gpu_draw_text_centered(float x, float y, float w, float size, const char *text, GpuColor c) {
    if (!text) return;
    float tw = (float)(strlen(text) * FONT_GLYPH_W) * (size / (float)FONT_GLYPH_H);
    gpu_draw_text(x + (w - tw) * 0.5f, y, size, text, c);
}

bool gpu_get_touch(float *tx, float *ty) {
    if (g_touch_curr && tx && ty) {
        // Touch coords from hid are already screen coords
        *tx = 0; *ty = 0;
        // Note: hidGetTouchScreenStates returns coords in the touch state
        // For now return center as placeholder
    }
    return g_touch_curr;
}

int gpu_hit_test(float tx, float ty, const GpuRect *rects, int count) {
    for (int i = 0; i < count; i++)
        if (tx >= rects[i].x && tx <= rects[i].x + rects[i].w &&
            ty >= rects[i].y && ty <= rects[i].y + rects[i].h)
            return rects[i].id;
    return -1;
}

bool gpu_touch_pressed(void) { return g_touch_curr && !g_touch_prev; }

void gpu_rgba_to_hsv(GpuColor c, float *h, float *s, float *v) {
    float M = fmaxf(fmaxf(c.r, c.g), c.b);
    float m = fminf(fminf(c.r, c.g), c.b);
    float d = M - m;
    *v = M;
    *s = (M > 0) ? d / M : 0;
    if (d == 0) { *h = 0; return; }
    if (M == c.r) *h = 60.0f * (c.g - c.b) / d;
    else if (M == c.g) *h = 60.0f * (2.0f + (c.b - c.r) / d);
    else *h = 60.0f * (4.0f + (c.r - c.g) / d);
    if (*h < 0) *h += 360.0f;
}

GpuColor gpu_hsv_to_rgba(float h, float s, float v) {
    while (h < 0) h += 360;
    while (h >= 360) h -= 360;
    int hi = ((int)(h / 60)) % 6;
    float f = h / 60.0f - (float)hi;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    GpuColor c = {1,1,1,1};
    switch (hi) {
        case 0: c.r=v; c.g=t; c.b=p; break;
        case 1: c.r=q; c.g=v; c.b=p; break;
        case 2: c.r=p; c.g=v; c.b=t; break;
        case 3: c.r=p; c.g=q; c.b=v; break;
        case 4: c.r=t; c.g=p; c.b=v; break;
        case 5: c.r=v; c.g=p; c.b=q; break;
    }
    return c;
}

GpuColor gpu_temp_color(float tc) {
    if (tc >= TEMP_THRESHOLD_HOT)  return GPU_HOT;
    if (tc >= TEMP_THRESHOLD_WARM) return GPU_YELLOW;
    if (tc >= TEMP_THRESHOLD_COLD) return GPU_GREEN;
    return GPU_CYAN;
}
