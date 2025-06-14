#ifndef _LOGGING_H
#define _LOGGING_H

#include <errno.h>

/*
Log serverity level
*/
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
} log_severity_t;

/*
Logs out a message to the screen, adds a newline on its own
*/
void log_message(char *message, log_severity_t severity);

/*
Logs out the most recent system error (indicated by the value of errno)
*/
void log_sys_error();

#endif