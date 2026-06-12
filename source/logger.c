#include "logger.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

// ============================================================
// SD 卡日志系统 实现
// ============================================================

static FILE   *g_log_fp    = NULL;
static Mutex   g_log_mutex;
static int     g_log_count = 0;
static size_t  g_log_bytes = 0;

// 时间戳缓冲区
static char g_timestamp[32];

static void make_timestamp(void) {
    u64 tick = armGetSystemTick();
    u64 sec  = tick / 19200000ULL;  // armGetSystemTick 频率约 19.2MHz
    u64 h    = (sec / 3600) % 24;
    u64 m    = (sec / 60) % 60;
    u64 s    = sec % 60;
    u64 ms   = (tick % 19200000ULL) / 19200ULL;
    snprintf(g_timestamp, sizeof(g_timestamp),
             "%02lu:%02lu:%02lu.%03lu", h, m, s, ms);
}

Result logger_init(void) {
    mutexInit(&g_log_mutex);

    // 确保目录存在
    mkdir("sdmc:/switch", 0777);

    // 读取现有文件大小
    struct stat st;
    g_log_bytes = 0;
    if (stat(LOG_FILE_PATH, &st) == 0)
        g_log_bytes = (size_t)st.st_size;

    // 打开日志文件（追加模式）
    g_log_fp = fopen(LOG_FILE_PATH, "a");
    if (!g_log_fp) {
        // 尝试截断后重新打开
        g_log_fp = fopen(LOG_FILE_PATH, "w");
        if (!g_log_fp) return MAKERESULT(Module_Libnx, 1);
        g_log_bytes = 0;
    }

    make_timestamp();
    fprintf(g_log_fp, "\n--- Switch Printer v1.1.0 log start [%s] ---\n", g_timestamp);
    fflush(g_log_fp);
    g_log_count = 0;

    return 0;
}

void logger_exit(void) {
    mutexLock(&g_log_mutex);
    if (g_log_fp) {
        make_timestamp();
        fprintf(g_log_fp, "--- Log end [%s] ---\n", g_timestamp);
        fflush(g_log_fp);
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    mutexUnlock(&g_log_mutex);
}

void logger_write(LogLevel level, const char *fmt, ...) {
    mutexLock(&g_log_mutex);
    if (!g_log_fp) { mutexUnlock(&g_log_mutex); return; }

    make_timestamp();

    const char *lv;
    switch (level) {
        case LOG_INFO:  lv = "INFO";  break;
        case LOG_WARN:  lv = "WARN";  break;
        case LOG_ERROR: lv = "ERROR"; break;
        case LOG_PRINT: lv = "PRINT"; break;
        default:        lv = "??";    break;
    }

    // 头部
    int wrote = fprintf(g_log_fp, "[%s] %-5s ", g_timestamp, lv);
    g_log_bytes += wrote;

    // 可变消息体
    va_list args;
    va_start(args, fmt);
    wrote = vfprintf(g_log_fp, fmt, args);
    va_end(args);
    g_log_bytes += wrote;

    wrote = fprintf(g_log_fp, "\n");
    g_log_bytes += wrote;

    // 批量刷新
    g_log_count++;
    if (g_log_count >= LOG_FLUSH_EVERY) {
        fflush(g_log_fp);
        g_log_count = 0;
    }

    // 环形截断：超过最大大小后截掉前半
    if (g_log_bytes > LOG_MAX_SIZE) {
        fclose(g_log_fp);
        // 重命名 -> 读 -> 截取后半 -> 写回
        char tmp_path[280];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", LOG_FILE_PATH);
        rename(LOG_FILE_PATH, tmp_path);
        FILE *old = fopen(tmp_path, "r");
        g_log_fp = fopen(LOG_FILE_PATH, "w");
        if (old && g_log_fp) {
            size_t half = g_log_bytes / 2;
            fseek(old, (long)half, SEEK_SET);
            // 对齐到下一行开头
            int c;
            while ((c = fgetc(old)) != EOF && c != '\n') {}
            char line[512];
            while (fgets(line, sizeof(line), old))
                fputs(line, g_log_fp);
            fclose(old);
            remove(tmp_path);
        } else {
            if (old) fclose(old);
            if (!g_log_fp) g_log_fp = fopen(LOG_FILE_PATH, "w");
            remove(tmp_path);
        }
        g_log_bytes = 0;
        fprintf(g_log_fp, "[%s] INFO  Log truncated (ring buffer)\n", g_timestamp);
        fflush(g_log_fp);
    }

    mutexUnlock(&g_log_mutex);
}

void logger_flush(void) {
    mutexLock(&g_log_mutex);
    if (g_log_fp) fflush(g_log_fp);
    mutexUnlock(&g_log_mutex);
}
