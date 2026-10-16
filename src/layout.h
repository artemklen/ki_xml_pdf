#ifndef LAYOUT_H
#define LAYOUT_H

#include "pdf_write.h"

#include <stddef.h>

#define LAYOUT_LINE PDF_LINE_BYTES
#define LAYOUT_LINES 50

typedef struct Layout {
    PdfWriter *pdf;
    char line[LAYOUT_LINE];
    size_t len;
    long units;
    char page[LAYOUT_LINES][LAYOUT_LINE];
    size_t plen[LAYOUT_LINES];
    int nlines;
    unsigned char hold[4];
    int hold_n;
    unsigned bad_cp;
    /* Set when a line would exceed LAYOUT_LINE - 1 bytes without reaching
       the width limit, e.g. a long run of zero-advance marks. */
    int line_full;
} Layout;

int layout_init(Layout *lay, PdfWriter *pdf);
int layout_text(Layout *lay, const char *s, size_t n);
int layout_break(Layout *lay);
int layout_finish(Layout *lay);

#endif
