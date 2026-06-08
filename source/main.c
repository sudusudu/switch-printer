#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"

static void draw_ui(Ch340Device *dev) {
    consoleClear();
    PrinterStatus st_copy;
    gcode_get_status_safe(&st_copy);
    int s = (st_copy.state >= 0 && st_copy.state <= 4) ? st_copy.state : 0;

    printf("\n  ===== Switch 3D Printer =====\n\n");
    printf("  State: ");
    switch (s) {
        case 0: printf("OFFLINE\n"); break;
        case 1: printf("IDLE\n"); break;
        case 2: printf("PRINTING\n"); break;
        case 3: printf("PAUSED\n"); break;
        case 4: printf("ERROR\n"); break;
    }
    printf("\n");
    printf("  Nozzle: %.0f / %.0f C\n", st_copy.temp.nozzle_actual, st_copy.temp.nozzle_target);
    printf("  Bed:    %.0f / %.0f C\n", st_copy.temp.bed_actual, st_copy.temp.bed_target);

    if (s == 2 || s == 3) {
        printf("\n  File: %s\n", st_copy.current_file);
        printf("  Progress: %d%%  (%d/%d)\n",
               st_copy.progress_percent, st_copy.lines_sent, st_copy.lines_total);
    }

    printf("\n  WiFi : %s\n", httpd_get_ip());
    printf("  USB  : %s\n", dev->connected ? "CONNECTED" : "no printer");
    printf("\n  Browser -> http://%s:%d\n", httpd_get_ip(), HTTP_PORT);
    printf("\n  Press + to connect  B to exit\n");
}

int main(int argc, char **argv) {
    Result rc;
    consoleInit(NULL);
    socketInitializeDefault();

    printf("\n  Switch 3D Printer v1.0\n");
    printf("  WiFi: %s\n\n", WIFI_SSID);

    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("  USB init FAILED: 0x%x\n", rc);
        printf("  Press any key to exit...\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        return 0;
    }

    gcode_init();
    httpd_init();

    Ch340Device printer;
    memset(&printer, 0, sizeof(printer));

    bool running = true;

    while (running && appletMainLoop()) {
        hidScanInput();
        u64 down = hidKeysDown(CONTROLLER_P1_AUTO);

        if (down & KEY_PLUS) {
            if (!printer.connected) {
                printf("\n  Scanning USB for CH340...\n");
                consoleUpdate(NULL);
                rc = ch340_connect(&printer);
                if (R_SUCCEEDED(rc)) {
                    printf("  Printer connected!\n");
                    httpd_start(&printer);
                } else {
                    printf("  Not found (0x%x)\n", rc);
                }
            }
        }

        if (down & KEY_MINUS) {
            if (printer.connected) {
                httpd_stop();
                ch340_disconnect(&printer);
                printf("\n  Disconnected.\n");
            }
        }

        if (down & KEY_B) running = false;

        gcode_update(&printer);
        draw_ui(&printer);
        consoleUpdate(NULL);
        svcSleepThread(50000000ULL);
    }

    if (printer.connected) { httpd_stop(); ch340_disconnect(&printer); }
    socketExit();
    consoleExit(NULL);
    return 0;
}