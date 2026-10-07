#ifndef CONVERT_H
#define CONVERT_H

#include <stddef.h>

/* One visual page: lines that fit a letter page in Helvetica 10pt, each line
   short enough to stay inside the media box. The byte cap is exactly that
   grid, so a finished page is appended and cleared instead of growing. */
#define XML2PDF_PAGE_COLS 90
#define XML2PDF_PAGE_LINES 48
#define XML2PDF_PAGE_CAP (XML2PDF_PAGE_LINES * (XML2PDF_PAGE_COLS + 1))

#define XML2PDF_OK 0
#define XML2PDF_ERR_XML 1
#define XML2PDF_ERR_USAGE 2
#define XML2PDF_ERR_IO 3
#define XML2PDF_ERR_RESOURCE 4

int xml2pdf_convert_cap(const char *xml_path, const char *pdf_path, size_t page_cap);
int xml2pdf_convert(const char *xml_path, const char *pdf_path);

/* Test hooks. Zero turns the hook off. Cleared when a conversion returns. */
void xml2pdf_debug_fail_at_page(int n);
void xml2pdf_debug_fail_before_publish(int on);
void xml2pdf_debug_fail_after_bytes(long n);

#endif
