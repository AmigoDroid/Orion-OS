/* Funcoes basicas de memoria/texto. O GCC pode gerar chamadas a memset/
 * memcpy por conta propria (mesmo em codigo freestanding), entao elas
 * precisam existir. Este arquivo e compilado com
 * -fno-tree-loop-distribute-patterns para os lacos nao virarem chamadas
 * a si mesmos. */

#include "string.h"

void *memset(void *dst, int c, size_t n)
{
    void *d = dst;
    __asm__ volatile ("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *d = dst;
    const void *s = src;
    __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        memcpy(d, s, n);
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y)
            return *x < *y ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}
