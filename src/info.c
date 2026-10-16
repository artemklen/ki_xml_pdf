#include "info.h"

#include <string.h>

void info_clear(PdfInfo *info) {
    memset(info, 0, sizeof *info);
}

int info_present(const PdfInfo *info) {
    return info != NULL && (info->has_title || info->has_producer || info->has_date);
}

/* Strict UTF-8: no overlong forms, surrogates or values above U+10FFFF.
   Returns the sequence length, or 0 when s[0..n) does not start with one. */
static size_t utf8_scalar(const unsigned char *s, size_t n, unsigned *cp) {
    unsigned c = s[0];
    size_t len;
    unsigned min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        min = 0x80;
        c &= 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        min = 0x800;
        c &= 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        min = 0x10000;
        c &= 0x07;
    } else {
        return 0;
    }
    if (n < len) {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        return 0;
    }
    *cp = c;
    return len;
}

/* Tab, LF and CR are kept; other C0 controls, DEL and C1 controls are not. */
static int text_allowed(unsigned cp) {
    if (cp == 0x09 || cp == 0x0A || cp == 0x0D) {
        return 1;
    }
    return !(cp < 0x20 || (cp >= 0x7F && cp <= 0x9F));
}

static int take_text(char *dst, const char *src, const char **why, const char *bad_len, const char *bad_text) {
    size_t n = strnlen(src, KI_INFO_TEXT_MAX + 1);
    if (n == 0 || n > KI_INFO_TEXT_MAX) {
        *why = bad_len;
        return -1;
    }
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n;) {
        unsigned cp = 0;
        size_t k = utf8_scalar(s + i, n - i, &cp);
        if (k == 0 || !text_allowed(cp)) {
            *why = bad_text;
            return -1;
        }
        i += k;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

static int digits(const char *s, int n, int *out) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

static int month_days(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

/* YYYY-MM-DDTHH:MM:SS followed by Z or +HH:MM / -HH:MM. */
static int take_date(char *dst, const char *src) {
    size_t n = strnlen(src, 26);
    if (n != 20 && n != 25) {
        return -1;
    }
    int y, mo, d, h, mi, s;
    if (digits(src, 4, &y) || src[4] != '-' || digits(src + 5, 2, &mo) || src[7] != '-' ||
        digits(src + 8, 2, &d) || src[10] != 'T' || digits(src + 11, 2, &h) || src[13] != ':' ||
        digits(src + 14, 2, &mi) || src[16] != ':' || digits(src + 17, 2, &s)) {
        return -1;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > month_days(y, mo) || h > 23 || mi > 59 || s > 59) {
        return -1;
    }
    char body[16];
    memcpy(body, src, 4);
    memcpy(body + 4, src + 5, 2);
    memcpy(body + 6, src + 8, 2);
    memcpy(body + 8, src + 11, 2);
    memcpy(body + 10, src + 14, 2);
    memcpy(body + 12, src + 17, 2);
    body[14] = '\0';
    if (n == 20) {
        if (src[19] != 'Z') {
            return -1;
        }
        memcpy(dst, "D:", 2);
        memcpy(dst + 2, body, 14);
        memcpy(dst + 16, "Z", 2);
        return 0;
    }
    int oh, om;
    if ((src[19] != '+' && src[19] != '-') || digits(src + 20, 2, &oh) || src[22] != ':' ||
        digits(src + 23, 2, &om) || oh > 23 || om > 59) {
        return -1;
    }
    /* D: + 14 digits + sign + HH'mm' = 24 bytes */
    memcpy(dst, "D:", 2);
    memcpy(dst + 2, body, 14);
    dst[16] = src[19];
    memcpy(dst + 17, src + 20, 2);
    dst[19] = '\'';
    memcpy(dst + 20, src + 23, 2);
    dst[22] = '\'';
    dst[23] = '\0';
    return 0;
}

int info_set(PdfInfo *info, const char *title, const char *producer, const char *creation_date, const char **why) {
    info_clear(info);
    if (title != NULL) {
        if (take_text(info->title, title, why, "metadata: title must be 1..1024 UTF-8 bytes",
                      "metadata: title is not valid UTF-8 text") != 0) {
            info_clear(info);
            return -1;
        }
        info->has_title = 1;
    }
    if (producer != NULL) {
        if (take_text(info->producer, producer, why, "metadata: producer must be 1..1024 UTF-8 bytes",
                      "metadata: producer is not valid UTF-8 text") != 0) {
            info_clear(info);
            return -1;
        }
        info->has_producer = 1;
    }
    if (creation_date != NULL) {
        if (take_date(info->date, creation_date) != 0) {
            *why = "metadata: creation date must be YYYY-MM-DDTHH:MM:SS with Z or +HH:MM/-HH:MM";
            info_clear(info);
            return -1;
        }
        info->has_date = 1;
    }
    return 0;
}

/* A hex string of UTF-16BE with a byte order mark: no character of the value
   can close the string or start PDF syntax. */
static int write_text(FILE *fp, const char *key, const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    if (fprintf(fp, "/%s <FEFF", key) < 0) {
        return -1;
    }
    const unsigned char *s = (const unsigned char *)text;
    size_t n = strlen(text);
    for (size_t i = 0; i < n;) {
        unsigned cp = 0;
        size_t k = utf8_scalar(s + i, n - i, &cp);
        if (k == 0) {
            return -1;
        }
        i += k;
        unsigned units[2];
        int nu = 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            units[0] = 0xD800 + (cp >> 10);
            units[1] = 0xDC00 + (cp & 0x3FF);
            nu = 2;
        } else {
            units[0] = cp;
        }
        for (int u = 0; u < nu; u++) {
            char out[4] = {hex[(units[u] >> 12) & 15], hex[(units[u] >> 8) & 15], hex[(units[u] >> 4) & 15],
                           hex[units[u] & 15]};
            if (fwrite(out, 1, 4, fp) != 4) {
                return -1;
            }
        }
    }
    return fputs(">\n", fp) < 0 ? -1 : 0;
}

int info_write(FILE *fp, const PdfInfo *info) {
    if (fputs("<<\n", fp) < 0) {
        return -1;
    }
    if (info->has_title && write_text(fp, "Title", info->title) != 0) {
        return -1;
    }
    if (info->has_producer && write_text(fp, "Producer", info->producer) != 0) {
        return -1;
    }
    if (info->has_date && fprintf(fp, "/CreationDate (%s)\n", info->date) < 0) {
        return -1;
    }
    return fputs(">>\n", fp) < 0 ? -1 : 0;
}
