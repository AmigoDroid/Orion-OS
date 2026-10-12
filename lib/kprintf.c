#include "kprintf.h"
#include "terminal.h"
#include "log.h"

typedef struct {
    void (*put)(void *ctx, char c);
    void *ctx;
} sink_t;

static void put_term(void *ctx, char c) { (void)ctx; terminal_putchar(c); }
static void put_log(void *ctx, char c)  { (void)ctx; log_putc(c); }

typedef struct { char *buf; unsigned int size, len; } bufctx_t;

static void put_buf(void *ctx, char c)
{
    bufctx_t *b = ctx;
    if (b->len + 1 < b->size)
        b->buf[b->len] = c;
    b->len++;
}

static void emit_padded(const sink_t *s, const char *text, int len,
                        int width, int zero, int left)
{
    int pad = width > len ? width - len : 0;

    if (!left) {
        while (pad--)
            s->put(s->ctx, zero ? '0' : ' ');
        pad = 0;
    }
    for (int i = 0; i < len; i++)
        s->put(s->ctx, text[i]);
    while (left && pad--)
        s->put(s->ctx, ' ');
}

static void vformat(const sink_t *s, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        char tmp[16];
        int width = 0, zero = 0, left = 0, len;

        if (*fmt != '%') {
            s->put(s->ctx, *fmt);
            continue;
        }

        fmt++;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        switch (*fmt) {
        case '%':
            s->put(s->ctx, '%');
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            emit_padded(s, tmp, 1, width, 0, left);
            break;
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            for (len = 0; str[len]; len++) { }
            emit_padded(s, str, len, width, 0, left);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            unsigned int u = v < 0 ? 0u - (unsigned int)v : (unsigned int)v;
            char rev[12];
            int n = 0;
            do { rev[n++] = (char)('0' + u % 10); u /= 10; } while (u);
            len = 0;
            if (v < 0)
                tmp[len++] = '-';
            while (n)
                tmp[len++] = rev[--n];
            emit_padded(s, tmp, len, width, zero && !left, left);
            break;
        }
        case 'u': {
            unsigned int u = va_arg(ap, unsigned int);
            char rev[12];
            int n = 0;
            do { rev[n++] = (char)('0' + u % 10); u /= 10; } while (u);
            len = 0;
            while (n)
                tmp[len++] = rev[--n];
            emit_padded(s, tmp, len, width, zero && !left, left);
            break;
        }
        case 'p':
            s->put(s->ctx, '0');
            s->put(s->ctx, 'x');
            width = 8;
            zero = 1;
            /* fallthrough */
        case 'x':
        case 'X': {
            unsigned int u = va_arg(ap, unsigned int);
            const char *digits = *fmt == 'X' ? "0123456789ABCDEF"
                                             : "0123456789abcdef";
            char rev[8];
            int n = 0;
            do { rev[n++] = digits[u & 15]; u >>= 4; } while (u);
            len = 0;
            while (n)
                tmp[len++] = rev[--n];
            emit_padded(s, tmp, len, width, zero && !left, left);
            break;
        }
        case '\0':
            return;
        default:
            s->put(s->ctx, '%');
            s->put(s->ctx, *fmt);
            break;
        }
    }
}

void kvprintf(const char *fmt, va_list ap)
{
    sink_t s = { put_term, 0 };
    vformat(&s, fmt, ap);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

void klog(const char *fmt, ...)
{
    sink_t s = { put_log, 0 };
    va_list ap;
    va_start(ap, fmt);
    vformat(&s, fmt, ap);
    va_end(ap);
}

int ksnprintf(char *buf, unsigned int size, const char *fmt, ...)
{
    bufctx_t b = { buf, size, 0 };
    sink_t s = { put_buf, &b };
    va_list ap;

    va_start(ap, fmt);
    vformat(&s, fmt, ap);
    va_end(ap);

    if (size) {
        unsigned int end = b.len < size ? b.len : size - 1;
        buf[end] = '\0';
    }
    return (int)b.len;
}
