#ifndef ORION_RTC_H
#define ORION_RTC_H

#include <stdint.h>

/* Relogio CMOS (portas 0x70/0x71). Hora em UTC, como o QEMU a entrega. */

/* Segundos desde 1970-01-01 (valido ate 2106). 0 se o RTC nao responde. */
uint32_t rtc_unix_time(void);

/* Escreve "AAAA-MM-DD HH:MM" (17 bytes com o '\0'). t == 0 vira "-". */
void rtc_format(uint32_t t, char *out);

#endif
