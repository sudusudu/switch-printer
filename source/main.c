#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"

// ============================================================
// 触屏 UI 绘制
// ============================================================
static void draw_ui(PrintConsole *console, Ch340Device *dev) {
    consoleClear();
    PrinterStatus *st = gcode_get_status();

    const char *state_str[] = {
        "[ 离线 ]", "[ 空闲 ]", "[ 打印中 ]", "[ 已暂停 ]", "[ 错误!! ]"
    };
    const char *state_color[] = {
        "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[36m", "\x1b[31m"
    };

    int s = st->state;
    if (s < 0) s = 0; if (s > 4) s = 4;

    printf("\n");
    printf("   ====== Switch 3D Printer ======\n\n");
    printf("   状态: %s%s\x1b[0m\n", state_color[s], state_str[s]);
    printf("\n");
    printf("   喷头: %6.1f / %6.1f °C\n",
           st->temp.nozzle_actual, st->temp.nozzle_target);
    printf("   热床: %6.1f / %6.1f °C\n",
           st->temp.bed_actual, st->temp.bed_target);
    printf("\n");

    if (s == 2 || s == 3) { // 打印中或暂停
        printf("   文件: %s\n", st->current_file);
        printf("   进度: %d%%  (%d / %d 行)\n",
               st->progress_percent, st->lines_sent, st->lines_total);
        // 进度条
        int bar_w = 40;
        int filled = (st->progress_percent * bar_w) / 100;
        printf("   [");
        for (int i = 0; i < bar_w; i++) {
            printf("%c", i < filled ? '=' : ' ');
        }
        printf("]\n");
    }

    printf("\n   ===============================\n");
    printf("\n");
    printf("   WiFi: %s\n", httpd_get_ip());
    printf("   USB : %s\n", dev->connected ? "已连接 CH340" : "未连接打印机");
    printf("\n");
    printf("   浏览器打开 http://%s:%d\n", httpd_get_ip(), HTTP_PORT);
    printf("   手机/电脑均可控制\n");
    printf("\n");
    printf("   [+] 连接  [-] 断开  [B] 退出\n");
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char **argv) {
    Result rc;

    // --- 初始化所有子系统 ---
    consoleInit(NULL);
    socketInitializeDefault();

    printf("\n   Switch 3D Printer NRO v1.0\n");
    printf("   正在初始化...\n\n");

    // Wi-Fi 配置（如未连接则尝试连接）
    printf("   配置 WiFi: %s\n", WIFI_SSID);
    // Note: WiFi 需要在系统设置中预先连接
    // 或者使用 nifm 自动连接（此处简化，假设已配好）

    // 初始化 USB
    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("   USB 初始化失败: 0x%x\n", rc);
        printf("   按 + 退出\n");
        while (appletMainLoop()) {
            hidScanInput();
            if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 0;
    }
    printf("   USB Host 就绪\n");

    // 初始化 G-code 引擎
    gcode_init();
    printf("   G-code 引擎就绪\n");

    // 初始化 HTTP 服务器
    httpd_init();

    // --- 主状态机 ---
    Ch340Device printer;
    memset(&printer, 0, sizeof(printer));
    PrintConsole *console = consoleGetDefault();

    bool running = true;
    bool scan_usb = true;

    while (running && appletMainLoop()) {
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        // 按键处理
        if (kDown & KEY_PLUS) {
            // + 键：连接打印机
            if (!printer.connected) {
                printf("\n   正在扫描 CH340 设备...\n");
                consoleUpdate(NULL);
                rc = ch340_connect(&printer);
                if (R_SUCCEEDED(rc)) {
                    printf("   CH340 已连接！\n");
                    // 启动 HTTP 服务器
                    httpd_start(&printer);
                } else {
                    printf("   未找到打印机 (0x%x)\n", rc);
                    printf("   请确认: USB-OTG 已连接、打印机已开机\n");
                }
            }
        }

        if (kDown & KEY_MINUS) {
            // - 键：断开
            if (printer.connected) {
                httpd_stop();
                ch340_disconnect(&printer);
                printf("\n   已断开打印机\n");
            }
        }

        if (kDown & KEY_B) {
            // B 键：退出
            running = false;
        }

        // 周期更新
        gcode_update(&printer);

        // 刷新显示
        draw_ui(console, &printer);
        consoleUpdate(NULL);

        svcSleepThread(50000000ULL); // 50ms
    }

    // --- 清理 ---
    printf("\n   正在退出...\n");
    if (printer.connected) {
        httpd_stop();
        ch340_disconnect(&printer);
    }
    socketExit();
    consoleExit(NULL);
    return 0;
}
