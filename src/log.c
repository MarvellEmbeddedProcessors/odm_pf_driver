/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "log.h"

void
log_write(int log_lvl, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsyslog(log_lvl, format, args);
	va_end(args);
}

void
log_init(const char *id, int log_lvl, bool console_logging_enabled)
{
	int flags = LOG_NDELAY | LOG_PID;

	if (console_logging_enabled)
		flags |= LOG_PERROR;

	setlogmask(LOG_UPTO(log_lvl));

	openlog(id, flags, LOG_DAEMON);
}

void
log_fini(void)
{
	closelog();
}
