#include <switch.h>
#include <switch/runtime/pad.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"
#include "power.h"
#include "logger.h"
#include "crash.h"

#define CLR_RST    "\x1b[0m"
#define CLR_RED    "\x1b[31m"
#define CLR_GRN    "\x1b[32m"
#define CLR_YEL    "\x1b[33m"
#define CLR_CYAN   "\x1b[36m"
#define CLR_WHITE  "\x1b[37m"
#define CLR_BOLD   "\x1b[1m"
#define CLR_DIM    "\x1b[2m"

// ============================================================
// Unicode 1/8 block elements: 20-char bar = 160 discrete levels
// ============================================================
static const char *unicode_block_chars[9] = {
    " ",
    "\xe2\x96\x8f",  // U+258F
    "\xe2\x96\x8e",  // U+258E
    "\xe2\x96\x8d",  // U+258D
    "\xe2\x96\x8c",  // U+258C
    "\xe2\x96\x8b",  // U+258B
    "\xe2\x96\x8a",  // U+258A
    "\xe2\x96\x89",  // U+2589
    "\xe2\x96\x88"   // U+2588 (全块)
};

// 预组装渲染缓冲区（避免每帧数百次 printf 调用）
// UTF-8 3字节/字符: 横线 242B, 进度条 ~220B → 512B 安全
static char line_buf[512];

static void draw_progress_bar(int width, float value, float max_val,
                               const char *hi_color, const char *lo_color) {
    float r = (max_val > 0) ? value / max_val : 0;
    if (!isfinite(r)) r = 0;
    if (r < 0) r = 0;
    if (r > 1) r = 1;
    int full = (int)(r * width);
    int frac = ((int)(r * width * 8)) % 8;

    char *p = line_buf;
    p += sprintf(p, "%s", hi_color);
    for (int i = 0; i < full; i++) p += sprintf(p, "\xe2\x96\x88");
    if (full < width) {
        p += sprintf(p, "%s", unicode_block_chars[frac]);
        for (int i = full + 1; i < width; i++)
            p += sprintf(p, "%s\xc2\xb7", lo_color);
    }
    *p = '\0';
    printf("%s" CLR_RST, line_buf);
}

static const char *temp_to_color(float t) {
    if (t >= TEMP_THRESHOLD_HOT)  return CLR_RED;
    if (t >= TEMP_THRESHOLD_WARM) return CLR_YEL;
    if (t >= TEMP_THRESHOLD_COLD) return CLR_GRN;
    return CLR_CYAN;
}

static void pad_line_to_end(int used) {
    char *p = line_buf;
    for (int i = used; i < CONSOLE_WIDTH; i++) *p++ = ' ';
    *p++ = '\xe'; *p++ = '\x94'; *p++ = '\x82'; *p++ = '\n'; *p = '\0';
    printf("%s", line_buf);
}

static void draw_horizontal_rule(void) {
    char *p = line_buf;
    *p++ = '\xe'; *p++ = '\x94'; *p++ = '\x9c';
    for (int i = 0; i < CONSOLE_WIDTH; i++) {
        *p++ = '\xe'; *p++ = '\x94'; *p++ = '\x80';
    }
    *p++ = '\xe'; *p++ = '\x94'; *p++ = '\xa4'; *p++ = '\n'; *p = '\0';
    printf("%s", line_buf);
}

