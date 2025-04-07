#include <stdio.h>
#include <time.h>
#include "logging.h"

void log_message(const char *message, enum LogSeverityLevel severity) {
    const char *severity_labels[] = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};

    // Get current time
    time_t epoch;
    struct tm local;
    time(&epoch);
    localtime_r(&epoch, &local);

    // Format current time
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);

    // Display formatted log message
    printf("[%s] [%s] %s\n", buf, severity_labels[severity], message);
}
