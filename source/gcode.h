#pragma once
#include "usb_ch340.h"
#include "config.h"

typedef enum {
    PRINTER_OFFLINE = 0,
    PRINTER_IDLE,
    PRINTER_PRINTING,
    PRINTER_PAUSED,
    PRINTER_ERROR,
    PRINTER_STATE_COUNT
} PrinterState;

typedef struct {
    float nozzle_actual;
    float nozzle_target;
    float bed_actual;
    float bed_target;
} PrinterTemp;

typedef struct {
    PrinterState state;
    PrinterTemp  temp;
    int   progress_percent;
    int   lines_sent;
    int   lines_total;
    char  current_file[256];
    float pos_x, pos_y, pos_z;
} PrinterStatus;

Result gcode_init(void);
Result gcode_start_print(Ch340Device *dev, const char *file_path);
Result gcode_pause(void);
Result gcode_resume(void);
Result gcode_cancel(Ch340Device *dev);
Result gcode_send_raw(Ch340Device *dev, const char *gcode_line);
Result gcode_query_temp(Ch340Device *dev, PrinterTemp *temp);
Result gcode_home(Ch340Device *dev);
// JOG movement using G91 relative positioning, only non-zero axes
// coords in mm, feedrate in mm/min, boundary-checked against PRINTER_BED_*
Result gcode_move(Ch340Device *dev, float x, float y, float z, float feedrate);
void gcode_get_status_safe(PrinterStatus *out);
void gcode_update(Ch340Device *dev);
