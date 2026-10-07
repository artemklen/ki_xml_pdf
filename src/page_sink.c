#include "page_sink.h"

#include <stdlib.h>
#include <string.h>

int page_sink_init(PageSink *s, size_t cap, PageFlushFn flush, void *user) {
    memset(s, 0, sizeof *s);
    if (cap == 0 || flush == NULL) {
        return -1;
    }
    s->buf = malloc(cap);
    if (s->buf == NULL) {
        return -1;
    }
    memset(s->buf, 0, cap);
    s->cap = cap;
    s->flush = flush;
    s->user = user;
    return 0;
}

void page_sink_free(PageSink *s) {
    free(s->buf);
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
}

size_t page_sink_pending(const PageSink *s) {
    return s->len;
}

const char *page_sink_pending_data(const PageSink *s) {
    return s->buf;
}

static int flush_page(PageSink *s) {
    if (s->len == 0) {
        return 0;
    }
    size_t n = s->len;
    int rc = s->flush(s->buf, n, s->user);
    memset(s->buf, 0, n);
    s->len = 0;
    s->col = 0;
    s->lines = 0;
    if (rc != 0) {
        return -1;
    }
    s->pages_flushed++;
    return 0;
}

void page_sink_set_columns(PageSink *s, int max_cols, int max_lines) {
    if (max_cols > 0 && max_lines > 0) {
        s->max_cols = max_cols;
        s->max_lines = max_lines;
    }
}

/* A piece that fits in an empty page is kept intact: if it does not fit in
   the bytes still free, the current page is flushed first. A piece larger
   than the page is split, because a single page cannot hold it. */
static int append_raw(PageSink *s, const char *data, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (n <= s->cap) {
        if (s->len > 0 && s->len + n > s->cap) {
            if (flush_page(s) != 0) {
                return -1;
            }
        }
        memcpy(s->buf + s->len, data, n);
        s->len += n;
        return 0;
    }
    size_t off = 0;
    while (off < n) {
        if (s->len == s->cap) {
            if (flush_page(s) != 0) {
                return -1;
            }
        }
        size_t room = s->cap - s->len;
        size_t take = n - off;
        if (take > room) {
            take = room;
        }
        memcpy(s->buf + s->len, data + off, take);
        s->len += take;
        off += take;
    }
    return 0;
}

static int break_line(PageSink *s) {
    if (s->len + 1 > s->cap) {
        if (flush_page(s) != 0) {
            return -1;
        }
    }
    s->buf[s->len++] = '\n';
    s->col = 0;
    s->lines++;
    if (s->lines >= s->max_lines) {
        if (flush_page(s) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Short runs stay on one line. A run longer than the line is split, because
   it cannot fit even on an empty line. */
static int append_wrapped(PageSink *s, const char *data, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (data[i] == '\n') {
            if (break_line(s) != 0) {
                return -1;
            }
            i++;
            continue;
        }
        size_t run = 0;
        while (i + run < n && data[i + run] != '\n') {
            run++;
        }
        while (run > 0) {
            size_t room = (size_t)(s->max_cols - s->col);
            if (s->col > 0 && run <= (size_t)s->max_cols && run > room) {
                if (break_line(s) != 0) {
                    return -1;
                }
                continue;
            }
            if (room == 0) {
                if (break_line(s) != 0) {
                    return -1;
                }
                continue;
            }
            size_t take = run < room ? run : room;
            if (s->len + take > s->cap) {
                if (s->len == 0) {
                    return -1;
                }
                if (flush_page(s) != 0) {
                    return -1;
                }
                continue;
            }
            memcpy(s->buf + s->len, data + i, take);
            s->len += take;
            s->col += (int)take;
            i += take;
            run -= take;
        }
    }
    return 0;
}

static int page_sink_append(PageSink *s, const char *data, size_t n) {
    if (s->max_cols > 0) {
        return append_wrapped(s, data, n);
    }
    return append_raw(s, data, n);
}

static int app_str(PageSink *s, const char *z) {
    return page_sink_append(s, z, strlen(z));
}

int page_sink_event(PageSink *s, const XmlEvent *ev) {
    if (ev->kind == XML_EV_START) {
        if (app_str(s, "<") != 0 || app_str(s, ev->name) != 0) {
            return -1;
        }
        for (int i = 0; i < ev->nattr; i++) {
            if (app_str(s, " ") != 0 || app_str(s, ev->attr_name[i]) != 0 ||
                app_str(s, "=\"") != 0 || app_str(s, ev->attr_val[i]) != 0 ||
                app_str(s, "\"") != 0) {
                return -1;
            }
        }
        if (app_str(s, ">\n") != 0) {
            return -1;
        }
        return 0;
    }
    if (ev->kind == XML_EV_END) {
        if (app_str(s, "</") != 0 || app_str(s, ev->name) != 0 || app_str(s, ">\n") != 0) {
            return -1;
        }
        return 0;
    }
    if (ev->kind == XML_EV_TEXT) {
        return page_sink_append(s, ev->text, ev->text_len);
    }
    return 0;
}

int page_sink_finish(PageSink *s) {
    return flush_page(s);
}
