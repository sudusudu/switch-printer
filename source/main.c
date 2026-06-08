#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"

// ANSI 颜色
#define CLR_RST    "\x1b[0m"
#define CLR_RED    "\x1b[31m"
#define CLR_GRN    "\x1b[32m"
#define CLR_YEL    "\x1b[33m"
#define CLR_BLU    "\x1b[34m"
#define CLR_CYAN   "\x1b[36m"
#define CLR_WHITE  "\x1b[37m"
#define CLR_BOLD   "\x1b[1m"
#define CLR_DIM    "\x1b[2m"
#define CLR_RED_BG "\x1b[41m"
#define CLR_GRN_BG "\x1b[42m"
#define CLR_YEL_BG "\x1b[43m"
#define CLR_BLU_BG "\x1b[44m"

static void draw_bar(const char *label, float actual, float target, float max) {
    // 温度条：标签 |████░░░░| 实际/目标 °C
    printf("  %-6s ", label);
    int w = 20;
    float ratio = actual / max;
    if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
    int filled = (int)(ratio * w);

    // 颜色：冷蓝(<40) 暖绿(40-60) 热黄(60-80) 烫红(>80)
    const char *clr = CLR_CYAN;
    if (actual >= 80) clr = CLR_RED;
    else if (actual >= 60) clr = CLR_YEL;
    else if (actual >= 40) clr = CLR_GRN;

    printf("%s", clr);
    for (int i = 0; i < w; i++) printf("%c", i < filled ? '#' : '.');
    printf("%s ", CLR_RST);
    printf("%5.0f", actual);
    if (target > 0) printf("/%-5.0f", target);
    else            printf("     ");
    printf(" C\n");
}

static void draw_ui(Ch340Device *dev) {
    consoleClear();
    PrinterStatus st;
    gcode_get_status_safe(&st);

    const char *state_str[] = {"离线", "空闲", "打印中", "暂停", "错误"};
    const char *state_clr[] = {CLR_DIM, CLR_GRN, CLR_YEL CLR_BOLD, CLR_CYAN, CLR_RED CLR_BOLD};
    const char *state_sym[] = {"[x]", "[ ]", "[>]", "[||]", "[!]"};
    int s = (st.state >= 0 && st.state <= 4) ? st.state : 0;

    // 顶栏
    printf(CLR_BOLD CLR_CYAN);
    printf("  ┌──────────────────────────────┐\n");
    printf("  │  🎮 Switch 3D Printer v1.0   │\n");
    printf("  └──────────────────────────────┘\n" CLR_RST);
    printf("\n");

    // 状态行
    printf("  %s%s 状态: %s%s" CLR_RST "\n",
           state_clr[s], state_sym[s], state_str[s],
           (s == 2) ? " ..." : "      ");

    // 温度
    printf("\n");
    printf(CLR_BOLD "  温度" CLR_RST "\n");
    draw_bar("喷头", st.temp.nozzle_actual, st.temp.nozzle_target, 300);
    draw_bar("热床", st.temp.bed_actual, st.temp.bed_target, 120);

    // 分割线
    printf("\n  %s", CLR_DIM);
    for (int i = 0; i < 30; i++) printf("-");
    printf(CLR_RST "\n");

    // 进度
    if (s == 2 || s == 3) {
        printf("\n" CLR_BOLD "  打印进度" CLR_RST "\n");
        printf("  %s\n", st.current_file[0] ? st.current_file : "(无文件)");

        int bar_w = 26;
        int filled = (st.progress_percent * bar_w) / 100;
        const char *pclr = (s == 3) ? CLR_YEL : CLR_GRN;
        printf("  ");
        for (int i = 0; i < bar_w; i++) {
            if (i < filled) printf("%s#", pclr);
            else printf("%s.", CLR_DIM);
        }
        printf(CLR_RST "  %s%d%%" CLR_RST "\n", CLR_BOLD, st.progress_percent);
        printf("  %d / %d 行\n", st.lines_sent, st.lines_total);
    }

    // 分割线
    printf("\n  %s", CLR_DIM);
    for (int i = 0; i < 30; i++) printf("-");
    printf(CLR_RST "\n\n");

    // 连接信息
    const char *usb_clr = dev->connected ? CLR_GRN : CLR_DIM;
    const char *usb_txt = dev->connected ? "已连接 CH340" : "未连接";
    const char *wifi_ip = httpd_get_ip();
    bool has_wifi = (wifi_ip[0] != '0' && wifi_ip[0] != '\0');

    printf("  %sUSB: %s" CLR_RST "\n", usb_clr, usb_txt);
    printf("  %sWiFi: %s" CLR_RST "\n",
           has_wifi ? CLR_GRN : CLR_DIM,
           has_wifi ? wifi_ip : "未连接");
    if (has_wifi && dev->connected) {
        printf("\n  " CLR_BOLD "→ " CLR_CYAN "http://%s:%d" CLR_RST "\n", wifi_ip, HTTP_PORT);
    }

    // 底部按键提示
    printf("\n\n  %s", CLR_DIM);
    printf("[+]连接  [-]断开  ");
    if (s == 2) printf("[A]暂停  ");
    if (s == 3) printf("[A]恢复  ");
    if (s == 2 || s == 3) printf("[B]取消  ");
    printf("%s" CLR_RST, dev->connected ? "" : "");
    printf(CLR_BOLD "[X]退出" CLR_RST "\n");
}

