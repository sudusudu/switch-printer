#pragma once

// --- WiFi ---
// 必填：改成你的 WiFi 名和密码
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASS"

// --- HTTP Server ---
#define HTTP_PORT      8080

// --- USB CH340 ---
#define CH340_VID      0x1A86
#define CH340_PID      0x7523
#define CH340_BAUDRATE 115200

// --- G-code ---
#define GCODE_LINE_MAX   256
#define GCODE_OK_TIMEOUT 30000
#define GCODE_RING_SIZE  64

// --- Printer ---
#define PRINTER_BED_SIZE_X  400
#define PRINTER_BED_SIZE_Y  400
#define PRINTER_BED_SIZE_Z  450