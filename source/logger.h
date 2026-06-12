#pragma once
#include <switch.h>

// ============================================================
// SD 卡日志系统
// - 环形缓冲区 + 批量刷写到 SD 卡
// - 关键事件即时记录（打印开始/暂停/错误/USB断开）
// - 日志文件自动截断到 LOG_MAX_SIZE
// ============================================================

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_PRINT   // 打印相关事件
} LogLevel;

// 初始化日志系统（打开/创建日志文件）
Result logger_init(void);

// 关闭日志系统（刷新并关闭文件）
void   logger_exit(void);

// 写日志（线程安全）。自动加时间戳前缀
void   logger_write(LogLevel level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// 强制刷新缓冲区到 SD 卡（关键路径调用，如崩溃前）
void   logger_flush(void);

// 便捷宏
#define LOG_I(fmt, ...) logger_write(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) logger_write(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) logger_write(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_P(fmt, ...) logger_write(LOG_PRINT, fmt, ##__VA_ARGS__)
