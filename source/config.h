#pragma once

// --- WiFi ---
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASS"

// --- HTTP Server ---
#define HTTP_PORT          8080
#define HTTP_REQ_BUF_SIZE  16384

// --- USB CH340 ---
#define CH340_VID      0x1A86
#define CH340_PID      0x7523
#define CH340_BAUDRATE 250000

// --- G-code ---
#define GCODE_LINE_MAX    256
#define GCODE_OK_TIMEOUT  30000

// --- Printer ---
#define PRINTER_BED_X  400
#define PRINTER_BED_Y  400
#define PRINTER_BED_Z  450