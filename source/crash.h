#pragma once
#include <switch.h>

// ============================================================
// Atmosphere 崩溃处理
// - 注册 fatal 重定向 handler
// - 崩溃时自动写寄存器 + backtrace 到 CRASH_DUMP_PATH
// - 同时刷新日志缓冲区
// ============================================================

// 初始化崩溃处理器（必须在 logger_init 之后调用）
Result crash_init(void);

// 注销崩溃处理器
void   crash_exit(void);

// 手动触发崩溃转储（用于可恢复错误的取证，不会终止程序）
void   crash_dump_registers(void);
