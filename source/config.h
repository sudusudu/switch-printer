#pragma once

// --- WiFi ---
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASS"

// --- HTTP Server ---
#define HTTP_PORT             8080
#define HTTP_REQ_BUF_SIZE     16384
#define HTTP_UPLOAD_MAX_MB    100

// --- USB CH340 ---
#define CH340_VID         0x1A86
#define CH340_PID         0x7523
#define CH340_BAUDRATE    250000
#define CH340_BUF_SIZE    0x1000

// --- G-code ---
#define GCODE_LINE_MAX           256
#define GCODE_OK_TIMEOUT         30000
#define GCODE_EST_BYTES_PER_LINE 50

// --- JOG ---
#define JOG_FEEDRATE  1000

// --- Printer Bed ---
#define PRINTER_BED_X  400
#define PRINTER_BED_Y  400
#define PRINTER_BED_Z  450

// --- Temperature ---
#define PRINTER_NOZZLE_MAX_TEMP   300
#define PRINTER_BED_MAX_TEMP      120
#define TEMP_THRESHOLD_COLD       40
#define TEMP_THRESHOLD_WARM       120
#define TEMP_THRESHOLD_HOT        190
