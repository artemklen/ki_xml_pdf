#include "platform.h"
#include "layout.h"

#include "font.h"

#include <stdlib.h>
#include <string.h>

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

/* Lines stay in page[] until the page is full: one content stream is deflated
   from them in order, and the first-line shift is known only while that walk
   runs. The array is fixed, not allocated per page. */
static int flush_page(Layout *lay) {
    if (lay->nlines == 0) {
        return 0;
    }
    if (pdf_page_begin(lay->pdf) != 0) {
        return -1;
    }
    for (int i = 0; i < lay->nlines; i++) {
        if (pdf_page_line(lay->pdf, lay->page[i], lay->plen[i]) != 0) {
            lay->nlines = 0;
            return -1;
        }
    }
    int rc = pdf_page_end(lay->pdf);
    lay->nlines = 0;
    return rc;
}

/* Moves line[0, len) to the page and the pending word to the line start. */
static int commit_line(Layout *lay) {
    int max_lines = lay->pdf->frame.max_lines;
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
    if (lay->len >= LAYOUT_LINE || lay->len + lay->wlen >= LAYOUT_LINE) {
        return -1;
    }
    memcpy(lay->page[lay->nlines], lay->line, lay->len);
    lay->plen[lay->nlines] = lay->len;
    lay->nlines++;
    memmove(lay->line, lay->line + lay->len, lay->wlen);
    lay->len = 0;
    run_reset(&lay->run);
    return 0;
}

/* Measures the pending word again after a cut moved it to a line start. */
static int word_rescan(Layout *lay) {
    const FontFace *face = &lay->pdf->font;
    const char *s = lay->line + lay->len;
    size_t i = 0;
    run_reset(&lay->wrun);
    run_reset(&lay->wbase_run);
    lay->wbase = 0;
    while (i < lay->wlen) {
        size_t at = i;
        unsigned cp = 0;
        size_t clen = 0;
        uint16_t gid = 0;
        uint16_t adv = 0;
        if (utf8_next(s, lay->wlen, &i, &cp, &clen) <= 0 || font_lookup(face, cp, &gid, &adv) != 0) {
            return -1;
        }
        if (adv > 0) {
            lay->wbase = at;
            lay->wbase_run = lay->wrun;
        }
        run_add(&lay->wrun, face, cp, adv);
    }
    return 0;
}

/* Commits the first k bytes (extent k_run) of the pending word as one full
   line; the rest stays pending. The line must already be empty. */
static int commit_word(Layout *lay, size_t k, const InkRun *k_run) {
    lay->run = *k_run;
    lay->len = k;
    lay->wlen -= k;
    if (commit_line(lay) != 0) {
        return -1;
    }
    return word_rescan(lay);
}

/* Puts the pending word on the current line, or on a new one when it does
   not fit after what is already there. */
static int place_word(Layout *lay) {
    if (lay->wlen == 0) {
        return 0;
    }
    if (lay->len > 0) {
        InkRun joined = lay->run;
        run_join(&joined, &lay->wrun);
        if (!geom_fits(&lay->pdf->frame, &joined) && commit_line(lay) != 0) {
            return -1;
        }
    }
    run_join(&lay->run, &lay->wrun);
    lay->len += lay->wlen;
    lay->wlen = 0;
    run_reset(&lay->wrun);
    lay->wbase = 0;
    run_reset(&lay->wbase_run);
    return 0;
}

/* Appends one code point to the pending word. A word whose advance box or
   ink would cross the margins, or longer than LAYOUT_LINE - 1 bytes, is cut
   after anything earlier on the line has been committed: before this code
   point when it has an advance, otherwise before the base it would attach
   to. */
static int word_append(Layout *lay, const char *b, size_t clen, unsigned cp, unsigned adv) {
    const PageFrame *f = &lay->pdf->frame;
    InkRun glyph;
    run_reset(&glyph);
    run_add(&glyph, &lay->pdf->font, cp, adv);
    InkRun next = lay->wrun;
    run_join(&next, &glyph);
    if (!geom_fits(f, &next)) {
        if (lay->len > 0 && commit_line(lay) != 0) {
            return -1;
        }
        size_t k = adv > 0 ? lay->wlen : lay->wbase;
        InkRun k_run = adv > 0 ? lay->wrun : lay->wbase_run;
        if (k > 0 && commit_word(lay, k, &k_run) != 0) {
            return -1;
        }
    }
    if (lay->len + lay->wlen + clen >= LAYOUT_LINE) {
        if (lay->len > 0 && commit_line(lay) != 0) {
            return -1;
        }
        if (lay->wlen + clen >= LAYOUT_LINE) {
            size_t k = adv > 0 ? lay->wlen : lay->wbase;
            InkRun k_run = adv > 0 ? lay->wrun : lay->wbase_run;
            if (k == 0) {
                lay->line_full = 1;
                return -1;
            }
            if (commit_word(lay, k, &k_run) != 0) {
                return -1;
            }
        }
    }
    next = lay->wrun;
    run_join(&next, &glyph);
    if (!geom_fits(f, &next)) {
        lay->too_wide = 1;
        return -1;
    }
    if (adv > 0) {
        lay->wbase = lay->wlen;
        lay->wbase_run = lay->wrun;
    }
    memcpy(lay->line + lay->len + lay->wlen, b, clen);
    lay->wlen += clen;
    lay->wrun = next;
    return 0;
}

/* Space and tab end the pending word and stay on its line even past the
   width: they have no ink, and the next word is measured after them. */
static int space_append(Layout *lay, const char *b, size_t clen, unsigned adv) {
    if (place_word(lay) != 0) {
        return -1;
    }
    if (lay->len + clen >= LAYOUT_LINE && commit_line(lay) != 0) {
        return -1;
    }
    memcpy(lay->line + lay->len, b, clen);
    lay->len += clen;
    run_add(&lay->run, &lay->pdf->font, ' ', adv);
    return 0;
}

int layout_init(Layout *lay, PdfWriter *pdf) {
    memset(lay, 0, sizeof *lay);
    lay->pdf = pdf;
    return 0;
}

int layout_line_empty(const Layout *lay) {
    return lay->len == 0 && lay->wlen == 0 && lay->hold_n == 0;
}

int layout_break(Layout *lay) {
    if (place_word(lay) != 0) {
        return -1;
    }
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
    lay->too_wide = 0;
    size_t i = 0;
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
            if (place_word(lay) != 0 || commit_line(lay) != 0) {
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
        const char *b = chunk + (at - clen);
        rc = look == ' ' ? space_append(lay, b, clen, adv) : word_append(lay, b, clen, look, adv);
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

int layout_finish(Layout *lay) {
    if (lay->hold_n != 0) {
        return -1;
    }
    if (place_word(lay) != 0) {
        return -1;
    }
    if (lay->len > 0 && commit_line(lay) != 0) {
        return -1;
    }
    return flush_page(lay);
}
