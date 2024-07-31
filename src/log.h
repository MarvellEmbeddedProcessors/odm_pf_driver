/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

/**
 * @file
 *
 * Logging library
 *
 * APIs to log messages. The log messages can be written to syslog/console.
 * The log levels used are defined in syslog.h.
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <stdbool.h>
#include <syslog.h>

/**
 * Initialize the logging library.
 *
 * @param	id			Identifier to be used in the syslog. If NULL, the program
 * 					name is used.
 * @param	log_lvl			Log level to be used. Only messages with log level <=
 * 					log_lvl are logged. The log levels are defined in syslog.h.
 * @param	console_logging_enabled	Enable console logging. If true, log messages are written
 * 					to stderr.
 */
void log_init(const char *id, int log_lvl, bool console_logging_enabled);

/**
 * Write a log message.
 *
 * @param	log_lvl	Log level of the message.
 * @param	format	Format string.
 */
void log_write(int log_lvl, const char *format, ...);

/**
 * Cleanup the logging library.
 */
void log_fini(void);

#endif /* __LOG_H__ */
