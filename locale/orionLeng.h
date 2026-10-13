#ifndef ORION_LENG_H
#define ORION_LENG_H

/*
 * orionLeng - Gerenciador de idiomas do Orion OS.
 * Os textos retornados pelo gerenciador usam UTF-8.
 */

unsigned int orionLeng_init(void);

const char *orionLeng_current_code(void);
const char *orionLeng_current_name(void);

unsigned int orionLeng_language_count(void);
const char *orionLeng_language_code(unsigned int index);
const char *orionLeng_language_name(unsigned int index);

int orionLeng_set_language(const char *code);
const char *orionLeng_get(const char *key);

#endif
