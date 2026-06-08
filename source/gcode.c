#include "gcode.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static PrinterStatus g_status;
static volatile bool g_paused = false;
static volatile bool g_cancel = false;
static bool g_thread_running = false;
static Mutex g_status_mutex;

// 打印线程
static Thread g_print_thread;
static Ch340Device *g_dev = NULL;
static char g_file_path[256];

// ============================================================
Result gcode_init(void) {
    mutexInit(&g_status_mutex);
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = PRINTER_OFFLINE;
    return 0;
}

// ============================================================
// 等待打印机返回 "ok"
// ============================================================
static Result wait_ok(Ch340Device *dev, u64 timeout_ms) {
    char buf[128];
    size_t received;
    u64 start = armGetSystemTick();

    while (armGetSystemTick() - start < timeout_ms) {
        Result rc = ch340_recv(dev, buf, sizeof(buf) - 1, 100, &received);
        if (R_FAILED(rc)) {
            if (rc == MAKERESULT(0xEA01, 0)) continue;
            return rc;
        }
        if (received > 0) {
            buf[received] = '\0';
            if (strncmp(buf, "ok", 2) == 0) return 0;
            if (strncmp(buf, "T:", 2) == 0 || strncmp(buf, "ok T:", 5) == 0) {
                float nozzle_actual = 0, nozzle_target = 0;
                float bed_actual = 0, bed_target = 0;
                sscanf(buf, "%*s T:%f /%f B:%f /%f",
                       &nozzle_actual, &nozzle_target, &bed_actual, &bed_target);
                mutexLock(&g_status_mutex);
                g_status.temp.nozzle_actual = nozzle_actual;
                g_status.temp.nozzle_target = nozzle_target;
                g_status.temp.bed_actual = bed_actual;
                g_status.temp.bed_target = bed_target;
                mutexUnlock(&g_status_mutex);
                if (strstr(buf, "ok") != NULL) return 0;
            }
        }
    }
    return MAKERESULT(Module_Libnx, 5);
}

// ============================================================
// 打印线程主函数
// ============================================================
static void print_thread_func(void *arg) {
    FILE *fp = fopen(g_file_path, "r");
    if (!fp) {
        mutexLock(&g_status_mutex);
        g_status.state = PRINTER_ERROR;
        mutexUnlock(&g_status_mutex);
        return;
    }

    int total_lines = 0;
    char line_buf[GCODE_LINE_MAX];
    while (fgets(line_buf, sizeof(line_buf), fp)) total_lines++;
    rewind(fp);

    mutexLock(&g_status_mutex);
    g_status.state = PRINTER_PRINTING;
    g_status.lines_total = total_lines;
    g_status.lines_sent = 0;
    g_status.progress_percent = 0;
    mutexUnlock(&g_status_mutex);

    int line_num = 0;
    bool error = false;

    while (fgets(line_buf, sizeof(line_buf), fp) && !g_cancel) {
        while (g_paused && !g_cancel) {
            svcSleepThread(100000000ULL);
        }
        if (g_cancel) break;

        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len-1] == '\n' || line_buf[len-1] == '\r')) {
            line_buf[--len] = '\0';
        }

        if (len == 0) continue;
        if (line_buf[0] == ';') continue;

        if (len >= GCODE_LINE_MAX - 2) len = GCODE_LINE_MAX - 3;
        line_buf[len] = '\n';
        line_buf[len+1] = '\0';

        Result rc = ch340_send(g_dev, line_buf, len + 1);
        if (R_FAILED(rc)) { error = true; break; }

        rc = wait_ok(g_dev, GCODE_OK_TIMEOUT);
        if (R_FAILED(rc)) { error = true; break; }

        line_num++;
        mutexLock(&g_status_mutex);
        g_status.lines_sent = line_num;
        if (total_lines > 0)
            g_status.progress_percent = (line_num * 100) / total_lines;
        mutexUnlock(&g_status_mutex);

        svcSleepThread(1000000ULL);
    }

    fclose(fp);

    mutexLock(&g_status_mutex);
    if (g_cancel) {
        g_status.state = PRINTER_IDLE;
        ch340_send(g_dev, "M112\n", 5);
        ch340_send(g_dev, "M104 S0\n", 8);
        ch340_send(g_dev, "M140 S0\n", 8);
        g_cancel = false;
    } else if (error) {
        g_status.state = PRINTER_ERROR;
    } else {
        g_status.state = PRINTER_IDLE;
        g_status.progress_percent = 100;
        g_status.lines_sent = total_lines;
    }
    mutexUnlock(&g_status_mutex);
}