static void draw_ui(Ch340Device *dev) {
    // 防烧屏微移偏移（GPU Phase 2 会做亚像素级，console 版本做不了）
    consoleClear();
    PrinterStatus st;
    gcode_get_status_safe(&st);

    const char *state_names[] = {
        "\xe7\xa6\xbb\xe7\xba\xbf",        // 离线
        "\xe7\xa9\xba\xe9\x97\xb2",        // 空闲
        "\xe6\x89\x93\xe5\x8d\xb0\xe4\xb8\xad",  // 打印中
        "\xe6\x9a\x82\xe5\x81\x9c",        // 暂停
        "\xe9\x94\x99\xe8\xaf\xaf"         // 错误
    };
    const char *state_colors[] = {
        CLR_DIM,
        CLR_GRN,
        "\x1b[33m\x1b[1m",
        CLR_CYAN,
        "\x1b[31m\x1b[1m"
    };
    int s = (st.state >= 0 && st.state < PRINTER_STATE_COUNT) ? st.state : 0;

    const char *wifi_ip = httpd_get_ip();
    bool wf = (wifi_ip[0] != '0' && wifi_ip[0] != '\0');
    bool usb = dev->connected;
    const char *auth_token = httpd_get_auth_token();

    // 顶部边框
    printf(CLR_BOLD CLR_CYAN "\xe2\x94\x8c");
    for (int i = 0; i < CONSOLE_WIDTH; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x90\n");
    printf("\xe2\x94\x82  \xf0\x9f\x8e\xae Switch 3D Printer v2.0");
    pad_line_to_end(30);

    draw_horizontal_rule();

    // 连接状态
    printf("\xe2\x94\x82  %s\xe2\x97\x8f %s" CLR_RST "  %s\xe2\x97\x8f %s" CLR_RST,
           usb ? CLR_GRN : CLR_DIM,
           usb ? "USB Connected" : "USB Not Found",
           wf ? CLR_GRN : CLR_DIM,
           wf ? wifi_ip : "WiFi Not Connected");
    for (int i = 44; i < CONSOLE_WIDTH - (int)strlen(state_names[s]); i++) printf(" ");
    printf("%s%s" CLR_RST, state_colors[s], state_names[s]);
    pad_line_to_end(CONSOLE_WIDTH);

    draw_horizontal_rule();

    // 喷嘴温度
    printf("\xe2\x94\x82  " CLR_BOLD "Nozzle" CLR_RST " ");
    draw_progress_bar(30, st.temp.nozzle_actual, PRINTER_NOZZLE_MAX_TEMP,
                      temp_to_color(st.temp.nozzle_actual), CLR_DIM);
    printf(" " CLR_BOLD "%5.0f" CLR_RST, st.temp.nozzle_actual);
    if (st.temp.nozzle_target > 0)
        printf(CLR_DIM "/%-5.0f" CLR_RST, st.temp.nozzle_target);
    else printf("      ");
    printf("\xc2\xb0""C ");
    if (st.temp.nozzle_target > 0 && st.temp.nozzle_actual < st.temp.nozzle_target - 2)
        printf(CLR_YEL "\xe2\x96\xb2" CLR_RST);
    else if (st.temp.nozzle_target > 0)
        printf(CLR_GRN "\xe2\x97\x8f" CLR_RST);
    else printf(" ");
    pad_line_to_end(CONSOLE_WIDTH);

    // 热床温度
    printf("\xe2\x94\x82  " CLR_BOLD "Bed   " CLR_RST " ");
    draw_progress_bar(30, st.temp.bed_actual, PRINTER_BED_MAX_TEMP,
                      temp_to_color(st.temp.bed_actual * (PRINTER_NOZZLE_MAX_TEMP / (float)PRINTER_BED_MAX_TEMP)),
                      CLR_DIM);
    printf(" " CLR_BOLD "%5.0f" CLR_RST, st.temp.bed_actual);
    if (st.temp.bed_target > 0)
        printf(CLR_DIM "/%-5.0f" CLR_RST, st.temp.bed_target);
    else printf("      ");
    printf("\xc2\xb0""C ");
    if (st.temp.bed_target > 0 && st.temp.bed_actual < st.temp.bed_target - 1)
        printf(CLR_YEL "\xe2\x96\xb2" CLR_RST);
    else if (st.temp.bed_target > 0)
        printf(CLR_GRN "\xe2\x97\x8f" CLR_RST);
    else printf(" ");
    pad_line_to_end(CONSOLE_WIDTH);

    draw_horizontal_rule();

    // 打印进度
    if (s == 2 || s == 3) {
        printf("\xe2\x94\x82  " CLR_BOLD "Prog" CLR_RST "  ");
        draw_progress_bar(44, st.progress_percent, 100,
                          (s == 3) ? CLR_YEL : CLR_CYAN, CLR_DIM);
        printf(" " CLR_BOLD "%3d%%" CLR_RST, st.progress_percent);
        pad_line_to_end(CONSOLE_WIDTH);
        printf("\xe2\x94\x82  \xf0\x9f\x93\x84 %s",
               st.current_file[0] ? st.current_file : "(no file)");
        pad_line_to_end(4 + (int)strlen(st.current_file[0] ? st.current_file : "(no file)"));
        printf("\xe2\x94\x82  %d / %d lines", st.lines_sent, st.lines_total);
        pad_line_to_end(14);
    } else {
        printf("\xe2\x94\x82  " CLR_DIM "Waiting for print job..." CLR_RST);
        pad_line_to_end(25);
        printf("\xe2\x94\x82"); pad_line_to_end(0);
        printf("\xe2\x94\x82"); pad_line_to_end(0);
    }

    draw_horizontal_rule();

    // 方向键
    printf("\xe2\x94\x82                          " CLR_BOLD "\xe2\x96\xb2 Y+" CLR_RST "                                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                " CLR_BOLD "\xe2\x97\x84 X-" CLR_RST "   " CLR_BOLD "\xe2\x97\x86 HOME" CLR_RST "   " CLR_BOLD "X+ \xe2\x96\xba" CLR_RST "                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                          " CLR_BOLD "\xe2\x96\xbc Y-" CLR_RST "                                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                                                                              \xe2\x94\x82\n");
    printf("\xe2\x94\x82  " CLR_BOLD "Z+1" CLR_RST "  " CLR_BOLD "Z Home" CLR_RST "  " CLR_BOLD "Z-1" CLR_RST "        Step: " CLR_BOLD "10" CLR_RST " mm");
    pad_line_to_end(60);

    draw_horizontal_rule();

    // 按钮提示
    printf("\xe2\x94\x82  " CLR_BOLD "[+]" CLR_RST "Connect  " CLR_BOLD "[-]" CLR_RST "Disconnect  ");
    if (s == 2) printf(CLR_BOLD "[A]" CLR_RST "Pause  ");
    if (s == 3) printf(CLR_BOLD "[A]" CLR_RST "Resume  ");
    if (s == 2 || s == 3) printf(CLR_BOLD "[B]" CLR_RST "Cancel  ");
    printf(CLR_BOLD "[X]" CLR_RST "Exit");
    pad_line_to_end(66);

    // 底部边框
    printf("\xe2\x94\x94");
    for (int i = 0; i < CONSOLE_WIDTH; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x98\n" CLR_RST);

    // WiFi / Web UI / Token 提示
    if (wf && usb) {
        printf("\n  " CLR_BOLD "Web UI \xe2\x86\x92 " CLR_CYAN "http://%s:%d" CLR_RST "\n", wifi_ip, HTTP_PORT);
        if (auth_token && auth_token[0]) {
            printf("  " CLR_BOLD "Token  \xe2\x86\x92 " CLR_YEL "%s" CLR_RST "\n", auth_token);
        }
    } else if (!usb) {
        printf("\n  " CLR_DIM "Press [+] to connect printer" CLR_RST "\n");
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    Result rc;

    // ============================================================
    // Phase 1: 基础设施初始化
    // ============================================================
    consoleInit(NULL);
    socketInitializeDefault();

    // 日志系统 (先于崩溃处理器, 崩溃时需要 flush 日志)
    rc = logger_init();
    if (R_FAILED(rc)) {
        printf("\n  " CLR_YEL "SD log unavailable" CLR_RST "\n");
    }

    // 崩溃处理器
    rc = crash_init();
    if (R_FAILED(rc)) {
        printf("\n  " CLR_YEL "Crash handler unavailable" CLR_RST "\n");
    }

    // 电源管理 + Applet 生命周期
    rc = power_init();
    if (R_FAILED(rc)) {
        LOG_W("Power management init failed: 0x%x", rc);
    }

    LOG_I("Switch Printer v2.0 starting");

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    printf("\n" CLR_BOLD CLR_CYAN "  Switch 3D Printer v2.0" CLR_RST "\n");
    printf("  " CLR_DIM "Atmosphere CFW Deep Integration" CLR_RST "\n\n");
    consoleUpdate(NULL);

    gcode_init();
    httpd_init();

    rc = ch340_init();
    if (R_FAILED(rc)) {
        LOG_E("USB init failed: 0x%x", rc);
        printf("\n  " CLR_RED "USB init failed: 0x%x" CLR_RST "\n", rc);
        printf("  " CLR_DIM "Check OTG cable connection" CLR_RST "\n\n");
        printf("  Press any key to exit...\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        logger_exit();
        crash_exit();
        socketExit();
        return 0;
    }

    size_t dev_sz = (sizeof(Ch340Device) + 0xFFF) & ~0xFFF;
    Ch340Device *printer = aligned_alloc(0x1000, dev_sz);
    if (!printer) {
        LOG_E("Memory allocation failed");
        printf("\n  " CLR_RED "Memory allocation failed" CLR_RST "\n\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        logger_exit();
        crash_exit();
        socketExit();
        return 1;
    }
    memset(printer, 0, sizeof(Ch340Device));
    mutexInit(&printer->mutex);

    printf("\n  " CLR_YEL "Scanning for printer..." CLR_RST "\n");
    consoleUpdate(NULL);

    int retry = 0;
    while (retry < USB_SCAN_RETRY_MAX && !printer->connected && appletMainLoop()) {
        rc = ch340_connect(printer);
        if (R_SUCCEEDED(rc)) {
            printf("  " CLR_GRN "Printer connected!" CLR_RST "\n");
            LOG_I("Printer connected via USB");
            httpd_start(printer);
            break;
        }
        consoleUpdate(NULL);
        svcSleepThread(UI_SCAN_INTERVAL_NS);
        retry++;
    }

    // ============================================================
    // Phase 2: 主循环 (集成电源管理 + Applet 生命周期)
    // ============================================================
    while (appletMainLoop()) {
        // --- 电源管理 ---
        power_update(UI_REFRESH_NS);

        // --- Applet 焦点检测 (HOME键/切桌面) ---
        FocusEvent fe = power_check_focus();
        if (fe == FOCUS_LOST) {
            // 失去焦点 → 硬暂停打印
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING) {
                gcode_pause();
                LOG_P("Print paused: applet focus lost (HOME pressed)");
            }
        } else if (fe == FOCUS_RETURNED) {
            LOG_I("Applet focus returned");
        }

        // --- 手柄输入 ---
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        // 任何按键 = 用户交互 → 恢复亮度
        if (down) power_on_interaction();

        if ((down & HidNpadButton_Plus) && !printer->connected) {
            consoleClear();
            printf("\n  " CLR_YEL "Re-scanning..." CLR_RST "\n");
            consoleUpdate(NULL);
            rc = ch340_connect(printer);
            if (R_SUCCEEDED(rc)) {
                printf("  " CLR_GRN "Connected!" CLR_RST "\n");
                LOG_I("Printer connected (manual re-scan)");
                httpd_start(printer);
            } else {
                printf("  " CLR_DIM "Device not found" CLR_RST "\n");
                LOG_W("Printer re-scan failed");
            }
        }

        if ((down & HidNpadButton_Minus) && printer->connected) {
            LOG_I("Manual disconnect");
            gcode_cancel(printer);
            httpd_stop();
            ch340_disconnect(printer);
            power_print_end();
        }

        if (down & HidNpadButton_A) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING) {
                gcode_pause();
                LOG_P("Print paused (A button)");
            }
            else if (st.state == PRINTER_PAUSED) {
                gcode_resume();
                LOG_P("Print resumed (A button)");
            }
        }

        if (down & HidNpadButton_B) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING || st.state == PRINTER_PAUSED) {
                LOG_P("Print cancelled (B button)");
                gcode_cancel(printer);
                power_print_end();
            }
        }

        if (down & HidNpadButton_X) break;

        gcode_update(printer);
        draw_ui(printer);
        consoleUpdate(NULL);
        svcSleepThread(UI_REFRESH_NS);
    }

    // ============================================================
    // Phase 3: 干净退出
    // ============================================================
    LOG_I("Shutting down...");
    gcode_cancel(printer);
    if (printer->connected) {
        httpd_stop();
        ch340_disconnect(printer);
    }
    free(printer);

    power_exit();
    crash_exit();
    logger_exit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