// ============================================================
int main(int argc, char **argv) {
    Result rc;

    consoleInit(NULL);
    socketInitializeDefault();

    printf("\n" CLR_BOLD CLR_CYAN "  Switch 3D Printer" CLR_RST "\n");
    printf("  " CLR_DIM "正在初始化..." CLR_RST "\n\n");
    consoleUpdate(NULL);

    gcode_init();
    httpd_init();

    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("\n  " CLR_RED "USB 初始化失败: 0x%x" CLR_RST "\n", rc);
        printf("  " CLR_DIM "请确认 OTG 线已连接" CLR_RST "\n\n");
        printf("  按任意键退出...\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        return 0;
    }

    size_t dev_sz = (sizeof(Ch340Device) + 0xFFF) & ~0xFFF;
    Ch340Device *printer = aligned_alloc(0x1000, dev_sz);
    if (!printer) {
        printf("\n  " CLR_RED "内存分配失败" CLR_RST "\n\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        return 1;
    }
    memset(printer, 0, sizeof(Ch340Device));

    printf("\n  " CLR_YEL "正在扫描打印机..." CLR_RST "\n");
    consoleUpdate(NULL);

    int retry = 0;
    while (retry < 30 && !printer->connected && appletMainLoop()) {
        rc = ch340_connect(printer);
        if (R_SUCCEEDED(rc)) {
            printf("  " CLR_GRN "打印机已连接!" CLR_RST "\n");
            httpd_start(printer);
            break;
        }
        consoleUpdate(NULL);
        svcSleepThread(1000000000ULL);
        retry++;
    }

    while (appletMainLoop()) {
        hidScanInput();
        u64 down = hidKeysDown(CONTROLLER_P1_AUTO);

        // + 键：连接
        if ((down & KEY_PLUS) && !printer->connected) {
            consoleClear();
            printf("\n  " CLR_YEL "重新扫描..." CLR_RST "\n");
            consoleUpdate(NULL);
            rc = ch340_connect(printer);
            if (R_SUCCEEDED(rc)) {
                printf("  " CLR_GRN "已连接!" CLR_RST "\n");
                httpd_start(printer);
            } else {
                printf("  " CLR_DIM "未找到设备" CLR_RST "\n");
            }
        }

        // - 键：断开
        if ((down & KEY_MINUS) && printer->connected) {
            httpd_stop();
            ch340_disconnect(printer);
        }

        // A 键：暂停/恢复
        if (down & KEY_A) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING) gcode_pause();
            else if (st.state == PRINTER_PAUSED) gcode_resume();
        }

        // B 键：取消打印
        if (down & KEY_B) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING || st.state == PRINTER_PAUSED)
                gcode_cancel(printer);
        }

        // X 键：退出
        if (down & KEY_X) break;

        gcode_update(printer);
        draw_ui(printer);
        consoleUpdate(NULL);
        svcSleepThread(100000000ULL);
    }

    gcode_cancel(printer);
    if (printer->connected) { httpd_stop(); ch340_disconnect(printer); }
    free(printer);
    socketExit();
    consoleExit(NULL);
    return 0;
}
