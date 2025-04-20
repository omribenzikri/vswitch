#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "util/logging.h"

void log_message(char *message, log_severity_t severity) {
    char *severity_labels[] = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};

    // Get current timestamp
    time_t timestamp;
    time(&timestamp);

    // Convert to calendar time
    struct tm local = *localtime(&timestamp);

    // Format as string
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);

    // Display formatted log message
    printf("[%s] [%s] %s\n", buf, severity_labels[severity], message);
}

void log_sys_error() {
    // Log out message matching the current error stored in errno
    log_message(strerror(errno), LOG_CRITICAL);
}
