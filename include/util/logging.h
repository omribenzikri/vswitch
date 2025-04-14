#ifndef LOGGING_H
#define LOGGING_H

#include <errno.h>

/*
Log serverity level
*/
enum LogSeverityLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

/*
Logs out a message to the screen, adds a newline on its own
*/
void log_message(const char *message, enum LogSeverityLevel severity);

/*
Logs out the most recent system error (indicated by the value of errno)
*/
void log_sys_error();

#endif