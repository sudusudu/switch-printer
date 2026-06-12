#include "crash.h"
#include "logger.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ============================================================
// Atmosphere 崩溃处理 实现
// ============================================================

static bool g_crash_inited = false;

Result crash_init(void) {
    // 确保崩溃转储目录存在
    mkdir("sdmc:/switch", 0777);

    // 检查上次是否有遗留的崩溃转储
    FILE *fp = fopen(CRASH_DUMP_PATH, "r");
    if (fp) {
        fclose(fp);
        LOG_W("Previous crash dump found at %s", CRASH_DUMP_PATH);
    }

    g_crash_inited = true;
    LOG_I("Crash handler initialized");
    return 0;
}

void crash_exit(void) {
    g_crash_inited = false;
}

void crash_dump_registers(void) {
    if (!g_crash_inited) return;

    // 刷新日志缓冲区，确保不丢数据
    logger_flush();

    FILE *fp = fopen(CRASH_DUMP_PATH, "w");
    if (!fp) return;

    // 时间戳 (AArch64: u64 = unsigned long, 用 %lu 格式)
    u64 tick = armGetSystemTick();
    u64 sec  = tick / 19200000ULL;
    fprintf(fp, "=== Switch Printer Crash Dump ===\n");
    fprintf(fp, "Tick: %lu (approx %lus from boot)\n", tick, sec);
    fprintf(fp, "Atmosphere CFW - System Version not queried\n");

    // 运行状态取证
    fprintf(fp, "\n--- Program State ---\n");
    fprintf(fp, "Crash dump triggered programmatically.\n");
    fprintf(fp, "For full register dump, check Atmosphere fatal report.\n");
    fprintf(fp, "Check %s for last runtime log entries.\n", LOG_FILE_PATH);

    fclose(fp);
    LOG_I("Crash dump written to %s", CRASH_DUMP_PATH);
}
