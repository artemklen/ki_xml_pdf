#ifndef LAYOUT_H
#define LAYOUT_H

#include "pdf_write.h"

#include <stddef.h>

#define LAYOUT_LINE PDF_LINE_BYTES
#define LAYOUT_LINES GEOM_MAX_LINES

/* line[0, len) is placed on the current output line and line[len, len +
   wlen) is the word still being collected, so the pending word needs no
   storage of its own and never exceeds LAYOUT_LINE - 1 bytes with it. */
typedef struct Layout {
    PdfWriter *pdf;
    char line[LAYOUT_LINE];
    size_t len;
    InkRun run;
    size_t wlen;
    InkRun wrun;
    /* Start of the word's last positive-advance code point and the word's
       extent before it: the latest split that keeps marks with their base. */
    size_t wbase;
    InkRun wbase_run;
    char page[LAYOUT_LINES][LAYOUT_LINE];
    size_t plen[LAYOUT_LINES];
    int nlines;
    unsigned char hold[4];
    int hold_n;
    unsigned bad_cp;
    /* Set when a word would exceed LAYOUT_LINE - 1 bytes and the next code
       point has zero advance, so no split can keep a mark with its base. */
    int line_full;
    /* Set when a base and its zero-advance marks do not fit one line. */
    int too_wide;
} Layout;

int layout_init(Layout *lay, PdfWriter *pdf);
/* True when nothing, not even a pending word, is on the current line. */
int layout_line_empty(const Layout *lay);
int layout_text(Layout *lay, const char *s, size_t n);
int layout_break(Layout *lay);
int layout_finish(Layout *lay);

#endif
