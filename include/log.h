
#ifndef ORION_LOG_H
#define ORION_LOG_H

void log_init(void);
void log_write(const char *message);
void log_ok(const char *message);
void log_error(const char *message);
void log_hex(const char *label, unsigned int value);

#endif
