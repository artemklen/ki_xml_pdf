#ifndef PAGE_SINK_H
#define PAGE_SINK_H

#include "xml_parse.h"

#include <stddef.h>

/* Called with one finished page. The bytes are valid only for the duration of
   the call; the sink clears them before returning. Return 0 on success. */
typedef int (*PageFlushFn)(const char *data, size_t len, void *user);

typedef struct PageSink {
    char *buf;
    size_t cap;
    size_t len;
    int col;
    int lines;
    int max_cols;
    int max_lines;
    int pages_flushed;
    PageFlushFn flush;
    void *user;
} PageSink;

int page_sink_init(PageSink *s, size_t cap, PageFlushFn flush, void *user);
/* Also flush when the next visual line would not fit. 0, 0 keeps byte cap only. */
void page_sink_set_columns(PageSink *s, int max_cols, int max_lines);
void page_sink_free(PageSink *s);
int page_sink_event(PageSink *s, const XmlEvent *ev);
int page_sink_finish(PageSink *s);
size_t page_sink_pending(const PageSink *s);
const char *page_sink_pending_data(const PageSink *s);

#endif
