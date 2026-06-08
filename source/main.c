#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"

// 简化触屏 UI
static void draw_ui(Ch340Device *dev) {
    consoleClear();
    PrinterStatus st;
    gcode_get_status_safe(&st);
    int s = (st.state >= 0 && st.state <= 4) ? st.state : 0;
    const char *names[] = {"OFFLINE","IDLE","PRINTING","PAUSED","ERROR"};

    printf("\n  ==== Switch 3D Printer ====\n\n");
    printf("  State  : %s\n", names[s]);
    printf("  Nozzle : %.0f / %.0f C\n", st.temp.nozzle_actual, st.temp.nozzle_target);
    printf("  Bed    : %.0f / %.0f C\n", st.temp.bed_actual, st.temp.bed_target);

    if (s == 2 || s == 3) {
        printf("\n  File   : %s\n", st.current_file);
        printf("  Done   : %d%% (%d/%d)\n",
               st.progress_percent, st.lines_sent, st.lines_total);
    }

    printf("\n  WiFi : %s\n", httpd_get_ip());
    printf("  USB  : %s\n", dev->connected ? "CONNECTED" : "scanning...");
    printf("\n  -> http://%s:%d\n", httpd_get_ip(), HTTP_PORT);
    if (!dev->connected) printf("  (connect USB-OTG + printer)\n");
}

// ============================================================
int main(int argc, char **argv) {
    Result rc;

    consoleInit(NULL);
    socketInitializeDefault();

    printf("\n  Switch 3D Printer v1.0\n\n");

    gcode_init();
    httpd_init();

    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("  USB init FAILED: 0x%x\n", rc);
        consoleUpdate(NULL);
        svcSleepThread(5000000000ULL);
        consoleExit(NULL);
        return 0;
    }

    // aligned_alloc 要求 size 是 alignment 的整数倍 (C11 UB)
    size_t dev_sz = (sizeof(Ch340Device) + 0xFFF) & ~0xFFF;
    Ch340Device *printer = aligned_alloc(0x1000, dev_sz);
    if (!printer) {
        printf("  Memory allocation failed\n");
        consoleUpdate(NULL);
        svcSleepThread(3000000000ULL);
        consoleExit(NULL);
        return 1;
    }
    memset(printer, 0, sizeof(Ch340Device));

    // 自动扫描连接打印机
    printf("  Scanning for printer...\n");
    consoleUpdate(NULL);

    int retry = 0;
    while (retry < 30 && !printer->connected && appletMainLoop()) {
        rc = ch340_connect(printer);
        if (R_SUCCEEDED(rc)) {
            printf("  Printer connected!\n");
            httpd_start(printer);
            break;
        }
        consoleUpdate(NULL);
        svcSleepThread(1000000000ULL); // 1s
        retry++;
    }

    // 主循环
    while (appletMainLoop()) {
        gcode_update(printer);
        draw_ui(printer);
        consoleUpdate(NULL);
        svcSleepThread(100000000ULL); // 100ms
    }

    gcode_cancel(printer);
    if (printer->connected) { httpd_stop(); ch340_disconnect(printer); }
    free(printer);
    socketExit();
    consoleExit(NULL);
    return 0;
}