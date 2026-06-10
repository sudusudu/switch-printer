#include <stdatomic.h>
#include <math.h>
#include "gcode.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static PrinterStatus g_status;
static atomic_bool g_paused = false;
static atomic_bool g_cancel = false;
static atomic_bool g_thread_running = false;
static Mutex g_status_mutex;

static Thread g_print_thread;
static Ch340Device *g_dev = NULL;
static char g_file_path[256];

// ============================================================
// 危险 G-code/M-code 黑名单（大小写不敏感，前缀匹配）
// ============================================================
static const char *DANGEROUS_CODES[] = {
    "M500",  // 保存到 EEPROM（磨损闪存）
    "M502",  // 恢复出厂设置
    "M303",  // PID 自整定
    NULL
};

static bool is_dangerous_gcode(const char *line) {
    while (*line == ' ' || *line == '\t' || *line == '\r') line++;
    if (*line == '\0') return false;
    for (int i = 0; DANGEROUS_CODES[i] != NULL; i++) {
        size_t len = strlen(DANGEROUS_CODES[i]);
        bool match = true;
        for (size_t j = 0; j < len; j++) {
            char a = line[j], b = DANGEROUS_CODES[i][j];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) { match = false; break; }
        }
        if (match) {
            char c = line[len];
            if (c == '\0' || c == ' ' || c == '\n' || c == '\r')
                return true;
        }
    }
    return false;
}

Result gcode_init(void) {
    mutexInit(&g_status_mutex);
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = PRINTER_OFFLINE;
    return 0;
}

static Result wait_ok(Ch340Device *dev, u64 timeout_ms) {
    char buf[128];
    size_t received;
    u64 deadline = armGetSystemTick() + armNsToTicks(timeout_ms * 1000000ULL);

    while (armGetSystemTick() < deadline) {
        if (atomic_load(&g_cancel)) return MAKERESULT(Module_Libnx, 5);

        Result rc = ch340_recv(dev, buf, sizeof(buf) - 1, 100, &received);
        if (R_FAILED(rc)) {
            if (rc == MAKERESULT(0xEA01, 0)) continue;
            return rc;
        }
        if (received > 0) {
            buf[received] = '\0';
            char *tpos = strstr(buf, "T:");
            if (tpos) {
                float na = 0, nt = 0, ba = 0, bt = 0;
                int n = sscanf(tpos, "T:%f /%f B:%f /%f", &na, &nt, &ba, &bt);
                if (n >= 1 && isfinite(na) && isfinite(nt) && isfinite(ba) && isfinite(bt)) {
                    mutexLock(&g_status_mutex);
                    g_status.temp.nozzle_actual = na;
                    g_status.temp.nozzle_target = nt;
                    g_status.temp.bed_actual = ba;
                    g_status.temp.bed_target = bt;
                    mutexUnlock(&g_status_mutex);
                }
            }
            if (buf[0] == 'o' && buf[1] == 'k' &&
                (buf[2] == '\0' || buf[2] == '\n' || buf[2] == '\r' || buf[2] == ' '))
                return 0;
        }
    }
    return MAKERESULT(Module_Libnx, 5);
}

