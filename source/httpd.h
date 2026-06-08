#pragma once
#include <switch.h>
#include "gcode.h"

Result httpd_init(void);
Result httpd_start(Ch340Device *printer_dev);
void httpd_stop(void);
const char *httpd_get_ip(void);