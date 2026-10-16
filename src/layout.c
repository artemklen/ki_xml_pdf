#include "layout.h"

#include "font.h"

#include <stdlib.h>
#include <string.h>

static long line_limit(const FontFace *face) {
    return (long)(612 - 80) * face->units / 10;
}

static int utf8_next(const char *s, size_t n, size_t *i, unsigned *cp, size_t *clen) {
    if (*i >= n) {
        return 0;
    }
    size_t start = *i;
    unsigned char c = (unsigned char)s[*i];
    int need = 0;
    unsigned min = 0;
    if (c < 0x80) {
        *cp = c;
        *i += 1;
        *clen = 1;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        need = 1;
        min = 0x80;
        *cp = c & 0x1F;
        if (c < 0xC2) {
            return -1;
        }
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        min = 0x800;
        *cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        min = 0x10000;
        *cp = c & 0x07;
        if (c > 0xF4) {
            return -1;
        }
    } else {
        return -1;
    }
    if (*i + 1 + (size_t)need > n) {
        return -1;
    }
    for (int k = 0; k < need; k++) {
        unsigned char d = (unsigned char)s[*i + 1 + (size_t)k];
        if ((d & 0xC0) != 0x80) {
            return -1;
        }
        *cp = (*cp << 6) | (d & 0x3F);
    }
    if (*cp < min || *cp > 0x10FFFF) {
        return -1;
    }
    *i += 1 + (size_t)need;
    *clen = *i - start;
    return 1;
}

static int flush_page(Layout *lay) {
    if (lay->nlines == 0) {
        return 0;
    }
    size_t cap = (size_t)lay->nlines * (LAYOUT_LINE + 1);
    char *buf = malloc(cap);
    if (buf == NULL) {
        return -1;
    }
    size_t o = 0;
    for (int i = 0; i < lay->nlines; i++) {
        memcpy(buf + o, lay->page[i], lay->plen[i]);
        o += lay->plen[i];
        buf[o++] = '\n';
    }
    int rc = pdf_add_page(lay->pdf, buf, o);
    free(buf);
    lay->nlines = 0;
    return rc;
}

static int commit_line(Layout *lay) {
    int max_lines = font_max_lines(&lay->pdf->font, 792, 40, 10, 14);
    if (max_lines > LAYOUT_LINES) {
        max_lines = LAYOUT_LINES;
    }
    if (max_lines < 1) {
        max_lines = 1;
    }
    if (lay->nlines >= max_lines) {
        if (flush_page(lay) != 0) {
            return -1;
        }
    }
    if (lay->len >= LAYOUT_LINE) {
        return -1;
    }
    memcpy(lay->page[lay->nlines], lay->line, lay->len);
    lay->plen[lay->nlines] = lay->len;
    lay->nlines++;
    lay->len = 0;
    lay->units = 0;
    return 0;
}

int layout_init(Layout *lay, PdfWriter *pdf) {
    memset(lay, 0, sizeof *lay);
    lay->pdf = pdf;
    return 0;
}

int layout_break(Layout *lay) {
    if (lay->len == 0) {
        return 0;
    }
    return commit_line(lay);
}

static int seq_need(unsigned char c) {
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
        return 4;
    }
    return -1;
}

int layout_text(Layout *lay, const char *s, size_t n) {
    lay->bad_cp = 0;
    lay->line_full = 0;
    size_t i = 0;
    long limit = line_limit(&lay->pdf->font);
    while (lay->hold_n > 0 || i < n) {
        if (lay->hold_n < 4 && i < n) {
            int need = lay->hold_n == 0 ? seq_need((unsigned char)s[i]) : seq_need(lay->hold[0]);
            if (need < 0) {
                return -1;
            }
            if (lay->hold_n < need) {
                lay->hold[lay->hold_n++] = (unsigned char)s[i++];
                if (lay->hold_n < need && i >= n) {
                    return 0;
                }
                if (lay->hold_n < need) {
                    continue;
                }
            }
        }
        const char *chunk;
        size_t chunk_n;
        char scratch[4];
        if (lay->hold_n > 0) {
            memcpy(scratch, lay->hold, (size_t)lay->hold_n);
            chunk = scratch;
            chunk_n = (size_t)lay->hold_n;
        } else {
            chunk = s + i;
            chunk_n = n - i;
        }
        size_t at = 0;
        unsigned cp = 0;
        size_t clen = 0;
        int rc = utf8_next(chunk, chunk_n, &at, &cp, &clen);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }
        if (lay->hold_n > 0) {
            if ((size_t)lay->hold_n < clen) {
                return -1;
            }
            memmove(lay->hold, lay->hold + clen, (size_t)lay->hold_n - clen);
            lay->hold_n -= (int)clen;
        } else {
            i += clen;
        }
        if (cp == '\n' || cp == '\r') {
            if (commit_line(lay) != 0) {
                return -1;
            }
            continue;
        }
        unsigned look = cp == '\t' ? ' ' : cp;
        uint16_t gid = 0;
        uint16_t adv = 0;
        if (font_lookup(&lay->pdf->font, look, &gid, &adv) != 0) {
            lay->bad_cp = look;
            return -1;
        }
        if (lay->units > 0 && lay->units + (long)adv > limit) {
            if (commit_line(lay) != 0) {
                return -1;
            }
        }
        if ((long)adv > limit) {
            return -1;
        }
        if (lay->len + clen >= LAYOUT_LINE) {
            lay->line_full = 1;
            return -1;
        }
        memcpy(lay->line + lay->len, chunk + (at - clen), clen);
        lay->len += clen;
        lay->units += adv;
    }
    return 0;
}

int layout_finish(Layout *lay) {
    if (lay->hold_n != 0) {
        return -1;
    }
    if (lay->len > 0 && commit_line(lay) != 0) {
        return -1;
    }
    return flush_page(lay);
}
