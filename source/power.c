#include "power.h"
#include "config.h"
#include "logger.h"
#include <time.h>

// ============================================================
// 电源管理 + Applet 生命周期 实现
// ============================================================

static bool  g_sleep_disabled = false;
static float g_brightness     = 1.0f;
static u64   g_last_interact  = 0;       // armGetSystemTick() 时间戳
static bool  g_dimmed         = false;
static int   g_burnin_phase   = 0;       // 0~3 微移阶段
static u64   g_burnin_last    = 0;
static int   g_burnin_dx      = 0;
static int   g_burnin_dy      = 0;

// 上一次的焦点状态（检测变化沿）
static bool  g_was_focused    = true;

Result power_init(void) {
    g_last_interact = armGetSystemTick();
    g_was_focused   = true;
    LOG_I("Power management initialized (dim=%ds, burnin=%ds)",
          UI_DIM_TIMEOUT_SEC, UI_BURNIN_INTERVAL_SEC);
    return 0;
}

void power_exit(void) {
    if (g_sleep_disabled) {
        appletSetAutoSleepDisabled(false);
        g_sleep_disabled = false;
    }
    LOG_I("Power management exited");
}

void power_print_start(void) {
    if (!g_sleep_disabled) {
        Result rc = appletSetAutoSleepDisabled(true);
        if (R_SUCCEEDED(rc)) {
            g_sleep_disabled = true;
            LOG_P("Auto-sleep disabled for print job");
        } else {
            LOG_E("Failed to disable auto-sleep: 0x%x", rc);
        }
    }
}

void power_print_end(void) {
    if (g_sleep_disabled) {
        appletSetAutoSleepDisabled(false);
        g_sleep_disabled = false;
        LOG_P("Auto-sleep restored");
    }
}

bool power_update(u64 dt_ns) {
    (void)dt_ns;
    bool dimmed_changed = false;

    // --- 空闲降亮度 ---
    if (UI_DIM_TIMEOUT_SEC > 0 && !g_dimmed) {
        u64 elapsed = armGetSystemTick() - g_last_interact;
        u64 timeout_ticks = armNsToTicks((u64)UI_DIM_TIMEOUT_SEC * 1000000000ULL);
        if (elapsed >= timeout_ticks) {
            g_dimmed = true;
            g_brightness = UI_DIM_BRIGHTNESS;
            dimmed_changed = true;
            LOG_I("Screen dimmed to %.0f%%", UI_DIM_BRIGHTNESS * 100);
        }
    }

    // --- 防烧屏像素微移 ---
    if (g_dimmed && UI_BURNIN_SHIFT_PX > 0 && UI_BURNIN_INTERVAL_SEC > 0) {
        u64 elapsed = armGetSystemTick() - g_burnin_last;
        u64 interval_ticks = armNsToTicks((u64)UI_BURNIN_INTERVAL_SEC * 1000000000ULL);
        if (elapsed >= interval_ticks) {
            g_burnin_phase = (g_burnin_phase + 1) & 3;
            switch (g_burnin_phase) {
                case 0: g_burnin_dx = 0;                g_burnin_dy = 0;                break;
                case 1: g_burnin_dx = UI_BURNIN_SHIFT_PX; g_burnin_dy = 0;                break;
                case 2: g_burnin_dx = UI_BURNIN_SHIFT_PX; g_burnin_dy = UI_BURNIN_SHIFT_PX; break;
                case 3: g_burnin_dx = 0;                 g_burnin_dy = UI_BURNIN_SHIFT_PX; break;
            }
            g_burnin_last = armGetSystemTick();
        }
    }

    return dimmed_changed;
}

FocusEvent power_check_focus(void) {
    AppletFocusState state = appletGetFocusState();

    if (state == AppletFocusState_InFocus && !g_was_focused) {
        g_was_focused = true;
        LOG_I("Applet focus returned");
        return FOCUS_RETURNED;
    }

    if (state != AppletFocusState_InFocus && g_was_focused) {
        g_was_focused = false;
        LOG_I("Applet focus lost (HOME pressed or overlay)");
        return FOCUS_LOST;
    }

    return FOCUS_OK;
}

float power_get_brightness(void) {
    return g_brightness;
}

void power_get_burnin_offset(int *dx, int *dy) {
    if (dx) *dx = g_burnin_dx;
    if (dy) *dy = g_burnin_dy;
}

void power_on_interaction(void) {
    g_last_interact = armGetSystemTick();
    if (g_dimmed) {
        g_dimmed = false;
        g_brightness = 1.0f;
        g_burnin_dx = 0;
        g_burnin_dy = 0;
        g_burnin_phase = 0;
        LOG_I("Screen brightness restored");
    }
}
