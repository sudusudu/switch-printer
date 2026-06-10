#pragma once
#include <switch.h>
#include "gcode.h"

// ============================================================
// 初始化 HTTP 服务器（必须在 socketInitializeDefault 之后调用）
// ============================================================
Result httpd_init(void);

// ============================================================
// 启动 HTTP 服务器（在独立线程中运行，幂等）
// printer_dev: 已连接的 CH340 设备指针
// ============================================================
Result httpd_start(Ch340Device *printer_dev);
void httpd_stop(void);

// 获取本机 WiFi IP（未连接时返回 "0.0.0.0"）
const char *httpd_get_ip(void);

// ============================================================
// 获取 API 认证 Token（16 字符 hex，启动时随机生成）
// 仅返回 token 字符串；认证逻辑在 httpd.c 内部
// ============================================================
const char *httpd_get_auth_token(void);
