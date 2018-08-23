
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "log.h"

// Longer lines are cut
#define MAX_LOG_STRING_LENGTH 256


static const char *level_names[] = {
        "EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG", NULL
};

static log_level_e level_priv = LOG_NOTICE;

log_level_e log_get_level(void) {
        return level_priv;
}

void log_set_level(log_level_e level) {
        level_priv = level;
}


int log_set_level_str(char *level) {
        int index = 0;
        for(const char **level_str = level_names; *level_str != NULL; level_str++ ) {
                if(strcmp(*level_str, level) == 0){
                        level_priv = index;
                        return index;
                }
                index++;
        }
        return -1;
}

static void log_log_v(log_level_e level, const char *file, int line, const char *fmt, va_list ap) {
        char log_buffer[MAX_LOG_STRING_LENGTH];
        int log_buffer_len;

        if (level_priv < level) {
                return;
        }

        log_buffer_len = snprintf(log_buffer, sizeof(log_buffer)-2, "<%u> %s:%d: ", level, file, line);
        log_buffer_len += vsnprintf(log_buffer + log_buffer_len, sizeof(log_buffer)-2 - log_buffer_len, fmt, ap);
        log_buffer[log_buffer_len++] = '\n';
        log_buffer[log_buffer_len] = 0;
        fwrite(log_buffer, 1, log_buffer_len, stdout);
        fflush(stdout);
}

void log_log(log_level_e level, const char *file, int line, const char *fmt, ...) {
        va_list ap;

        if (level_priv < level) {
                return;
        }

        va_start(ap, fmt);
        log_log_v(level, file, line, fmt, ap);
        va_end(ap);
}

void log_print_levels(void) {
        for(const char **level_str = level_names; *level_str != NULL; level_str++ ) {
                printf("%s ", *level_str);
        }
}
