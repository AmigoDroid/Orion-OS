#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int update_in_progress(void)
{
    return cmos(0x0A) & 0x80;
}

typedef struct { uint8_t s, m, h, d, mo, y; } raw_t;

static void read_raw(raw_t *r)
{
    r->s = cmos(0x00);
    r->m = cmos(0x02);
    r->h = cmos(0x04);
    r->d = cmos(0x07);
    r->mo = cmos(0x08);
    r->y = cmos(0x09);
}

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

/* Dias desde 1970-01-01 (algoritmo de Howard Hinnant, so 32 bits). */
static uint32_t days_from_civil(int y, unsigned m, unsigned d)
{
    int era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = y / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
}

static void civil_from_days(uint32_t z, int *y, unsigned *m, unsigned *d)
{
    unsigned doe, yoe, doy, mp;
    int era;

    z += 719468;
    era = (int)(z / 146097);
    doe = z - (uint32_t)era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)yoe + era * 400 + (*m <= 2);
}

uint32_t rtc_unix_time(void)
{
    raw_t a, b;
    uint8_t status_b;
    int tries = 0;

    for (int i = 0; i < 100000 && update_in_progress(); i++) { }

    /* Le duas vezes ate dar igual: evita pegar a virada de um segundo. */
    read_raw(&a);
    do {
        b = a;
        for (int i = 0; i < 100000 && update_in_progress(); i++) { }
        read_raw(&a);
    } while (++tries < 5 &&
             (a.s != b.s || a.m != b.m || a.h != b.h ||
              a.d != b.d || a.mo != b.mo || a.y != b.y));

    status_b = cmos(0x0B);
    if (!(status_b & 0x04)) {                       /* valores em BCD */
        int pm = a.h & 0x80;

        a.s = bcd(a.s); a.m = bcd(a.m);
        a.h = bcd(a.h & 0x7F);
        a.d = bcd(a.d); a.mo = bcd(a.mo); a.y = bcd(a.y);
        if (!(status_b & 0x02) && pm)               /* 12 h com PM */
            a.h = (uint8_t)((a.h % 12) + 12);
    } else if (!(status_b & 0x02) && (a.h & 0x80)) {
        a.h = (uint8_t)(((a.h & 0x7F) % 12) + 12);
    }

    if (a.mo < 1 || a.mo > 12 || a.d < 1 || a.d > 31 || a.h > 23 ||
        a.m > 59 || a.s > 59)
        return 0;

    return days_from_civil(2000 + a.y, a.mo, a.d) * 86400u +
           a.h * 3600u + a.m * 60u + a.s;
}

void rtc_format(uint32_t t, char *out)
{
    int y;
    unsigned mo, d, secs;
    static const char dig[] = "0123456789";

    if (t == 0) {
        out[0] = '-';
        out[1] = '\0';
        return;
    }

    civil_from_days(t / 86400u, &y, &mo, &d);
    secs = t % 86400u;

    out[0] = dig[(y / 1000) % 10];
    out[1] = dig[(y / 100) % 10];
    out[2] = dig[(y / 10) % 10];
    out[3] = dig[y % 10];
    out[4] = '-';
    out[5] = dig[mo / 10];
    out[6] = dig[mo % 10];
    out[7] = '-';
    out[8] = dig[d / 10];
    out[9] = dig[d % 10];
    out[10] = ' ';
    out[11] = dig[(secs / 3600) / 10];
    out[12] = dig[(secs / 3600) % 10];
    out[13] = ':';
    out[14] = dig[((secs / 60) % 60) / 10];
    out[15] = dig[((secs / 60) % 60) % 10];
    out[16] = '\0';
}
