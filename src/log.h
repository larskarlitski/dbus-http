#pragma once

typedef enum { LOG_EMERG, LOG_ALERT, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, LOG_DEBUG  } log_level_e;

#define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_notice(...)  log_log(LOG_NOTICE,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warning(...)  log_log(LOG_WARNING,  __FILE__, __LINE__, __VA_ARGS__)
#define log_err(...) log_log(LOG_ERR, __FILE__, __LINE__, __VA_ARGS__)
#define log_crit(...) log_log(LOG_CRIT, __FILE__, __LINE__, __VA_ARGS__)
#define log_alert(...) log_log(LOG_ALERT, __FILE__, __LINE__, __VA_ARGS__)
#define log_emerg(...) log_log(LOG_EMERG, __FILE__, __LINE__, __VA_ARGS__)

log_level_e log_get_level(void);
void log_set_level(log_level_e level);
int log_set_level_str(char* level);
void log_print_levels(void);

void log_log(log_level_e level, const char *file, int line, const char *fmt, ...);
