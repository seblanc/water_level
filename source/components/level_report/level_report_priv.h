/* Internal to the level_report component. */
#pragma once

#include "level_report.h"

/* Stage 1 console sink (level_report_uart.c). ctx is unused. */
void level_report_console_sink(const level_reading_t *r, void *ctx);
