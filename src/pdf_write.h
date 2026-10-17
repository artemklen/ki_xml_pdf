#ifndef PDF_WRITE_H
#define PDF_WRITE_H

#include "font.h"
#include "geometry.h"
#include "info.h"

#include <stddef.h>
#include <stdio.h>

/* Append-only PDF. Each page is written and dropped before pdf_add_page
   returns. Object offsets live in a temporary file, not in a list that grows
   in RAM. Page object numbers are a fixed function of the page index. */

/* Line buffer size shared with layout: a line holds at most
   PDF_LINE_BYTES - 1 UTF-8 bytes. */
#define PDF_LINE_BYTES 1024

/* A classic xref entry has a ten-digit offset field. Every emitted byte is
   counted against this ceiling, even when the user output quota is 0, so no
   object offset or startxref value can need an eleventh digit. */
#define PDF_XREF_MAX_OFFSET 9999999999LL

/* fail_kind values. */
#define PDF_FAIL_LIMIT 1 /* object, page or allocation limit */
#define PDF_FAIL_IO 2
#define PDF_FAIL_FONT 3
#define PDF_FAIL_QUOTA 4
#define PDF_FAIL_SPOOL 5
#define PDF_FAIL_CANCEL 6
#define PDF_FAIL_XREF 7
#define PDF_FAIL_FLATE 8 /* zlib reported an error other than out of memory */
#define PDF_FAIL_GEOMETRY 9 /* no line or glyph of the font fits the page */

typedef struct PdfConfig {
    const char *font_path;
    const char *temp_dir;
    int max_pages;
    long max_output;
    long max_spool;
    int debug_fail_page;
    int (*cancelled)(void *user);
    void *cancel_user;
    /* NULL is the default Letter geometry. */
    const Geometry *geometry;
    /* NULL or empty writes no /Info; must outlive the writer. */
    const PdfInfo *info;
} PdfConfig;

struct z_stream_s;

typedef struct PdfWriter {
    FILE *fp;
    FILE *raw;
    FILE *offsets;
    char *off_path;
    FontFace font;
    /* Fitted after the font loads and before the header is written. */
    PageFrame frame;
    const PdfInfo *info;
    int nobj;
    int npages;
    int hit_limit;
    int fail_kind;
    int fail_errno;
    int max_pages;
    long max_output;
    long max_spool;
    /* Bytes accepted by the destination stream; never above pdf_byte_ceiling(). */
    long written;
    long spool_bytes;
    int debug_fail_page;
    int debug_pages;
    int (*cancelled)(void *user);
    void *cancel_user;
    size_t live_page_bytes;
    /* One deflate state and output window, reused by every Flate stream. */
    struct z_stream_s *zs;
    unsigned char *zbuf;
    /* One content-stream buffer for every page of this conversion. */
    char *page_buf;
    size_t page_cap;
    size_t page_o;
    int page_lines;
    int page_any;
    long long page_prev_shift;
} PdfWriter;

int pdf_begin(PdfWriter *w, FILE *fp);
int pdf_begin_cfg(PdfWriter *w, FILE *fp, const PdfConfig *cfg);
int pdf_page_begin(PdfWriter *w);
int pdf_page_line(PdfWriter *w, const char *text, size_t len);
int pdf_page_end(PdfWriter *w);
int pdf_add_page(PdfWriter *w, const char *text, size_t len);
/* How many times a conversion allocated its page-content buffer. */
int pdf_page_content_allocs(void);
/* 0 disables. n > 0 makes the nth pdf_add_page fail before writing. */
void pdf_debug_fail_at_page(int n);
int pdf_end(PdfWriter *w);
void pdf_free(PdfWriter *w);
size_t pdf_live_page_bytes(const PdfWriter *w);
int pdf_page_count(const PdfWriter *w);
int pdf_object_count(const PdfWriter *w);
int pdf_hit_object_limit(const PdfWriter *w);
/* Hard total-output ceiling: PDF_XREF_MAX_OFFSET, or LONG_MAX if smaller. */
long pdf_byte_ceiling(void);

#endif