static void print_thread_func(void *arg) {
    (void)arg;
    FILE *fp = fopen(g_file_path, "r");
    if (!fp) {
        mutexLock(&g_status_mutex);
        g_status.state = PRINTER_ERROR;
        mutexUnlock(&g_status_mutex);
        atomic_store(&g_thread_running, false);
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

    while (fgets(line_buf, sizeof(line_buf), fp) &&
           !atomic_load(&g_cancel)) {
        while (atomic_load(&g_paused) && !atomic_load(&g_cancel))
            svcSleepThread(100000000ULL);
        if (atomic_load(&g_cancel)) break;

        size_t len = strlen(line_buf);
        if (len == GCODE_LINE_MAX - 1 && line_buf[len-1] != '\n' && line_buf[len-1] != '\r') {
            int c;
            while ((c = fgetc(fp)) != EOF && c != '\n') {}
            continue;
        }
        while (len > 0 && (line_buf[len-1] == '\n' || line_buf[len-1] == '\r'))
            line_buf[--len] = '\0';

        if (len == 0 || line_buf[0] == ';') continue;

        // 跳过危险命令
        if (is_dangerous_gcode(line_buf)) continue;

        if (len >= GCODE_LINE_MAX - 2) len = GCODE_LINE_MAX - 3;
        line_buf[len] = '\n'; line_buf[len+1] = '\0';

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

    if (ferror(fp)) error = true;
    fclose(fp);

    mutexLock(&g_status_mutex);
    if (atomic_load(&g_cancel)) {
        g_status.state = PRINTER_IDLE;
        (void)ch340_send(g_dev, "M112\n", 5);
        (void)ch340_send(g_dev, "M104 S0\n", 8);
        (void)ch340_send(g_dev, "M140 S0\n", 8);
        atomic_store(&g_cancel, false);
    } else if (error) {
        g_status.state = PRINTER_ERROR;
        (void)ch340_send(g_dev, "M104 S0\n", 8);
        (void)ch340_send(g_dev, "M140 S0\n", 8);
    } else {
        g_status.state = PRINTER_IDLE;
        g_status.progress_percent = 100;
        g_status.lines_sent = total_lines;
    }
    mutexUnlock(&g_status_mutex);
    atomic_store(&g_thread_running, false);
}

Result gcode_start_print(Ch340Device *dev, const char *file_path) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (atomic_load(&g_thread_running)) return MAKERESULT(225, 1);

    mutexLock(&g_status_mutex);
    if (g_status.state == PRINTER_PRINTING || g_status.state == PRINTER_PAUSED) {
        mutexUnlock(&g_status_mutex);
        return MAKERESULT(225, 1);
    }
    g_dev = dev;
    strncpy(g_file_path, file_path, sizeof(g_file_path) - 1);
    g_file_path[sizeof(g_file_path) - 1] = '\0';
    g_status.state = PRINTER_PRINTING;
    strncpy(g_status.current_file, file_path, sizeof(g_status.current_file) - 1);
    g_status.current_file[sizeof(g_status.current_file) - 1] = '\0';
    mutexUnlock(&g_status_mutex);

    Result rc = threadCreate(&g_print_thread, print_thread_func, NULL, NULL,
                             PRINT_THREAD_STACK_SIZE, PRINT_THREAD_PRIORITY, THREAD_DEFAULT_CORE);
    if (R_FAILED(rc)) {
        mutexLock(&g_status_mutex);
        g_status.state = PRINTER_IDLE;
        mutexUnlock(&g_status_mutex);
        return rc;
    }
    atomic_store(&g_thread_running, true);
    threadStart(&g_print_thread);
    return 0;
}

Result gcode_pause(void) {
    atomic_store(&g_paused, true);
    mutexLock(&g_status_mutex);
    g_status.state = PRINTER_PAUSED;
    mutexUnlock(&g_status_mutex);
    return 0;
}

Result gcode_resume(void) {
    atomic_store(&g_paused, false);
    mutexLock(&g_status_mutex);
    g_status.state = PRINTER_PRINTING;
    mutexUnlock(&g_status_mutex);
    return 0;
}

Result gcode_cancel(Ch340Device *dev) {
    (void)dev;
    atomic_store(&g_cancel, true);
    atomic_store(&g_paused, false);
    threadWaitForExit(&g_print_thread);
    threadClose(&g_print_thread);
    atomic_store(&g_thread_running, false);
    return 0;
}

Result gcode_send_raw(Ch340Device *dev, const char *gcode_line) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);

    // 检查危险命令
    if (is_dangerous_gcode(gcode_line))
        return MAKERESULT(225, 3);

    char buf[GCODE_LINE_MAX + 2];
    snprintf(buf, sizeof(buf), "%s\n", gcode_line);
    return ch340_send(dev, buf, strlen(buf));
}

Result gcode_query_temp(Ch340Device *dev, PrinterTemp *temp) {
    if (!dev || !temp) return MAKERESULT(Module_Libnx, 1);
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
    if (tpos) {
        int n = sscanf(tpos, "T:%f /%f B:%f /%f", &na, &nt, &ba, &bt);
        if (n < 1 || !isfinite(na)) na = 0;
        if (n < 2 || !isfinite(nt)) nt = 0;
        if (n < 3 || !isfinite(ba)) ba = 0;
        if (n < 4 || !isfinite(bt)) bt = 0;
    }

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
    if (!dev || !dev->connected) return MAKERESULT(Module_Libnx, 1);

    if (x < -PRINTER_BED_X || x > PRINTER_BED_X ||
        y < -PRINTER_BED_Y || y > PRINTER_BED_Y ||
        z < -PRINTER_BED_Z || z > PRINTER_BED_Z)
        return MAKERESULT(225, 2);

    char buf[128];
    int pos = snprintf(buf, sizeof(buf), "G91\nG1");
    if (x != 0.0f) pos += snprintf(buf + pos, sizeof(buf) - pos, " X%.1f", x);
    if (y != 0.0f) pos += snprintf(buf + pos, sizeof(buf) - pos, " Y%.1f", y);
    if (z != 0.0f) pos += snprintf(buf + pos, sizeof(buf) - pos, " Z%.1f", z);
    if (feedrate > 0) snprintf(buf + pos, sizeof(buf) - pos, " F%.0f", feedrate);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nG90");

    return gcode_send_raw(dev, buf);
}

void gcode_get_status_safe(PrinterStatus *out) {
    mutexLock(&g_status_mutex);
    memcpy(out, &g_status, sizeof(PrinterStatus));
    mutexUnlock(&g_status_mutex);
}

void gcode_update(Ch340Device *dev) {
    mutexLock(&g_status_mutex);
    if (dev && dev->connected) {
        if (g_status.state == PRINTER_OFFLINE)
            g_status.state = PRINTER_IDLE;
    } else {
        if (g_status.state != PRINTER_ERROR)
            g_status.state = PRINTER_OFFLINE;
    }
    mutexUnlock(&g_status_mutex);
}