// ============================================================
Result gcode_start_print(Ch340Device *dev, const char *file_path) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    mutexLock(&g_status_mutex);
    if (g_status.state == PRINTER_PRINTING) {
        mutexUnlock(&g_status_mutex);
        return MAKERESULT(225, 1);
    }
    g_dev = dev;
    strncpy(g_file_path, file_path, sizeof(g_file_path) - 1);
    g_status.state = PRINTER_PRINTING;
    strncpy(g_status.current_file, file_path, sizeof(g_status.current_file) - 1);
    mutexUnlock(&g_status_mutex);

    Result rc = threadCreate(&g_print_thread, print_thread_func, NULL, NULL, 0x8000, 0x2C, -1);
    if (R_FAILED(rc)) {
        mutexLock(&g_status_mutex);
        g_status.state = PRINTER_IDLE;
        mutexUnlock(&g_status_mutex);
        return rc;
    }
    g_thread_running = true;
    threadStart(&g_print_thread);
    return 0;
}

Result gcode_pause(void) {
    g_paused = true;
    mutexLock(&g_status_mutex);
    g_status.state = PRINTER_PAUSED;
    mutexUnlock(&g_status_mutex);
    return 0;
}

Result gcode_resume(void) {
    g_paused = false;
    mutexLock(&g_status_mutex);
    g_status.state = PRINTER_PRINTING;
    mutexUnlock(&g_status_mutex);
    return 0;
}

Result gcode_cancel(Ch340Device *dev) {
    g_cancel = true;
    g_paused = false;
    if (g_thread_running) {
        threadWaitForExit(&g_print_thread);
        threadClose(&g_print_thread);
        g_thread_running = false;
    }
    return 0;
}

Result gcode_send_raw(Ch340Device *dev, const char *gcode_line) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    char buf[GCODE_LINE_MAX + 2];
    snprintf(buf, sizeof(buf), "%s\n", gcode_line);
    return ch340_send(dev, buf, strlen(buf));
}

Result gcode_query_temp(Ch340Device *dev, PrinterTemp *temp) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    Result rc = ch340_send(dev, "M105\n", 5);
    if (R_FAILED(rc)) return rc;

    char buf[128];
    size_t received;
    rc = ch340_recv(dev, buf, sizeof(buf) - 1, 2000, &received);
    if (R_FAILED(rc)) return rc;

    buf[received] = '\0';
    float na = 0, nt = 0, ba = 0, bt = 0;
    char *tpos = strstr(buf, "T:");
    if (tpos) sscanf(tpos, "T:%f /%f B:%f /%f", &na, &nt, &ba, &bt);

    mutexLock(&g_status_mutex);
    g_status.temp.nozzle_actual = na;
    g_status.temp.nozzle_target = nt;
    g_status.temp.bed_actual = ba;
    g_status.temp.bed_target = bt;
    memcpy(temp, &g_status.temp, sizeof(PrinterTemp));
    mutexUnlock(&g_status_mutex);
    return 0;
}

Result gcode_home(Ch340Device *dev) {
    return gcode_send_raw(dev, "G28");
}

Result gcode_move(Ch340Device *dev, float x, float y, float z, float feedrate) {
    char buf[128];
    snprintf(buf, sizeof(buf), "G1 X%.1f Y%.1f Z%.1f F%.0f", x, y, z, feedrate);
    return gcode_send_raw(dev, buf);
}

PrinterStatus *gcode_get_status(void) {
    return &g_status;
}

void gcode_get_status_safe(PrinterStatus *out) {
    mutexLock(&g_status_mutex);
    memcpy(out, &g_status, sizeof(PrinterStatus));
    mutexUnlock(&g_status_mutex);
}

void gcode_update(Ch340Device *dev) {
    mutexLock(&g_status_mutex);
    if (dev && dev->connected) {
        if (g_status.state == PRINTER_OFFLINE) g_status.state = PRINTER_IDLE;
    } else {
        g_status.state = PRINTER_OFFLINE;
    }
    mutexUnlock(&g_status_mutex);
}