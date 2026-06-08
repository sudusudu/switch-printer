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
        "[ OFFLINE ]", "[ IDLE ]", "[ PRINTING ]", "[ PAUSED ]", "[ ERROR!! ]"
    };
    const char *state_color[] = {
        "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[36m", "\x1b[31m"
    };

    int s = st->state;
    if (s < 0) s = 0; if (s > 4) s = 4;

    printf("\n");
    printf("   ====== Switch 3D Printer ======\n\n");
    printf("   State: %s%s\x1b[0m\n", state_color[s], state_str[s]);
    printf("\n");
    printf("   Nozzle: %6.1f / %6.1f C\n",
           st->temp.nozzle_actual, st->temp.nozzle_target);
    printf("   Bed:    %6.1f / %6.1f C\n",
           st->temp.bed_actual, st->temp.bed_target);
    printf("\n");

    if (s == 2 || s == 3) {
        printf("   File: %s\n", st->current_file);
        printf("   Progress: %d%%  (%d / %d lines)\n",
               st->progress_percent, st->lines_sent, st->lines_total);
        int bar_w = 40;
        int filled = (st->progress_percent * bar_w) / 100;
        printf("   [");
        for (int i = 0; i < bar_w; i++) {
            printf("%c", i < filled ? '=' : ' ');
        }
        printf("]\n");
    }

    printf("\n   ===============================\n\n");
    printf("   WiFi: %s\n", httpd_get_ip());
    printf("   USB : %s\n", dev->connected ? "CH340 Connected" : "No Printer");
    printf("\n");
    printf("   Browser: http://%s:%d\n", httpd_get_ip(), HTTP_PORT);
    printf("\n");
    printf("   [+] Connect  [-] Disconnect  [B] Exit\n");
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char **argv) {
    Result rc;

    consoleInit(NULL);
    socketInitializeDefault();

    printf("\n   Switch 3D Printer NRO v1.0\n");
    printf("   Initializing...\n\n");

    printf("   WiFi: %s\n", WIFI_SSID);

    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("   USB init failed: 0x%x\n", rc);
        printf("   Press + to exit\n");
        while (appletMainLoop()) {
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 0;
    }
    printf("   USB Host ready\n");

    gcode_init();
    printf("   G-code engine ready\n");

    httpd_init();

    Ch340Device printer;
    memset(&printer, 0, sizeof(printer));
    PrintConsole *console = consoleGetDefault();

    bool running = true;

    while (running && appletMainLoop()) {
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS) {
            if (!printer.connected) {
                printf("\n   Scanning for CH340...\n");
                consoleUpdate(NULL);
                rc = ch340_connect(&printer);
                if (R_SUCCEEDED(rc)) {
                    printf("   CH340 connected!\n");
                    httpd_start(&printer);
                } else {
                    printf("   Printer not found (0x%x)\n", rc);
                    printf("   Check: USB-OTG cable, printer power\n");
                }
            }
        }

        if (kDown & KEY_MINUS) {
            if (printer.connected) {
                httpd_stop();
                ch340_disconnect(&printer);
                printf("\n   Printer disconnected\n");
            }
        }

        if (kDown & KEY_B) {
            running = false;
        }

        gcode_update(&printer);
        draw_ui(console, &printer);
        consoleUpdate(NULL);

        svcSleepThread(50000000ULL); // 50ms
    }

    printf("\n   Exiting...\n");
    if (printer.connected) {
        httpd_stop();
        ch340_disconnect(&printer);
    }
    socketExit();
    consoleExit(NULL);
    return 0;
}