#include "orionLeng.h"

typedef struct {
    const char *key;
    const char *value;
} orionLeng_translation_t;

#include "orionLeng_catalog.h"

typedef struct {
    const char *code;
    const char *name;
} orionLeng_language_t;

/*
 * A lista contém somente idiomas realmente disponibilizados.
 * Outros idiomas poderão ser adicionados sem mudar a API pública.
 */
static const orionLeng_language_t available_languages[] = {
    {"pt-BR", "Português (Brasil)"},{"pt-BR-MA","Português (Maranhece)"}
};

#define ORION_LENG_LANGUAGE_COUNT \
    (sizeof(available_languages) / sizeof(available_languages[0]))

static const orionLeng_language_t *active_language =
    &available_languages[0];

static int strings_equal(const char *a, const char *b)
{
    unsigned int i = 0;

    if (a == 0 || b == 0)
        return 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return 0;
        i++;
    }

    return a[i] == b[i];
}

unsigned int orionLeng_init(void)
{
    /* O padrão inicial é Português do Brasil. */
    active_language = &available_languages[0];
    return 1;
}

const char *orionLeng_current_code(void)
{
    return active_language->code;
}

const char *orionLeng_current_name(void)
{
    return active_language->name;
}

unsigned int orionLeng_language_count(void)
{
    return (unsigned int)ORION_LENG_LANGUAGE_COUNT;
}

const char *orionLeng_language_code(unsigned int index)
{
    if (index >= ORION_LENG_LANGUAGE_COUNT)
        return 0;

    return available_languages[index].code;
}

const char *orionLeng_language_name(unsigned int index)
{
    if (index >= ORION_LENG_LANGUAGE_COUNT)
        return 0;

    return available_languages[index].name;
}

int orionLeng_set_language(const char *code)
{
    unsigned int i;

    if (code == 0)
        return 0;

    for (i = 0; i < ORION_LENG_LANGUAGE_COUNT; i++) {
        if (strings_equal(code, available_languages[i].code)) {
            active_language = &available_languages[i];
            return 1;
        }
    }

    /* Idioma não disponível: mantém a seleção atual. */
    return 0;
}

const char *orionLeng_get(const char *key)
{
    unsigned int i;

    if (key == 0)
        return "";

    /*
     * Cada idioma terá seu próprio catálogo.
     * Nesta primeira versão, somente pt-BR está disponível.
     */
    if (strings_equal(active_language->code, "pt-BR")) {
        for (i = 0; i < ORION_LENG_PT_BR_CATALOG_COUNT; i++) {
            if (strings_equal(key, orionLeng_pt_BR_catalog[i].key))
                return orionLeng_pt_BR_catalog[i].value;
        }
    }

    /* Chaves ausentes retornam o identificador para facilitar depuração. */
    return key;
}
