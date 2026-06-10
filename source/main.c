#include <switch.h>
#include <switch/runtime/pad.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usb_ch340.h"
#include "gcode.h"
#include "httpd.h"
#include "config.h"

// ANSI colors
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
static const char *blk[9] = {
    " ",
    "\xe2\x96\x8f",  // U+258F
    "\xe2\x96\x8e",  // U+258E
    "\xe2\x96\x8d",  // U+258D
    "\xe2\x96\x8c",  // U+258C
    "\xe2\x96\x8b",  // U+258B
    "\xe2\x96\x8a",  // U+258A
    "\xe2\x96\x89",  // U+2589
    "\xe2\x96\x88"   // U+2588
};

static void blkbar(int w, float v, float max, const char *hi, const char *lo) {
    float r = (max > 0) ? v / max : 0;
    if (r < 0) r = 0;
    if (r > 1) r = 1;
    int full = (int)(r * w);
    int frac = ((int)(r * w * 8)) % 8;
    printf("%s", hi);
    for (int i = 0; i < full; i++) printf("\xe2\x96\x88");
    if (full < w) {
        printf("%s", blk[frac]);
        for (int i = full + 1; i < w; i++) printf("%s\xc2\xb7", lo);
    }
    printf(CLR_RST);
}

static const char *tclr(float t) {
    if (t >= 190) return CLR_RED;
    if (t >= 120) return CLR_YEL;
    if (t >= 40)  return CLR_GRN;
    return CLR_CYAN;
}

static void end78(int used) {
    for (int i = used; i < 78; i++) printf(" ");
    printf("\xe2\x94\x82\n");
}

