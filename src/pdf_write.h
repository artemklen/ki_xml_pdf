#ifndef PDF_WRITE_H
#define PDF_WRITE_H

#include "font.h"

#include <stddef.h>
#include <stdio.h>

/* Append-only PDF. Each page is written and dropped before pdf_add_page
   returns. Object offsets live in a temporary file, not in a list that grows
   in RAM. Page object numbers are a fixed function of the page index. */

typedef struct PdfConfig {
    const char *font_path;
    const char *temp_dir;
    int max_pages;
    long max_output;
    long max_spool;
    int debug_fail_page;
    int (*cancelled)(void *user);
    void *cancel_user;
} PdfConfig;

typedef struct PdfWriter {
    FILE *fp;
    FILE *raw;
    FILE *offsets;
    char *off_path;
    FontFace font;
    int nobj;
    int npages;
    int hit_limit;
    int fail_kind;
    int fail_errno;
    int max_pages;
    long max_output;
    long max_spool;
    long written;
    long spool_bytes;
    int debug_fail_page;
    int debug_pages;
    int (*cancelled)(void *user);
    void *cancel_user;
    size_t live_page_bytes;
} PdfWriter;

int pdf_begin(PdfWriter *w, FILE *fp);
int pdf_begin_cfg(PdfWriter *w, FILE *fp, const PdfConfig *cfg);
int pdf_add_page(PdfWriter *w, const char *text, size_t len);
/* 0 disables. n > 0 makes the nth pdf_add_page fail before writing. */
void pdf_debug_fail_at_page(int n);
int pdf_end(PdfWriter *w);
void pdf_free(PdfWriter *w);
size_t pdf_live_page_bytes(const PdfWriter *w);
int pdf_page_count(const PdfWriter *w);
int pdf_object_count(const PdfWriter *w);
int pdf_hit_object_limit(const PdfWriter *w);

#endif
