#ifndef INFO_H
#define INFO_H

#include "ki_xml_pdf.h"

#include <stdio.h>

/* Optional document information dictionary. Values are validated and copied
   before any I/O, so the writer never sees caller memory that could change
   during the conversion. An empty PdfInfo writes no /Info at all. */

typedef struct PdfInfo {
    int has_title;
    int has_producer;
    int has_date;
    char title[KI_INFO_TEXT_MAX + 1];
    char producer[KI_INFO_TEXT_MAX + 1];
    /* PDF date string body, e.g. D:20261016120000+03'00' */
    char date[32];
} PdfInfo;

void info_clear(PdfInfo *info);
/* NULL fields are omitted. On failure returns -1 with *why, info is empty. */
int info_set(PdfInfo *info, const char *title, const char *producer, const char *creation_date, const char **why);
int info_present(const PdfInfo *info);
/* Writes the dictionary "<< ... >>" for a non-empty info. */
int info_write(FILE *fp, const PdfInfo *info);

#endif