static void hr(void) {
    printf("\xe2\x94\x9c");
    for (int i = 0; i < 78; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xa4\n");
}

static void draw_ui(Ch340Device *dev) {
    consoleClear();
    PrinterStatus st;
    gcode_get_status_safe(&st);

    const char *sn[] = {
        "\xe7\xa6\xbb\xe7\xba\xbf",
        "\xe7\xa9\xba\xe9\x97\xb2",
        "\xe6\x89\x93\xe5\x8d\xb0\xe4\xb8\xad",
        "\xe6\x9a\x82\xe5\x81\x9c",
        "\xe9\x94\x99\xe8\xaf\xaf"
    };
    const char *sc[] = {CLR_DIM,CLR_GRN,CLR_YEL CLR_BOLD,CLR_CYAN,CLR_RED CLR_BOLD};
    int s = (st.state >= 0 && st.state <= 4) ? st.state : 0;

    const char *wifi_ip = httpd_get_ip();
    bool wf = (wifi_ip[0] != '0' && wifi_ip[0] != '\0');
    bool usb = dev->connected;

    printf(CLR_BOLD CLR_CYAN);
    printf("\xe2\x94\x8c");
    for (int i = 0; i < 78; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x90\n");
    printf("\xe2\x94\x82  \xf0\x9f\x8e\xae Switch 3D Printer v1.0");
    end78(30);

    hr();
    printf("\xe2\x94\x82  %s\xe2\x97\x8f %s" CLR_RST "  %s\xe2\x97\x8f %s" CLR_RST,
           usb ? CLR_GRN : CLR_DIM,
           usb ? "USB Connected" : "USB Not Found",
           wf ? CLR_GRN : CLR_DIM,
           wf ? wifi_ip : "WiFi Not Connected");
    for (int i = 44; i < 78 - (int)strlen(sn[s]); i++) printf(" ");
    printf("%s%s" CLR_RST, sc[s], sn[s]);
    end78(78);

    hr();

    printf("\xe2\x94\x82  " CLR_BOLD "Nozzle" CLR_RST " ");
    blkbar(30, st.temp.nozzle_actual, 300,
           tclr(st.temp.nozzle_actual), CLR_DIM);
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
    end78(78);

    printf("\xe2\x94\x82  " CLR_BOLD "Bed   " CLR_RST " ");
    blkbar(30, st.temp.bed_actual, 120,
           tclr(st.temp.bed_actual * 2.5f), CLR_DIM);
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
    end78(78);

    hr();
    if (s == 2 || s == 3) {
        printf("\xe2\x94\x82  " CLR_BOLD "Prog" CLR_RST "  ");
        blkbar(44, st.progress_percent, 100,
               (s == 3) ? CLR_YEL : CLR_CYAN, CLR_DIM);
        printf(" " CLR_BOLD "%3d%%" CLR_RST, st.progress_percent);
        end78(78);

        printf("\xe2\x94\x82  \xf0\x9f\x93\x84 %s",
               st.current_file[0] ? st.current_file : "(no file)");
        end78(4 + (int)strlen(st.current_file[0] ? st.current_file : "(no file)"));

        printf("\xe2\x94\x82  %d / %d lines", st.lines_sent, st.lines_total);
        end78(14);
    } else {
        printf("\xe2\x94\x82  " CLR_DIM "Waiting for print job..." CLR_RST);
        end78(25);
        printf("\xe2\x94\x82"); end78(0);
        printf("\xe2\x94\x82"); end78(0);
    }

    hr();
    printf("\xe2\x94\x82                          " CLR_BOLD "\xe2\x96\xb2 Y+" CLR_RST "                                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                " CLR_BOLD "\xe2\x97\x84 X-" CLR_RST "   " CLR_BOLD "\xe2\x97\x86 HOME" CLR_RST "   " CLR_BOLD "X+ \xe2\x96\xba" CLR_RST "                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                          " CLR_BOLD "\xe2\x96\xbc Y-" CLR_RST "                                \xe2\x94\x82\n");
    printf("\xe2\x94\x82                                                                              \xe2\x94\x82\n");
    printf("\xe2\x94\x82  " CLR_BOLD "Z+1" CLR_RST "  " CLR_BOLD "Z Home" CLR_RST "  " CLR_BOLD "Z-1" CLR_RST "        Step: " CLR_BOLD "10" CLR_RST " mm");
    end78(60);

    hr();
    printf("\xe2\x94\x82  " CLR_BOLD "[+]" CLR_RST "Connect  " CLR_BOLD "[-]" CLR_RST "Disconnect  ");
    if (s == 2) printf(CLR_BOLD "[A]" CLR_RST "Pause  ");
    if (s == 3) printf(CLR_BOLD "[A]" CLR_RST "Resume  ");
    if (s == 2 || s == 3) printf(CLR_BOLD "[B]" CLR_RST "Cancel  ");
    printf(CLR_BOLD "[X]" CLR_RST "Exit");
    end78(66);

    printf("\xe2\x94\x94");
    for (int i = 0; i < 78; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x98\n" CLR_RST);

    if (wf && usb) {
        printf("\n  " CLR_BOLD "Web UI \xe2\x86\x92 " CLR_CYAN "http://%s:%d" CLR_RST "\n", wifi_ip, HTTP_PORT);
    } else if (!usb) {
        printf("\n  " CLR_DIM "Press [+] to connect printer" CLR_RST "\n");
    }
}

int main(int argc, char **argv) {
    Result rc;

    consoleInit(NULL);
    socketInitializeDefault();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    printf("\n" CLR_BOLD CLR_CYAN "  Switch 3D Printer" CLR_RST "\n");
    printf("  " CLR_DIM "Initializing..." CLR_RST "\n\n");
    consoleUpdate(NULL);

    gcode_init();
    httpd_init();

    rc = ch340_init();
    if (R_FAILED(rc)) {
        printf("\n  " CLR_RED "USB init failed: 0x%x" CLR_RST "\n", rc);
        printf("  " CLR_DIM "Check OTG cable connection" CLR_RST "\n\n");
        printf("  Press any key to exit...\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        return 0;
    }

    size_t dev_sz = (sizeof(Ch340Device) + 0xFFF) & ~0xFFF;
    Ch340Device *printer = aligned_alloc(0x1000, dev_sz);
    if (!printer) {
        printf("\n  " CLR_RED "Memory allocation failed" CLR_RST "\n\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) svcSleepThread(100000000ULL);
        consoleExit(NULL);
        return 1;
    }
    memset(printer, 0, sizeof(Ch340Device));

    printf("\n  " CLR_YEL "Scanning for printer..." CLR_RST "\n");
    consoleUpdate(NULL);

    int retry = 0;
    while (retry < 30 && !printer->connected && appletMainLoop()) {
        rc = ch340_connect(printer);
        if (R_SUCCEEDED(rc)) {
            printf("  " CLR_GRN "Printer connected!" CLR_RST "\n");
            httpd_start(printer);
            break;
        }
        consoleUpdate(NULL);
        svcSleepThread(1000000000ULL);
        retry++;
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if ((down & HidNpadButton_Plus) && !printer->connected) {
            consoleClear();
            printf("\n  " CLR_YEL "Re-scanning..." CLR_RST "\n");
            consoleUpdate(NULL);
            rc = ch340_connect(printer);
            if (R_SUCCEEDED(rc)) {
                printf("  " CLR_GRN "Connected!" CLR_RST "\n");
                httpd_start(printer);
            } else {
                printf("  " CLR_DIM "Device not found" CLR_RST "\n");
            }
        }

        if ((down & HidNpadButton_Minus) && printer->connected) {
            httpd_stop();
            ch340_disconnect(printer);
        }

        if (down & HidNpadButton_A) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING) gcode_pause();
            else if (st.state == PRINTER_PAUSED) gcode_resume();
        }

        if (down & HidNpadButton_B) {
            PrinterStatus st;
            gcode_get_status_safe(&st);
            if (st.state == PRINTER_PRINTING || st.state == PRINTER_PAUSED)
                gcode_cancel(printer);
        }

        if (down & HidNpadButton_X) break;

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
