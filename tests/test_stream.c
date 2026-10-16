#define _XOPEN_SOURCE 700

#include "convert.h"
#include "font.h"
#include "ki_xml_pdf.h"
#include "page_sink.h"
#include "pdf_write.h"
#include "xml_parse.h"

#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#if defined(__SANITIZE_ADDRESS__)
#define KI_RSS_GATE 0
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define KI_RSS_GATE 0
#    endif
#endif
#ifndef KI_RSS_GATE
#define KI_RSS_GATE 1
#endif

static int failures = 0;
static volatile sig_atomic_t test_stop;

static void test_on_term(int sig) {
    (void)sig;
    test_stop = 1;
}

static int test_cancelled(void *user) {
    (void)user;
    return test_stop != 0;
}

/* 0 idle, 1 armed, 2 fsync has returned. Only the armed test cancels. */
static volatile sig_atomic_t arm_fsync_cancel;

int __real_fsync(int fd);
int __wrap_fsync(int fd) {
    int rc = __real_fsync(fd);
    if (arm_fsync_cancel == 1) {
        arm_fsync_cancel = 2;
    }
    return rc;
}

/* Compressor fault injection, linked with --wrap. deflate_fail_in = n makes
   the nth following deflate() call return deflate_fail_rc without running. */
static int deflate_fail_in;
static int deflate_fail_rc;
static int deflate_init_fail_rc;
static int deflate_calls;

int __real_deflate(z_streamp strm, int flush);
int __wrap_deflate(z_streamp strm, int flush) {
    deflate_calls++;
    if (deflate_fail_in > 0 && --deflate_fail_in == 0) {
        return deflate_fail_rc;
    }
    return __real_deflate(strm, flush);
}

int __real_deflateInit2_(z_streamp strm, int level, int method, int bits, int mem, int strategy, const char *ver,
                         int size);
int __wrap_deflateInit2_(z_streamp strm, int level, int method, int bits, int mem, int strategy, const char *ver,
                         int size) {
    if (deflate_init_fail_rc != 0) {
        return deflate_init_fail_rc;
    }
    return __real_deflateInit2_(strm, level, method, bits, mem, strategy, ver, size);
}

static int cancel_after_fsync(void *user) {
    (void)user;
    return arm_fsync_cancel == 2;
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    size_t limit;
    size_t cuts[8];
    int ncuts;
    int cuti;
} ChunkIn;

static int chunk_read(void *user, char *dst, int max) {
    ChunkIn *c = user;
    if (max <= 0 || c->i >= c->n) {
        return 0;
    }
    size_t allow = c->n - c->i;
    if (c->limit > 0 && allow > c->limit) {
        allow = c->limit;
    }
    if (c->cuti < c->ncuts && c->i < c->cuts[c->cuti]) {
        size_t until = c->cuts[c->cuti] - c->i;
        if (until < allow) {
            allow = until;
        }
    }
    if (allow > (size_t)max) {
        allow = (size_t)max;
    }
    if (allow == 0) {
        if (c->cuti < c->ncuts && c->i >= c->cuts[c->cuti]) {
            c->cuti++;
            return chunk_read(user, dst, max);
        }
        return 0;
    }
    memcpy(dst, c->s + c->i, allow);
    c->i += allow;
    if (c->cuti < c->ncuts && c->i >= c->cuts[c->cuti]) {
        c->cuti++;
    }
    return (int)allow;
}

typedef struct {
    XmlEventKind kind;
    char name[XML_NAME_MAX];
    int nattr;
    char attr_name[XML_ATTR_MAX][XML_NAME_MAX];
    char attr_val[XML_ATTR_MAX][XML_ATTR_VAL_MAX];
    char text[XML_TEXT_MAX];
    size_t text_len;
} Rec;

static int parse_recs(XmlReadFn fn, void *user, Rec *recs, int max, int *nout, char *err, size_t errcap) {
    XmlParser p;
    CHECK(xml_parser_init(&p, fn, user) == 0);
    int n = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            snprintf(err, errcap, "%s", ev.error);
            return -1;
        }
        if (k == XML_EV_EOF) {
            CHECK(p.depth == 0);
            break;
        }
        if (n >= max) {
            snprintf(err, errcap, "too many events");
            return -1;
        }
        Rec *r = &recs[n++];
        memset(r, 0, sizeof *r);
        r->kind = ev.kind;
        memcpy(r->name, ev.name, sizeof r->name);
        r->nattr = ev.nattr;
        for (int i = 0; i < ev.nattr && i < XML_ATTR_MAX; i++) {
            memcpy(r->attr_name[i], ev.attr_name[i], XML_NAME_MAX);
            memcpy(r->attr_val[i], ev.attr_val[i], XML_ATTR_VAL_MAX);
        }
        r->text_len = ev.text_len;
        memcpy(r->text, ev.text, ev.text_len);
        r->text[ev.text_len] = '\0';
    }
    *nout = n;
    return 0;
}

static void concat_text(const Rec *recs, int n, int *i, char *out, size_t cap) {
    size_t o = 0;
    while (*i < n && recs[*i].kind == XML_EV_TEXT) {
        size_t L = recs[*i].text_len;
        if (o + L >= cap) {
            L = cap - 1 - o;
        }
        memcpy(out + o, recs[*i].text, L);
        o += L;
        (*i)++;
    }
    out[o] = '\0';
}

static int expect_start(const Rec *r, const char *name, const char *an, const char *av) {
    if (r->kind != XML_EV_START) {
        return 0;
    }
    if (strcmp(r->name, name) != 0) {
        return 0;
    }
    if (an == NULL) {
        return r->nattr == 0;
    }
    if (r->nattr != 1) {
        return 0;
    }
    return strcmp(r->attr_name[0], an) == 0 && strcmp(r->attr_val[0], av) == 0;
}

static int test_split_mid_tag_and_text(void) {
    const char *xml = "<root id=\"a&amp;b\"><kid>12&lt;34</kid></root>";
    const char *kid = strstr(xml, "<kid>");
    const char *ent = strstr(xml, "&lt;");
    CHECK(kid != NULL && ent != NULL);
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    /* First read stops inside the tag name "<kid"; second stops inside "&lt;". */
    in.cuts[0] = (size_t)(kid - xml) + 2;
    in.cuts[1] = (size_t)(ent - xml) + 2;
    in.ncuts = 2;
    CHECK(in.cuts[0] < in.cuts[1]);
    CHECK(in.cuts[1] < in.n);

    Rec recs[16];
    int n = 0;
    char err[160];
    int pr = parse_recs(chunk_read, &in, recs, 16, &n, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    CHECK(in.i == in.n);
    int i = 0;
    CHECK(i < n && expect_start(&recs[i], "root", "id", "a&b"));
    i++;
    CHECK(i < n && expect_start(&recs[i], "kid", NULL, NULL));
    i++;
    char text[64];
    concat_text(recs, n, &i, text, sizeof text);
    CHECK(strcmp(text, "12<34") == 0);
    CHECK(strstr(text, "&lt;") == NULL);
    CHECK(i < n && recs[i].kind == XML_EV_END && strcmp(recs[i].name, "kid") == 0);
    i++;
    CHECK(i < n && recs[i].kind == XML_EV_END && strcmp(recs[i].name, "root") == 0);
    i++;
    CHECK(i == n);
    return 0;
}

static int test_one_byte_entities_and_nesting(void) {
    const char *xml =
        "<a><b><c a=\"&quot;&apos;\">&amp;&lt;&gt;</c></b></a>";
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    in.limit = 1;

    Rec recs[16];
    int n = 0;
    char err[160];
    int pr = parse_recs(chunk_read, &in, recs, 16, &n, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    int i = 0;
    CHECK(expect_start(&recs[i++], "a", NULL, NULL));
    CHECK(expect_start(&recs[i++], "b", NULL, NULL));
    CHECK(expect_start(&recs[i], "c", "a", "\"'"));
    i++;
    char text[64];
    concat_text(recs, n, &i, text, sizeof text);
    CHECK(strcmp(text, "&<>") == 0);
    CHECK(recs[i].kind == XML_EV_END && strcmp(recs[i].name, "c") == 0);
    i++;
    CHECK(recs[i].kind == XML_EV_END && strcmp(recs[i].name, "b") == 0);
    i++;
    CHECK(recs[i].kind == XML_EV_END && strcmp(recs[i].name, "a") == 0);
    i++;
    CHECK(i == n);
    return 0;
}

static int test_skips_and_errors(void) {
    const char *xml =
        "<?xml version=\"1.0\"?><!DOCTYPE root SYSTEM \"missing.dtd\">"
        "<a>hi<!-- hidden -->there</a>";
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    in.limit = 3;
    Rec recs[16];
    int n = 0;
    char err[160];
    int pr = parse_recs(chunk_read, &in, recs, 16, &n, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    int i = 0;
    CHECK(expect_start(&recs[i++], "a", NULL, NULL));
    char text[64];
    concat_text(recs, n, &i, text, sizeof text);
    CHECK(strcmp(text, "hithere") == 0);
    CHECK(strstr(text, "hidden") == NULL);
    CHECK(recs[i].kind == XML_EV_END && strcmp(recs[i].name, "a") == 0);

    const char *spaced = "<root  id = \"z\" ></root>";
    memset(&in, 0, sizeof in);
    in.s = spaced;
    in.n = strlen(spaced);
    in.limit = 2;
    pr = parse_recs(chunk_read, &in, recs, 16, &n, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    CHECK(n == 2);
    CHECK(expect_start(&recs[0], "root", "id", "z"));
    CHECK(recs[1].kind == XML_EV_END && strcmp(recs[1].name, "root") == 0);

    const char *bad = "<a></b>";
    memset(&in, 0, sizeof in);
    in.s = bad;
    in.n = strlen(bad);
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    int saw_err = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            saw_err = 1;
            CHECK(ev.error[0] != '\0');
            break;
        }
        CHECK(k != XML_EV_EOF);
    }
    CHECK(saw_err == 1);

    const char *open = "<a>";
    memset(&in, 0, sizeof in);
    in.s = open;
    in.n = strlen(open);
    xml_parser_init(&p, chunk_read, &in);
    saw_err = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            saw_err = 1;
            break;
        }
        if (k == XML_EV_EOF) {
            break;
        }
    }
    CHECK(saw_err == 1);

    const char *empty = "<br/>";
    memset(&in, 0, sizeof in);
    in.s = empty;
    in.n = strlen(empty);
    in.limit = 1;
    pr = parse_recs(chunk_read, &in, recs, 16, &n, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    CHECK(n == 2);
    CHECK(expect_start(&recs[0], "br", NULL, NULL));
    CHECK(recs[1].kind == XML_EV_END && strcmp(recs[1].name, "br") == 0);
    return 0;
}

typedef struct {
    unsigned long long budget;
    int phase;
    const char *cur;
    int cur_len;
    int cur_pos;
    unsigned long long produced;
    unsigned long long items;
} Gen;

static void gen_arm(Gen *g, const char *s) {
    g->cur = s;
    g->cur_len = (int)strlen(s);
    g->cur_pos = 0;
}

static int gen_read(void *user, char *dst, int max) {
    Gen *g = user;
    int w = 0;
    while (w < max) {
        if (g->cur != NULL && g->cur_pos < g->cur_len) {
            dst[w++] = g->cur[g->cur_pos++];
            g->produced++;
            continue;
        }
        g->cur = NULL;
        if (g->phase == 0) {
            gen_arm(g, "<root>");
            g->phase = 1;
            continue;
        }
        if (g->phase == 1) {
            if (g->budget < 7) {
                g->phase = 2;
                continue;
            }
            gen_arm(g, "<i>v</i>");
            g->budget -= 7;
            g->items++;
            continue;
        }
        if (g->phase == 2) {
            gen_arm(g, "</root>");
            g->phase = 3;
            continue;
        }
        break;
    }
    return w;
}

static int test_big_stream_fixed_memory(void) {
    XmlParser probe;
    CHECK(sizeof probe.buf == XML_BUF_SIZE);
    CHECK(sizeof probe.stack == XML_MAX_DEPTH * XML_NAME_MAX);
    CHECK(XML_MAX_DEPTH <= 64);
    CHECK(sizeof(XmlParser) < 64 * 1024);
    CHECK(XML_BUF_SIZE < 1024 * 1024);

    Gen g;
    memset(&g, 0, sizeof g);
    g.budget = 8ULL * 1024ULL * 1024ULL;

    struct rusage ru0, ru1;
    CHECK(getrusage(RUSAGE_SELF, &ru0) == 0);

    XmlParser p;
    CHECK(xml_parser_init(&p, gen_read, &g) == 0);
    unsigned long long i_start = 0, i_end = 0, i_text = 0;
    unsigned long long root_start = 0, root_end = 0;
    int depth = 0;
    int max_depth = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            fprintf(stderr, "big stream error: %s\n", ev.error);
            return 1;
        }
        if (k == XML_EV_EOF) {
            break;
        }
        if (k == XML_EV_START) {
            depth++;
            if (depth > max_depth) {
                max_depth = depth;
            }
            if (strcmp(ev.name, "i") == 0) {
                i_start++;
            } else if (strcmp(ev.name, "root") == 0) {
                root_start++;
            }
        } else if (k == XML_EV_END) {
            if (strcmp(ev.name, "i") == 0) {
                i_end++;
            } else if (strcmp(ev.name, "root") == 0) {
                root_end++;
            }
            depth--;
            CHECK(depth >= 0);
        } else if (k == XML_EV_TEXT) {
            if (ev.text_len == 1 && ev.text[0] == 'v') {
                i_text++;
            }
        }
    }
    CHECK(getrusage(RUSAGE_SELF, &ru1) == 0);
    long delta_kb = ru1.ru_maxrss - ru0.ru_maxrss;
    printf("big-stream bytes=%llu items=%llu parser_bytes=%zu rss_delta_kb=%ld max_depth=%d\n",
           g.produced, g.items, sizeof(XmlParser), delta_kb, max_depth);

    CHECK(g.produced > 8ULL * 1024ULL * 1024ULL);
    CHECK(g.produced > (unsigned long long)sizeof(XmlParser) * 50ULL);
    CHECK(g.items > 100000ULL);
    CHECK(i_start == g.items);
    CHECK(i_end == g.items);
    CHECK(i_text == g.items);
    CHECK(root_start == 1);
    CHECK(root_end == 1);
    CHECK(depth == 0);
    CHECK(p.depth == 0);
    CHECK(max_depth == 2);
    /* Peak RSS must not track the input. 2 MiB is far below the 8 MiB stream. */
    CHECK(delta_kb >= 0);
#if KI_RSS_GATE
    CHECK(delta_kb < 2048);
#endif
    return 0;
}

typedef struct {
    char *pages[8];
    size_t lens[8];
    int n;
    char *frozen;
    size_t frozen_len;
} PageLog;

static int capture_page(const char *data, size_t len, void *user) {
    PageLog *log = user;
    CHECK(log->n < 8);
    char *copy = malloc(len + 1);
    CHECK(copy != NULL);
    memcpy(copy, data, len);
    copy[len] = '\0';
    if (log->n == 0) {
        log->frozen = malloc(len + 1);
        CHECK(log->frozen != NULL);
        memcpy(log->frozen, data, len);
        log->frozen[len] = '\0';
        log->frozen_len = len;
    }
    log->pages[log->n] = copy;
    log->lens[log->n] = len;
    log->n++;
    return 0;
}

static void set_text(XmlEvent *ev, const char *t) {
    memset(ev, 0, sizeof *ev);
    ev->kind = XML_EV_TEXT;
    ev->text_len = strlen(t);
    memcpy(ev->text, t, ev->text_len);
}

static int test_page_sink_flushes(void) {
    PageLog log;
    memset(&log, 0, sizeof log);
    PageSink sink;
    CHECK(page_sink_init(&sink, 48, capture_page, &log) == 0);

    XmlEvent ev;
    set_text(&ev, "EARLY_MARKER");
    CHECK(page_sink_event(&sink, &ev) == 0);

    char xs[101];
    memset(xs, 'X', 100);
    xs[100] = '\0';
    set_text(&ev, xs);
    CHECK(page_sink_event(&sink, &ev) == 0);

    set_text(&ev, "LATE_MARKER");
    CHECK(page_sink_event(&sink, &ev) == 0);
    CHECK(page_sink_finish(&sink) == 0);

    CHECK(log.n >= 2);
    CHECK(log.pages[0] != NULL);
    CHECK(strstr(log.pages[0], "EARLY_MARKER") != NULL);
    CHECK(strstr(log.pages[0], "LATE_MARKER") == NULL);
    int late_page = -1;
    for (int i = 1; i < log.n; i++) {
        if (strstr(log.pages[i], "LATE_MARKER") != NULL) {
            late_page = i;
        }
        CHECK(strstr(log.pages[i], "EARLY_MARKER") == NULL);
    }
    CHECK(late_page > 0);
    CHECK(log.frozen_len == log.lens[0]);
    CHECK(memcmp(log.frozen, log.pages[0], log.frozen_len) == 0);
    CHECK(page_sink_pending(&sink) == 0);
    const char *buf = page_sink_pending_data(&sink);
    for (size_t i = 0; i < sink.cap; i++) {
        CHECK(buf[i] == '\0');
    }
    CHECK(sink.pages_flushed == log.n);

    for (int i = 0; i < log.n; i++) {
        free(log.pages[i]);
    }
    free(log.frozen);
    page_sink_free(&sink);
    return 0;
}

static int find_bytes(const char *buf, size_t n, const char *needle) {
    size_t m = strlen(needle);
    if (m == 0 || m > n) {
        return 0;
    }
    for (size_t i = 0; i + m <= n; i++) {
        if (memcmp(buf + i, needle, m) == 0) {
            return 1;
        }
    }
    return 0;
}

#include "pdf_inflate.c"

static int count_pages(const char *buf, size_t n) {
    const char *needle = "/Type /Page";
    size_t m = strlen(needle);
    int count = 0;
    for (size_t i = 0; i + m <= n; i++) {
        if (memcmp(buf + i, needle, m) == 0) {
            if (i + m >= n || buf[i + m] != 's') {
                count++;
            }
        }
    }
    return count;
}

static int extract_streams(const char *buf, size_t n, char **out, size_t *outlen, int max) {
    int count = 0;
    size_t i = 0;
    const char *key = "stream\n";
    size_t klen = 7;
    while (count < max && i + klen <= n) {
        if (memcmp(buf + i, key, klen) == 0) {
            size_t start = i + klen;
            size_t j = start;
            int found = 0;
            for (; j + 9 <= n; j++) {
                if (memcmp(buf + j, "endstream", 9) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                return -1;
            }
            size_t len = j - start;
            char *copy = malloc(len + 1);
            if (copy == NULL) {
                return -1;
            }
            memcpy(copy, buf + start, len);
            copy[len] = '\0';
            out[count] = copy;
            outlen[count] = len;
            count++;
            i = j + 9;
        } else {
            i++;
        }
    }
    return count;
}

static int check_xref(const char *buf, size_t n) {
    const char *key = "startxref\n";
    size_t klen = 10;
    size_t at = 0;
    int found = 0;
    for (; at + klen <= n; at++) {
        if (memcmp(buf + at, key, klen) == 0) {
            found = 1;
            break;
        }
    }
    CHECK(found);
    long xref_off = strtol(buf + at + klen, NULL, 10);
    CHECK(xref_off > 0);
    CHECK((size_t)xref_off + 5 < n);
    CHECK(memcmp(buf + xref_off, "xref\n", 5) == 0);
    const char *p = buf + xref_off + 5;
    int sub_start = -1;
    int sub_count = -1;
    CHECK(sscanf(p, "%d %d", &sub_start, &sub_count) == 2);
    CHECK(sub_start == 0);
    CHECK(sub_count >= 4);
    const char *nl = strchr(p, '\n');
    CHECK(nl != NULL);
    p = nl + 1;
    for (int i = 0; i < sub_count; i++) {
        CHECK(p + 20 <= buf + n);
        CHECK(p[10] == ' ');
        CHECK(p[16] == ' ');
        CHECK(p[19] == '\n');
        long off = 0;
        int gen = 0;
        char flag = 0;
        CHECK(sscanf(p, "%ld %d %c", &off, &gen, &flag) == 3);
        if (i == 0) {
            CHECK(flag == 'f');
            CHECK(gen == 65535);
        } else {
            CHECK(flag == 'n');
            CHECK(gen == 0);
            CHECK(off > 0 && (size_t)off < n);
            char expect[32];
            int el = snprintf(expect, sizeof expect, "%d 0 obj\n", i);
            CHECK(el > 0);
            CHECK(memcmp(buf + off, expect, (size_t)el) == 0);
        }
        p += 20;
    }
    return 0;
}

static int recovered_pages(const char *pdf, size_t n, char **texts, size_t *tlens, int max, int *npages);

static int test_pdf_appends_and_releases(void) {
    char path[] = "test_stream_pdf_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *fp = fdopen(fd, "w+b");
    CHECK(fp != NULL);

    PdfWriter w;
    CHECK(pdf_begin(&w, fp) == 0);

    char *page1 = malloc(32);
    char *page2 = malloc(32);
    CHECK(page1 != NULL && page2 != NULL);
    memcpy(page1, "ONLY_ON_PAGE_ONE", 17);
    memcpy(page2, "ONLY_ON_PAGE_TWO", 17);
    CHECK(pdf_add_page(&w, page1, 16) == 0);
    CHECK(pdf_live_page_bytes(&w) == 0);
    memset(page1, 0xA5, 32);
    free(page1);
    page1 = NULL;
    CHECK(pdf_add_page(&w, page2, 16) == 0);
    CHECK(pdf_live_page_bytes(&w) == 0);
    memset(page2, 0xA5, 32);
    free(page2);
    page2 = NULL;
    CHECK(pdf_page_count(&w) == 2);
    CHECK(pdf_end(&w) == 0);
    pdf_free(&w);
    CHECK(pdf_live_page_bytes(&w) == 0);

    CHECK(fseek(fp, 0, SEEK_END) == 0);
    long sz = ftell(fp);
    CHECK(sz > 0);
    CHECK(fseek(fp, 0, SEEK_SET) == 0);
    char *buf = malloc((size_t)sz + 1);
    CHECK(buf != NULL);
    CHECK(fread(buf, 1, (size_t)sz, fp) == (size_t)sz);
    buf[sz] = '\0';
    fclose(fp);
    unlink(path);

    CHECK(memcmp(buf, "%PDF-", 5) == 0);
    CHECK(find_bytes(buf, (size_t)sz, "xref\n"));
    CHECK(find_bytes(buf, (size_t)sz, "trailer"));
    CHECK(find_bytes(buf, (size_t)sz, "%%EOF"));
    CHECK(find_bytes(buf, (size_t)sz, "/Type /Catalog"));
    CHECK(find_bytes(buf, (size_t)sz, "/Type /Pages"));
    CHECK(count_pages(buf, (size_t)sz) == 2);
    CHECK(check_xref(buf, (size_t)sz) == 0);

    char *texts[4];
    size_t tlens[4];
    int npages = 0;
    int ns = recovered_pages(buf, (size_t)sz, texts, tlens, 4, &npages);
    CHECK(ns == 2);
    char t0[128];
    char t1[128];
    CHECK(tlens[0] < sizeof t0 && tlens[1] < sizeof t1);
    memcpy(t0, texts[0], tlens[0]);
    t0[tlens[0]] = '\0';
    memcpy(t1, texts[1], tlens[1]);
    t1[tlens[1]] = '\0';
    CHECK(strcmp(t0, "ONLY_ON_PAGE_ONE") == 0);
    CHECK(strcmp(t1, "ONLY_ON_PAGE_TWO") == 0);
    CHECK(strstr(t0, "ONLY_ON_PAGE_TWO") == NULL);
    CHECK(strstr(t1, "ONLY_ON_PAGE_ONE") == NULL);

    free(texts[0]);
    free(texts[1]);
    free(buf);
    return 0;
}

static char *build_sample(size_t pad, size_t *out_len) {
    const char *head =
        "<?xml version=\"1.0\"?>\n"
        "<root>\n"
        "<section id=\"alpha&amp;beta\">\n"
        "<title>START_MARKER</title>\n"
        "<!-- skip-me -->\n"
        "<child>nested&lt;child-text</child>\n"
        "<body>";
    const char *tail =
        "</body>\n"
        "<tail>END_MARKER</tail>\n"
        "</section>\n"
        "</root>\n";
    size_t n = strlen(head) + pad + strlen(tail);
    char *buf = malloc(n + 1);
    if (buf == NULL) {
        return NULL;
    }
    memcpy(buf, head, strlen(head));
    memset(buf + strlen(head), 'A', pad);
    memcpy(buf + strlen(head) + pad, tail, strlen(tail));
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static int write_temp(char *path_template, const char *data, size_t n) {
    int fd = mkstemp(path_template);
    if (fd < 0) {
        return -1;
    }
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w < 0) {
            close(fd);
            unlink(path_template);
            return -1;
        }
        off += (size_t)w;
    }
    if (close(fd) != 0) {
        unlink(path_template);
        return -1;
    }
    return 0;
}

static int load_file(const char *path, char **out, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    buf[sz] = '\0';
    fclose(fp);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

static int load_gid_map(const char *pdf, size_t n, unsigned *map) {
    const char *begin = "beginbfchar";
    const char *end = "endbfchar";
    size_t i = 0;
    int any = 0;
    while (i + 11 < n) {
        if (memcmp(pdf + i, begin, 11) != 0) {
            i++;
            continue;
        }
        size_t j = i + 11;
        size_t stop = 0;
        int found = 0;
        for (; j + 9 < n; j++) {
            if (memcmp(pdf + j, end, 9) == 0) {
                found = 1;
                stop = j;
                break;
            }
        }
        if (!found) {
            return -1;
        }
        size_t p = i;
        while (p < stop) {
            if (pdf[p] != '<') {
                p++;
                continue;
            }
            unsigned gid = 0;
            unsigned cp = 0;
            int k = 0;
            p++;
            while (k < 4 && p < stop && pdf[p] != '>') {
                char c = pdf[p++];
                gid <<= 4;
                if (c >= '0' && c <= '9') {
                    gid |= (unsigned)(c - '0');
                } else if (c >= 'A' && c <= 'F') {
                    gid |= (unsigned)(c - 'A' + 10);
                } else if (c >= 'a' && c <= 'f') {
                    gid |= (unsigned)(c - 'a' + 10);
                }
                k++;
            }
            while (p < stop && pdf[p] != '<') {
                p++;
            }
            if (p >= stop || pdf[p] != '<') {
                break;
            }
            p++;
            k = 0;
            while (k < 4 && p < stop && pdf[p] != '>') {
                char c = pdf[p++];
                cp <<= 4;
                if (c >= '0' && c <= '9') {
                    cp |= (unsigned)(c - '0');
                } else if (c >= 'A' && c <= 'F') {
                    cp |= (unsigned)(c - 'A' + 10);
                } else if (c >= 'a' && c <= 'f') {
                    cp |= (unsigned)(c - 'a' + 10);
                }
                k++;
            }
            if (gid < 65536) {
                map[gid] = cp;
                any = 1;
            }
        }
        i = stop + 9;
    }
    return any ? 0 : -1;
}

static int utf8_put(unsigned cp, char *out, size_t cap, size_t *o) {
    char tmp[4];
    int n = 0;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        return -1;
    }
    if (*o + (size_t)n >= cap) {
        return -1;
    }
    memcpy(out + *o, tmp, (size_t)n);
    *o += (size_t)n;
    return 0;
}

static int decode_hex_stream(const char *stream, size_t n, const unsigned *map, char *out, size_t cap, size_t *outlen) {
    size_t o = 0;
    int any = 0;
    for (size_t i = 0; i + 4 < n; i++) {
        if (stream[i] != '<') {
            continue;
        }
        size_t j = i + 1;
        while (j < n && stream[j] != '>') {
            j++;
        }
        if (j >= n) {
            break;
        }
        size_t after = j + 1;
        while (after < n && (stream[after] == ' ' || stream[after] == '\n' || stream[after] == '\r')) {
            after++;
        }
        if (after + 2 > n || memcmp(stream + after, "Tj", 2) != 0) {
            i = j;
            continue;
        }
        any = 1;
        for (size_t p = i + 1; p + 4 <= j; p += 4) {
            unsigned gid = 0;
            for (int k = 0; k < 4; k++) {
                char c = stream[p + (size_t)k];
                gid <<= 4;
                if (c >= '0' && c <= '9') {
                    gid |= (unsigned)(c - '0');
                } else if (c >= 'A' && c <= 'F') {
                    gid |= (unsigned)(c - 'A' + 10);
                } else if (c >= 'a' && c <= 'f') {
                    gid |= (unsigned)(c - 'a' + 10);
                } else {
                    gid = 65536;
                    break;
                }
            }
            if (gid > 65535) {
                continue;
            }
            unsigned cp = map[gid];
            if (cp == 0) {
                continue;
            }
            if (utf8_put(cp, out, cap, &o) != 0) {
                return -1;
            }
        }
        i = j;
    }
    if (!any) {
        return -1;
    }
    out[o] = '\0';
    *outlen = o;
    return 0;
}

static int recovered_pages(const char *raw, size_t raw_n, char **texts, size_t *tlens, int max, int *npages) {
    char *pdf = NULL;
    size_t n = 0;
    if (pdf_inflate_copy(raw, raw_n, &pdf, &n, NULL) != 0) {
        return -1;
    }
    *npages = count_pages(pdf, n);
    unsigned *map = calloc(65536, sizeof(unsigned));
    if (map == NULL || load_gid_map(pdf, n, map) != 0) {
        free(map);
        free(pdf);
        return -1;
    }
    char *streams[64];
    size_t slens[64];
    int ns = extract_streams(pdf, n, streams, slens, 64);
    free(pdf);
    if (ns < 0) {
        free(map);
        return -1;
    }
    int outn = 0;
    for (int i = 0; i < ns; i++) {
        int is_page = 0;
        for (size_t k = 0; k + 3 <= slens[i]; k++) {
            if (memcmp(streams[i] + k, "BT\n", 3) == 0) {
                is_page = 1;
                break;
            }
        }
        if (is_page && outn < max) {
            texts[outn] = malloc(slens[i] + 1);
            size_t got = 0;
            if (texts[outn] == NULL ||
                decode_hex_stream(streams[i], slens[i], map, texts[outn], slens[i] + 1, &got) != 0) {
                free(texts[outn]);
                free(streams[i]);
                free(map);
                return -1;
            }
            tlens[outn] = got;
            outn++;
        }
        free(streams[i]);
    }
    free(map);
    return outn;
}

static int assert_sample_pdf(const char *path) {
    char *pdf = NULL;
    size_t n = 0;
    CHECK(load_file(path, &pdf, &n) == 0);
    CHECK(memcmp(pdf, "%PDF-", 5) == 0);
    CHECK(find_bytes(pdf, n, "xref\n"));
    CHECK(find_bytes(pdf, n, "trailer"));
    CHECK(find_bytes(pdf, n, "%%EOF"));
    CHECK(check_xref(pdf, n) == 0);

    char *texts[64];
    size_t tlens[64];
    int npages = 0;
    int ns = recovered_pages(pdf, n, texts, tlens, 64, &npages);
    CHECK(ns >= 2);
    CHECK(npages == ns);
    CHECK(npages >= 2);

    char *all = malloc(n + 1);
    CHECK(all != NULL);
    size_t ao = 0;
    for (int i = 0; i < ns; i++) {
        memcpy(all + ao, texts[i], tlens[i]);
        ao += tlens[i];
    }
    all[ao] = '\0';

    CHECK(strstr(all, "section") != NULL);
    CHECK(strstr(all, "child") != NULL);
    CHECK(strstr(all, "alpha&beta") != NULL);
    CHECK(strstr(all, "alpha&amp;beta") == NULL);
    CHECK(strstr(all, "nested<child-text") != NULL);
    CHECK(strstr(all, "nested&lt;child-text") == NULL);
    CHECK(strstr(all, "START_MARKER") != NULL);
    CHECK(strstr(all, "END_MARKER") != NULL);
    CHECK(strstr(all, "skip-me") == NULL);
    CHECK(strstr(texts[0], "START_MARKER") != NULL);
    CHECK(strstr(texts[0], "END_MARKER") == NULL);
    CHECK(strstr(texts[0], "</root>") == NULL);
    int end_on_later = 0;
    for (int i = 1; i < ns; i++) {
        if (strstr(texts[i], "END_MARKER") != NULL) {
            end_on_later = 1;
        }
    }
    CHECK(end_on_later == 1);
    /* The last page is the tail of the document, not a repeat of page 1. */
    CHECK(strstr(texts[ns - 1], "END_MARKER") != NULL ||
          strstr(texts[ns - 1], "</root>") != NULL ||
          strstr(texts[ns - 1], "</tail>") != NULL);
    CHECK(strstr(texts[ns - 1], "START_MARKER") == NULL);

    for (int i = 0; i < ns; i++) {
        free(texts[i]);
    }
    free(all);
    free(pdf);
    return 0;
}

static int test_convert_pipeline(void) {
    size_t pad = (size_t)XML2PDF_PAGE_CAP * 2 + 200;
    size_t len = 0;
    char *xml = build_sample(pad, &len);
    CHECK(xml != NULL);
    char xml_path[] = "test_stream_in_XXXXXX";
    char pdf_path[] = "test_stream_out_XXXXXX";
    CHECK(write_temp(xml_path, xml, len) == 0);
    free(xml);
    int pdf_fd = mkstemp(pdf_path);
    CHECK(pdf_fd >= 0);
    close(pdf_fd);

    CHECK(xml2pdf_convert(xml_path, pdf_path) == 0);
    int rc = assert_sample_pdf(pdf_path);

    char pdf_path2[] = "test_stream_out_XXXXXX";
    int pdf_fd2 = mkstemp(pdf_path2);
    if (pdf_fd2 >= 0) {
        close(pdf_fd2);
    }
    int rc2 = 1;
    if (pdf_fd2 >= 0 && xml2pdf_convert(xml_path, pdf_path2) == 0) {
        char *a = NULL, *b = NULL;
        size_t na = 0, nb = 0;
        if (load_file(pdf_path, &a, &na) == 0 && load_file(pdf_path2, &b, &nb) == 0) {
            int pa = count_pages(a, na);
            int pb = count_pages(b, nb);
            char *ta[64];
            char *tb[64];
            size_t la[64];
            size_t lb[64];
            int npa = 0, npb = 0;
            int sa = recovered_pages(a, na, ta, la, 64, &npa);
            int sb = recovered_pages(b, nb, tb, lb, 64, &npb);
            if (sa == sb && sa > 1 && pa == pb && pa >= 2) {
                rc2 = 0;
                for (int i = 0; i < sa; i++) {
                    if (la[i] != lb[i] || memcmp(ta[i], tb[i], la[i]) != 0) {
                        rc2 = 1;
                        break;
                    }
                }
            }
            if (sa > 0) {
                for (int i = 0; i < sa; i++) {
                    free(ta[i]);
                }
            }
            if (sb > 0) {
                for (int i = 0; i < sb; i++) {
                    free(tb[i]);
                }
            }
            free(a);
            free(b);
        }
    }
    unlink(xml_path);
    unlink(pdf_path);
    unlink(pdf_path2);
    CHECK(rc == 0);
    CHECK(rc2 == 0);
    return 0;
}

static int parse_limit(const char *xml, size_t limit, Rec *recs, int max, int *nout) {
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    in.limit = limit;
    char err[160];
    int pr = parse_recs(chunk_read, &in, recs, max, nout, err, sizeof err);
    if (pr != 0) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    if (in.i != in.n) {
        fprintf(stderr, "short consume %zu/%zu\n", in.i, in.n);
        return 1;
    }
    return 0;
}

static void concat_all(const Rec *recs, int n, char *out, size_t cap, size_t *outlen) {
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (recs[i].kind != XML_EV_TEXT) {
            continue;
        }
        size_t L = recs[i].text_len;
        if (o + L >= cap) {
            L = cap - 1 - o;
        }
        memcpy(out + o, recs[i].text, L);
        o += L;
    }
    out[o] = '\0';
    *outlen = o;
}

static int expect_err(const char *xml, const char *snippet) {
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    in.limit = 1;
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            if (strstr(ev.error, snippet) == NULL) {
                fprintf(stderr, "error [%s] missing [%s]\n", ev.error, snippet);
                return 1;
            }
            return 0;
        }
        if (k == XML_EV_EOF) {
            fprintf(stderr, "eof, wanted error for %s\n", xml);
            return 1;
        }
    }
}

static int test_cdata_and_numeric(void) {
    Rec recs[32];
    int n = 0;
    char text[128];
    size_t tlen = 0;

    CHECK(parse_limit("<a><![CDATA[x&lt;y]]></a>", 1, recs, 32, &n) == 0);
    CHECK(expect_start(&recs[0], "a", NULL, NULL));
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "x&lt;y") == 0);
    CHECK(recs[n - 1].kind == XML_EV_END && strcmp(recs[n - 1].name, "a") == 0);

    CHECK(parse_limit("<a>A<![CDATA[B]]>C</a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "ABC") == 0);

    CHECK(parse_limit("<a><![CDATA[]]></a>", 1, recs, 32, &n) == 0);
    CHECK(n == 2);
    CHECK(recs[0].kind == XML_EV_START && recs[1].kind == XML_EV_END);

    CHECK(parse_limit("<a><![CDATA[]]]></a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "]") == 0);

    CHECK(parse_limit("<a><![CDATA[]]]]></a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "]]") == 0);

    CHECK(parse_limit("<a><![CDATA[<b>]]></a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "<b>") == 0);

    CHECK(expect_err("<a><!NOTCDATA[nope]></a>", "unsupported declaration") == 0);

    const char *split = "<a><![CDATA[hello]]></a>";
    const char *mark = strstr(split, "[CDATA[");
    const char *term = strstr(split, "]]>");
    CHECK(mark != NULL && term != NULL);
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = split;
    in.n = strlen(split);
    in.cuts[0] = (size_t)(mark - split) + 2;
    in.cuts[1] = (size_t)(term - split) + 1;
    in.ncuts = 2;
    char err[160];
    CHECK(parse_recs(chunk_read, &in, recs, 32, &n, err, sizeof err) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "hello") == 0);

    CHECK(parse_limit("<a>&#60;&#x3c;&#x3C;</a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "<<<") == 0);

    CHECK(parse_limit("<a>&#38;amp;</a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "&amp;") == 0);

    CHECK(expect_err("<a>&nbsp;</a>", "unknown entity") == 0);

    CHECK(parse_limit("<a>&#10;&#9;</a>", 1, recs, 32, &n) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(tlen == 2);
    CHECK(text[0] == '\n' && text[1] == '\t');

    CHECK(parse_limit("<a v=\"&#34;ok&#x27;\"/>", 1, recs, 32, &n) == 0);
    CHECK(expect_start(&recs[0], "a", "v", "\"ok'"));

    CHECK(parse_limit("<a v=\"&#x41F;\"/>", 1, recs, 32, &n) == 0);
    CHECK(recs[0].kind == XML_EV_START);
    CHECK(recs[0].nattr == 1);
    CHECK(recs[0].text_len == 0);
    {
        const unsigned char *v = (const unsigned char *)recs[0].attr_val[0];
        CHECK(v[0] == 0xD0 && v[1] == 0x9F && v[2] == 0);
    }

    const char *num = "<a>&#x3c;</a>";
    const char *amp = strstr(num, "&#x3c;");
    CHECK(amp != NULL);
    memset(&in, 0, sizeof in);
    in.s = num;
    in.n = strlen(num);
    in.cuts[0] = (size_t)(amp - num) + 2;
    in.ncuts = 1;
    CHECK(parse_recs(chunk_read, &in, recs, 32, &n, err, sizeof err) == 0);
    concat_all(recs, n, text, sizeof text, &tlen);
    CHECK(strcmp(text, "<") == 0);

    CHECK(expect_err("<a><![CDATA[hi", "truncated cdata") == 0);
    CHECK(expect_err("<a>&#;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#x;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#38</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#0;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#xD800;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#xFFFE;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a>&#x110000;</a>", "bad character reference") == 0);
    CHECK(expect_err("<a v=\"&#12;\"/>", "bad character reference") == 0);

    size_t pad = XML_TEXT_MAX + XML_BUF_SIZE + 8;
    char *body = malloc(pad);
    CHECK(body != NULL);
    memset(body, 'Q', pad);
    size_t xml_n = 12 + pad + 7;
    char *xml = malloc(xml_n + 1);
    CHECK(xml != NULL);
    memcpy(xml, "<a><![CDATA[", 12);
    memcpy(xml + 12, body, pad);
    memcpy(xml + 12 + pad, "]]></a>", 7);
    xml[xml_n] = '\0';
    free(body);

    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = xml_n;
    in.limit = 3;
    int need = (int)(pad / (XML_TEXT_MAX - 1)) + 8;
    Rec *big = calloc((size_t)need, sizeof(Rec));
    CHECK(big != NULL);
    CHECK(parse_recs(chunk_read, &in, big, need, &n, err, sizeof err) == 0);
    CHECK(in.i == in.n);
    size_t got = 0;
    int parts = 0;
    for (int i = 0; i < n; i++) {
        if (big[i].kind != XML_EV_TEXT) {
            continue;
        }
        parts++;
        CHECK(big[i].text_len > 0);
        CHECK(big[i].text_len < XML_TEXT_MAX);
        for (size_t j = 0; j < big[i].text_len; j++) {
            CHECK(big[i].text[j] == 'Q');
        }
        got += big[i].text_len;
    }
    CHECK(parts > 1);
    CHECK(got == pad);
    free(big);
    free(xml);

    size_t head = XML_TEXT_MAX - 2;
    char *wide = malloc(head + 64);
    CHECK(wide != NULL);
    memcpy(wide, "<a>", 3);
    memset(wide + 3, 'A', head);
    memcpy(wide + 3 + head, "&#x10FFFF;</a>", 14);
    wide[3 + head + 14] = '\0';
    memset(&in, 0, sizeof in);
    in.s = wide;
    in.n = 3 + head + 14;
    in.limit = 1;
    CHECK(parse_recs(chunk_read, &in, recs, 32, &n, err, sizeof err) == 0);
    const unsigned char u4[4] = {0xF4, 0x8F, 0xBF, 0xBF};
    int saw_cp = 0;
    size_t as = 0;
    for (int i = 0; i < n; i++) {
        if (recs[i].kind != XML_EV_TEXT) {
            continue;
        }
        as += recs[i].text_len;
        if (recs[i].text_len >= 4 &&
            memcmp(recs[i].text + recs[i].text_len - 4, u4, 4) == 0) {
            saw_cp = 1;
        }
        for (size_t j = 0; j + 4 <= recs[i].text_len; j++) {
            if (memcmp(recs[i].text + j, u4, 4) == 0) {
                saw_cp = 1;
            }
        }
    }
    CHECK(saw_cp == 1);
    CHECK(as == head + 4);
    free(wide);
    return 0;
}

typedef struct {
    unsigned long long left;
    int phase;
    const char *cur;
    int cur_len;
    int cur_pos;
} CdataIn;

static void cdata_arm(CdataIn *g, const char *s) {
    g->cur = s;
    g->cur_len = (int)strlen(s);
    g->cur_pos = 0;
}

static int cdata_read(void *user, char *dst, int max) {
    CdataIn *g = user;
    int w = 0;
    while (w < max) {
        if (g->cur != NULL && g->cur_pos < g->cur_len) {
            dst[w++] = g->cur[g->cur_pos++];
            continue;
        }
        g->cur = NULL;
        if (g->phase == 0) {
            cdata_arm(g, "<r><![CDATA[");
            g->phase = 1;
            continue;
        }
        if (g->phase == 1) {
            if (g->left == 0) {
                g->phase = 2;
                continue;
            }
            dst[w++] = 'Q';
            g->left--;
            continue;
        }
        if (g->phase == 2) {
            cdata_arm(g, "]]></r>");
            g->phase = 3;
            continue;
        }
        break;
    }
    return w;
}

static int test_cdata_stream_fixed_memory(void) {
    CdataIn g;
    memset(&g, 0, sizeof g);
    const unsigned long long payload = 4ULL * 1024ULL * 1024ULL;
    g.left = payload;

    struct rusage ru0, ru1;
    CHECK(getrusage(RUSAGE_SELF, &ru0) == 0);

    XmlParser p;
    CHECK(xml_parser_init(&p, cdata_read, &g) == 0);
    unsigned long long texts = 0;
    int saw_start = 0;
    int saw_end = 0;
    int parts = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            fprintf(stderr, "cdata stream: %s\n", ev.error);
            return 1;
        }
        if (k == XML_EV_EOF) {
            break;
        }
        if (k == XML_EV_START) {
            CHECK(strcmp(ev.name, "r") == 0);
            saw_start++;
        } else if (k == XML_EV_END) {
            CHECK(strcmp(ev.name, "r") == 0);
            saw_end++;
        } else if (k == XML_EV_TEXT) {
            parts++;
            CHECK(ev.text_len > 0);
            CHECK(ev.text_len < XML_TEXT_MAX);
            for (size_t i = 0; i < ev.text_len; i++) {
                CHECK(ev.text[i] == 'Q');
            }
            texts += ev.text_len;
        }
    }
    CHECK(getrusage(RUSAGE_SELF, &ru1) == 0);
    long delta_kb = ru1.ru_maxrss - ru0.ru_maxrss;
    printf("cdata-stream bytes=%llu parts=%d parser_bytes=%zu rss_delta_kb=%ld\n", texts, parts,
           sizeof(XmlParser), delta_kb);
    CHECK(saw_start == 1);
    CHECK(saw_end == 1);
    CHECK(texts == payload);
    CHECK(parts > 1);
    CHECK(p.in_cdata == 0);
    CHECK(p.cdata_mark == 0);
    CHECK(p.depth == 0);
    CHECK(delta_kb >= 0);
#if KI_RSS_GATE
    CHECK(delta_kb < 2048);
#endif
    return 0;
}

static int write_all(const char *path, const void *data, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    size_t w = fwrite(data, 1, n, fp);
    if (fclose(fp) != 0 || w != n) {
        return -1;
    }
    return 0;
}

static int same_bytes(const char *path, const void *data, size_t n) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    char *buf = malloc(n + 1);
    if (buf == NULL) {
        fclose(fp);
        return 0;
    }
    size_t got = fread(buf, 1, n + 1, fp);
    fclose(fp);
    int ok = got == n && memcmp(buf, data, n) == 0;
    free(buf);
    return ok;
}

static int count_temps(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    int n = 0;
    size_t plen = strlen(prefix);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, prefix, plen) == 0 && strlen(ent->d_name) == plen + 7 &&
            ent->d_name[plen] == '.') {
            n++;
        }
    }
    closedir(d);
    return n;
}

static int test_output_keeps_previous(void) {
    char dir[] = "/tmp/ki-xml-pdf-out-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char in_path[256];
    char out_path[256];
    char link_path[256];
    snprintf(in_path, sizeof in_path, "%s/in.xml", dir);
    snprintf(out_path, sizeof out_path, "%s/out.pdf", dir);
    snprintf(link_path, sizeof link_path, "%s/alias.xml", dir);

    const char *src = "<r>KEEP_SOURCE</r>";
    const char *old = "OLD_DESTINATION_BYTES";
    CHECK(write_all(in_path, src, strlen(src)) == 0);
    CHECK(write_all(out_path, old, strlen(old)) == 0);

    CHECK(xml2pdf_convert(in_path, in_path) != 0);
    CHECK(same_bytes(in_path, src, strlen(src)));

    char dotted[256];
    snprintf(dotted, sizeof dotted, "%s/./in.xml", dir);
    CHECK(xml2pdf_convert(in_path, dotted) != 0);
    CHECK(same_bytes(in_path, src, strlen(src)));

    CHECK(link(in_path, link_path) == 0);
    CHECK(xml2pdf_convert(in_path, link_path) != 0);
    CHECK(same_bytes(in_path, src, strlen(src)));
    CHECK(same_bytes(link_path, src, strlen(src)));
    unlink(link_path);

    CHECK(symlink(in_path, link_path) == 0);
    CHECK(xml2pdf_convert(in_path, link_path) != 0);
    CHECK(same_bytes(in_path, src, strlen(src)));
    unlink(link_path);

    size_t pad = XML2PDF_PAGE_CAP + 64;
    char *bad = malloc(pad + 32);
    CHECK(bad != NULL);
    memcpy(bad, "<root>", 6);
    memset(bad + 6, 'A', pad);
    memcpy(bad + 6 + pad, "</nope>", 7);
    bad[6 + pad + 7] = '\0';
    CHECK(write_all(in_path, bad, strlen(bad)) == 0);
    free(bad);
    CHECK(write_all(out_path, old, strlen(old)) == 0);
    CHECK(xml2pdf_convert(in_path, out_path) != 0);
    CHECK(same_bytes(out_path, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);

    CHECK(write_all(in_path, src, strlen(src)) == 0);
    xml2pdf_debug_fail_after_bytes(4);
    CHECK(xml2pdf_convert(in_path, out_path) != 0);
    CHECK(same_bytes(out_path, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);

    xml2pdf_debug_fail_at_page(1);
    CHECK(write_all(in_path, "<r>page</r>", 11) == 0);
    CHECK(xml2pdf_convert(in_path, out_path) != 0);
    CHECK(same_bytes(out_path, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);

    xml2pdf_debug_fail_before_publish(1);
    CHECK(xml2pdf_convert(in_path, out_path) != 0);
    CHECK(same_bytes(out_path, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);

    char rodir[256];
    snprintf(rodir, sizeof rodir, "%s/ro", dir);
    CHECK(mkdir(rodir, 0755) == 0);
    char ro_out[300];
    snprintf(ro_out, sizeof ro_out, "%s/out.pdf", rodir);
    CHECK(chmod(rodir, 0555) == 0);
    CHECK(xml2pdf_convert(in_path, ro_out) != 0);
    CHECK(access(ro_out, F_OK) != 0);
    CHECK(chmod(rodir, 0755) == 0);

    unlink(out_path);
    CHECK(xml2pdf_convert(in_path, out_path) == 0);
    FILE *fp = fopen(out_path, "rb");
    CHECK(fp != NULL);
    char magic[5];
    CHECK(fread(magic, 1, 5, fp) == 5);
    fclose(fp);
    CHECK(memcmp(magic, "%PDF-", 5) == 0);

    char fifo[256];
    snprintf(fifo, sizeof fifo, "%s/in.fifo", dir);
    CHECK(mkfifo(fifo, 0600) == 0);
    CHECK(write_all(out_path, old, strlen(old)) == 0);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = test_on_term;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, NULL);
        KiOptions opt;
        ki_options_init(&opt);
        opt.cancelled = test_cancelled;
        int rc = ki_convert_file(fifo, out_path, &opt, NULL);
        _exit(rc == 0 ? 0 : 2);
    }
    int wfd = open(fifo, O_WRONLY);
    CHECK(wfd >= 0);
    const char *chunk = "<root>";
    CHECK(write(wfd, chunk, strlen(chunk)) == (ssize_t)strlen(chunk));
    nanosleep(&(struct timespec){0, 200000000L}, NULL);
    CHECK(kill(pid, SIGTERM) == 0);
    int st = 0;
    CHECK(waitpid(pid, &st, 0) == pid);
    close(wfd);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 2);
    CHECK(same_bytes(out_path, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);

    pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        int rc = xml2pdf_convert(fifo, out_path);
        _exit(rc == 0 ? 0 : 2);
    }
    wfd = open(fifo, O_WRONLY);
    CHECK(wfd >= 0);
    CHECK(write(wfd, chunk, strlen(chunk)) == (ssize_t)strlen(chunk));
    nanosleep(&(struct timespec){0, 200000000L}, NULL);
    CHECK(kill(pid, SIGKILL) == 0);
    CHECK(waitpid(pid, &st, 0) == pid);
    close(wfd);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    CHECK(same_bytes(out_path, old, strlen(old)));
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "out.pdf.", 8) == 0) {
            char tmp[512];
            snprintf(tmp, sizeof tmp, "%s/%s", dir, ent->d_name);
            unlink(tmp);
        }
    }
    closedir(d);

    unlink(in_path);
    unlink(out_path);
    unlink(fifo);
    rmdir(rodir);
    rmdir(dir);
    return 0;
}

static int test_wellformed_profile(void) {
    const char *bad[] = {
        "",
        "   \n\t",
        "<a/><b/>",
        "<r a=\"1\" a=\"2\"/>",
        "<r a=\"1\"b=\"2\"/>",
        "<r a=\"<\"/>",
        "<a>&nbsp;</a>",
        "<a>ok</a>trailing",
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?><a/>",
    };
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        ChunkIn in;
        memset(&in, 0, sizeof in);
        in.s = bad[i];
        in.n = strlen(bad[i]);
        in.limit = 1;
        XmlParser p;
        xml_parser_init(&p, chunk_read, &in);
        int saw = 0;
        for (;;) {
            XmlEvent ev;
            int k = xml_next(&p, &ev);
            if (k == XML_EV_ERROR) {
                saw = 1;
                CHECK(strstr(ev.error, "at byte") != NULL);
                break;
            }
            if (k == XML_EV_EOF) {
                break;
            }
        }
        if (!saw) {
            fprintf(stderr, "accepted bad xml: %s\n", bad[i][0] ? bad[i] : "<empty>");
            return 1;
        }
    }
    const char nul[] = {'<', 'a', '>', 'x', '\0', 'y', '<', '/', 'a', '>', '\0'};
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = nul;
    in.n = 10;
    in.limit = 1;
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    int saw = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            saw = 1;
            CHECK(strstr(ev.error, "illegal character") != NULL);
            break;
        }
        if (k == XML_EV_EOF) {
            break;
        }
    }
    CHECK(saw == 1);

    const unsigned char badutf[] = {'<', 'a', '>', 0xFF, '<', '/', 'a', '>'};
    memset(&in, 0, sizeof in);
    in.s = (const char *)badutf;
    in.n = sizeof badutf;
    in.limit = 1;
    xml_parser_init(&p, chunk_read, &in);
    saw = 0;
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            saw = 1;
            break;
        }
        if (k == XML_EV_EOF) {
            break;
        }
    }
    CHECK(saw == 1);

    Rec recs[8];
    int n = 0;
    CHECK(parse_limit("<root  id = \"z\" ></root>", 1, recs, 8, &n) == 0);
    CHECK(expect_start(&recs[0], "root", "id", "z"));
    CHECK(parse_limit("<a>&#x41F;</a>", 1, recs, 8, &n) == 0);

    char odir[] = "/tmp/ki-xml-oracle-XXXXXX";
    CHECK(mkdtemp(odir) != NULL);
    char oxml[256];
    char opdf[256];
    snprintf(oxml, sizeof oxml, "%s/in.xml", odir);
    snprintf(opdf, sizeof opdf, "%s/out.pdf", odir);
    CHECK(write_all(oxml, "", 0) == 0);
    CHECK(xml2pdf_convert(oxml, opdf) != 0);
    CHECK(access(opdf, F_OK) != 0);
    const char *agree[] = {"<a/><b/>", "<r a=\"1\" a=\"2\"/>", "<root id=\"z\"/>", "<a>ok</a>"};
    int expect_py[] = {1, 1, 0, 0};
    for (int i = 0; i < 4; i++) {
        CHECK(write_all(oxml, agree[i], strlen(agree[i])) == 0);
        char cmd[512];
        snprintf(cmd, sizeof cmd,
                 "python3 -c \"import sys,xml.etree.ElementTree as T; T.parse(sys.argv[1])\" \"%s\" >/dev/null 2>&1",
                 oxml);
        int py = system(cmd);
        int py_bad = py != 0;
        ChunkIn cin;
        memset(&cin, 0, sizeof cin);
        cin.s = agree[i];
        cin.n = strlen(agree[i]);
        cin.limit = 1;
        XmlParser cp;
        xml_parser_init(&cp, chunk_read, &cin);
        int our_bad = 0;
        for (;;) {
            XmlEvent ev;
            int k = xml_next(&cp, &ev);
            if (k == XML_EV_ERROR) {
                our_bad = 1;
                break;
            }
            if (k == XML_EV_EOF) {
                break;
            }
        }
        if (our_bad != expect_py[i] || py_bad != expect_py[i]) {
            fprintf(stderr, "oracle mismatch %s our=%d py=%d\n", agree[i], our_bad, py_bad);
            return 1;
        }
    }
    unlink(oxml);
    rmdir(odir);
    return 0;
}

static int test_review_regressions(void) {
    FontFace face;
    CHECK(font_load(&face, "tests/corpus/truncated-hmtx.ttf") != 0);
    setenv("KI_XML_PDF_FONT", "tests/corpus/truncated-hmtx.ttf", 1);
    CHECK(font_open_default(&face) != 0);
    unsetenv("KI_XML_PDF_FONT");
    CHECK(font_load(&face, "fonts/DejaVuSans.ttf") == 0);
    int baseline = font_first_baseline(&face, 792, 40, 10);
    int ascent = face.ascent * 10 / face.units;
    CHECK(792 - baseline - ascent >= 40);
    font_free(&face);

    char out[] = "/tmp/ki-xml-pdf-review-XXXXXX";
    int fd = mkstemp(out);
    CHECK(fd >= 0);
    close(fd);
    char cmd[512];
    CHECK(xml2pdf_convert("tests/corpus/utf8_512.xml", out) == 0);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", out);
    FILE *pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    int ya = 0;
    int prev = 0;
    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (prev == 0xD1 && ch == 0x8F) {
            ya++;
        }
        prev = ch;
    }
    CHECK(pclose(pipe) == 0);
    CHECK(ya == 512);
    CHECK(xml2pdf_convert("tests/corpus/split_whitespace.xml", out) == 0);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", out);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    char text[4096];
    size_t n = fread(text, 1, sizeof text - 1, pipe);
    text[n] = 0;
    int prc = pclose(pipe);
    unlink(out);
    CHECK(prc == 0);
    CHECK(strstr(text, "A B") != NULL);
    CHECK(strstr(text, "AB") == NULL || strstr(text, "A B") != NULL);
    return 0;
}

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    int chunk;
    char *buf;
    size_t len;
    size_t cap;
    int fail_at;
    int calls;
    int stop;
} MemIo;

static int mem_read(void *user, char *dst, int max) {
    MemIo *in = user;
    if (in->i >= in->n || max <= 0) {
        return 0;
    }
    int n = max;
    if (in->chunk > 0 && n > in->chunk) {
        n = in->chunk;
    }
    if ((size_t)n > in->n - in->i) {
        n = (int)(in->n - in->i);
    }
    memcpy(dst, in->s + in->i, (size_t)n);
    in->i += (size_t)n;
    return n;
}

static int mem_cancel_stop(void *user);
static int mem_write(void *user, const char *src, int n);

static int mem_write_see_pages(void *user, const char *src, int n) {
    MemIo *io = user;
    int wr = mem_write(user, src, n);
    if (wr < 0 || io->stop || io->buf == NULL) {
        return wr;
    }
    for (size_t i = 0; i + 12 <= io->len; i++) {
        if (memcmp(io->buf + i, "/Type /Pages", 12) == 0) {
            io->stop = 1;
            break;
        }
    }
    return wr;
}

static int mem_write(void *user, const char *src, int n) {
    MemIo *out = user;
    out->calls++;
    if (out->fail_at > 0 && out->calls >= out->fail_at) {
        return -1;
    }
    if (n < 0) {
        return -1;
    }
    if (out->len + (size_t)n > out->cap) {
        size_t cap = out->cap == 0 ? 1024 * 1024 : out->cap * 2;
        while (cap < out->len + (size_t)n) {
            cap *= 2;
        }
        char *grown = realloc(out->buf, cap);
        if (grown == NULL) {
            return -1;
        }
        out->buf = grown;
        out->cap = cap;
    }
    memcpy(out->buf + out->len, src, (size_t)n);
    out->len += (size_t)n;
    return n;
}

static int fail_late_map(void *user, KiDoc *doc, const KiEvent *ev) {
    int *seen = user;
    if (ev->kind == KI_EV_TEXT) {
        (*seen)++;
        if (*seen > 1) {
            return ki_doc_fail(doc, "mapping failed");
        }
        return ki_doc_text(doc, ev->text, ev->text_len);
    }
    if (ev->kind == KI_EV_END) {
        return ki_doc_break(doc);
    }
    return 0;
}

static int text_only_map(void *user, KiDoc *doc, const KiEvent *ev) {
    (void)user;
    if (ev->kind == KI_EV_TEXT) {
        return ki_doc_text(doc, ev->text, ev->text_len);
    }
    if (ev->kind == KI_EV_END) {
        return ki_doc_break(doc);
    }
    return 0;
}

struct ThreadJob {
    const char *in;
    const char *out;
    int rc;
};

static void *thread_convert(void *arg) {
    struct ThreadJob *job = arg;
    job->rc = ki_convert_file(job->in, job->out, NULL, NULL);
    return NULL;
}

static int test_library_contract(void) {
    KiResult result;
    int rc = ki_convert_file("/tmp", "/tmp/ki-xml-pdf-not-written.pdf", NULL, &result);
    CHECK(rc == XML2PDF_ERR_IO);
    CHECK(result.cause == KI_CAUSE_IO);

    rc = ki_convert_file("tests/fixture.xml", "/tmp/ki-missing-parent-zz/out.pdf", NULL, &result);
    CHECK(rc == XML2PDF_ERR_IO);
    CHECK(result.cause == KI_CAUSE_IO);

    char dest[] = "/tmp/ki-lib-dest-XXXXXX";
    int fd = mkstemp(dest);
    CHECK(fd >= 0);
    CHECK(write(fd, "OLD", 3) == 3);
    close(fd);
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = "tests/corpus/truncated-hmtx.ttf";
    rc = ki_convert_file("tests/fixture.xml", dest, &opt, &result);
    CHECK(rc == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    FILE *fp = fopen(dest, "rb");
    CHECK(fp != NULL);
    char old[4] = {0};
    CHECK(fread(old, 1, 3, fp) == 3);
    fclose(fp);
    CHECK(memcmp(old, "OLD", 3) == 0);

    ki_options_init(&opt);
    opt.max_output_bytes = 1024;
    rc = ki_convert_file("tests/fixture.xml", dest, &opt, &result);
    CHECK(rc == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_RESOURCE);
    fp = fopen(dest, "rb");
    CHECK(fp != NULL);
    CHECK(fread(old, 1, 3, fp) == 3);
    fclose(fp);
    CHECK(memcmp(old, "OLD", 3) == 0);

    char many[] = "/tmp/ki-lib-many-XXXXXX";
    fd = mkstemp(many);
    CHECK(fd >= 0);
    FILE *xml = fdopen(fd, "w");
    CHECK(xml != NULL);
    fputs("<doc>", xml);
    for (int i = 0; i < 80; i++) {
        fputs("line\n", xml);
    }
    fputs("</doc>", xml);
    fclose(xml);
    ki_options_init(&opt);
    opt.max_pages = 1;
    rc = ki_convert_file(many, dest, &opt, &result);
    CHECK(rc == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_RESOURCE);
    unlink(many);

    ki_options_init(&opt);
    opt.max_spool_bytes = 16;
    rc = ki_convert_file("tests/fixture.xml", dest, &opt, &result);
    CHECK(rc == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_RESOURCE);

    const char *bad[] = {"<doc/><![CDATA[OUT]]>", "<![CDATA[OUT]]><doc/>", "<doc>A<!FOO>B</doc>",
                         "<doc>hello]]>world</doc>", "<doc><?xml version=\"1.0\"?>ok</doc>",
                         "<?XML version=\"1.0\"?><doc/>", "<doc><!--\x01-->ok</doc>"};
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        char in[] = "/tmp/ki-lib-bad-XXXXXX";
        fd = mkstemp(in);
        CHECK(fd >= 0);
        CHECK(write(fd, bad[i], strlen(bad[i])) == (ssize_t)strlen(bad[i]));
        close(fd);
        rc = ki_convert_file(in, dest, NULL, &result);
        CHECK(rc != 0);
        fp = fopen(dest, "rb");
        CHECK(fp != NULL);
        CHECK(fread(old, 1, 3, fp) == 3);
        fclose(fp);
        CHECK(memcmp(old, "OLD", 3) == 0);
        unlink(in);
    }

    char lf[] = "/tmp/ki-lib-lf-XXXXXX";
    char cr[] = "/tmp/ki-lib-cr-XXXXXX";
    char lfpdf[] = "/tmp/ki-lib-lfp-XXXXXX";
    char crpdf[] = "/tmp/ki-lib-crp-XXXXXX";
    int a = mkstemp(lf);
    int b = mkstemp(cr);
    int c = mkstemp(lfpdf);
    int d = mkstemp(crpdf);
    CHECK(a >= 0 && b >= 0 && c >= 0 && d >= 0);
    const char *lf_xml = "<doc>first\nsecond</doc>";
    const char *cr_xml = "<doc>first\r\nsecond</doc>";
    CHECK(write(a, lf_xml, strlen(lf_xml)) == (ssize_t)strlen(lf_xml));
    CHECK(write(b, cr_xml, strlen(cr_xml)) == (ssize_t)strlen(cr_xml));
    close(a);
    close(b);
    close(c);
    close(d);
    CHECK(ki_convert_file(lf, lfpdf, NULL, NULL) == 0);
    CHECK(ki_convert_file(cr, crpdf, NULL, NULL) == 0);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", lfpdf);
    FILE *pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    char left[256];
    size_t n = fread(left, 1, sizeof left - 1, pipe);
    left[n] = 0;
    CHECK(pclose(pipe) == 0);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", crpdf);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    char right[256];
    n = fread(right, 1, sizeof right - 1, pipe);
    right[n] = 0;
    CHECK(pclose(pipe) == 0);
    CHECK(strcmp(left, right) == 0);
    CHECK(strstr(left, "first") != NULL && strstr(left, "second") != NULL);
    unlink(lf);
    unlink(cr);
    unlink(lfpdf);
    unlink(crpdf);

    ki_options_init(&opt);
    opt.map = text_only_map;
    char mapped[] = "/tmp/ki-lib-map-XXXXXX";
    fd = mkstemp(mapped);
    CHECK(fd >= 0);
    close(fd);
    rc = ki_convert_file("tests/corpus/split_whitespace.xml", mapped, &opt, &result);
    CHECK(rc == 0);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", mapped);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    char text[256];
    n = fread(text, 1, sizeof text - 1, pipe);
    text[n] = 0;
    CHECK(pclose(pipe) == 0);
    CHECK(strstr(text, "A B") != NULL);
    CHECK(strstr(text, "doc") == NULL);
    unlink(mapped);

    static volatile sig_atomic_t host_hits;
    host_hits = 0;
    struct sigaction sa;
    struct sigaction previous;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = test_on_term;
    sigemptyset(&sa.sa_mask);
    CHECK(sigaction(SIGUSR1, &sa, &previous) == 0);
    CHECK(ki_convert_file("tests/fixture.xml", dest, NULL, NULL) == 0);
    struct sigaction now;
    CHECK(sigaction(SIGUSR1, NULL, &now) == 0);
    CHECK(now.sa_handler == test_on_term);
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0);
    (void)host_hits;

    char in1[] = "/tmp/ki-lib-a-XXXXXX";
    char in2[] = "/tmp/ki-lib-b-XXXXXX";
    char out1[] = "/tmp/ki-lib-oa-XXXXXX";
    char out2[] = "/tmp/ki-lib-ob-XXXXXX";
    int f1 = mkstemp(in1);
    int f2 = mkstemp(in2);
    int o1 = mkstemp(out1);
    int o2 = mkstemp(out2);
    CHECK(f1 >= 0 && f2 >= 0 && o1 >= 0 && o2 >= 0);
    CHECK(write(f1, "<doc>one</doc>", 14) == 14);
    CHECK(write(f2, "<doc>two</doc>", 14) == 14);
    close(f1);
    close(f2);
    close(o1);
    close(o2);
    struct ThreadJob j1 = {in1, out1, -1};
    struct ThreadJob j2 = {in2, out2, -1};
    pthread_t t1;
    pthread_t t2;
    CHECK(pthread_create(&t1, NULL, thread_convert, &j1) == 0);
    CHECK(pthread_create(&t2, NULL, thread_convert, &j2) == 0);
    CHECK(pthread_join(t1, NULL) == 0);
    CHECK(pthread_join(t2, NULL) == 0);
    CHECK(j1.rc == 0 && j2.rc == 0);
    unlink(in1);
    unlink(in2);
    unlink(out1);
    unlink(out2);
    unlink(dest);

    FontFace face;
    unsigned char junk[64];
    memset(junk, 0, sizeof junk);
    for (int i = 0; i < 8; i++) {
        char font[] = "/tmp/ki-lib-font-XXXXXX";
        junk[i] = (unsigned char)(i * 17);
        fd = mkstemp(font);
        CHECK(fd >= 0);
        CHECK(write(fd, junk, sizeof junk) == (ssize_t)sizeof junk);
        close(fd);
        CHECK(font_load(&face, font) != 0);
        unlink(font);
    }

    char cdata[1400];
    int o = sprintf(cdata, "<doc><![CDATA[");
    for (int i = 0; i < 512; i++) {
        cdata[o++] = (char)0xD1;
        cdata[o++] = (char)0x8F;
    }
    memcpy(cdata + o, "]]></doc>", 9);
    o += 9;
    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = cdata;
    io.n = (size_t)o;
    io.chunk = 1;
    ki_options_init(&opt);
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    rc = ki_convert(&opt, &result);
    CHECK(rc == 0);
    CHECK(result.cause == KI_CAUSE_NONE);
    int ya = 0;
    for (size_t i = 0; i + 1 < io.len; i++) {
        if ((unsigned char)io.buf[i] == 0xD1 && (unsigned char)io.buf[i + 1] == 0x8F) {
            ya++;
        }
    }
    /* The PDF stores glyphs, not raw UTF-8 of the source. Count via a reader. */
    char pdf[] = "/tmp/ki-lib-cb-XXXXXX";
    fd = mkstemp(pdf);
    CHECK(fd >= 0);
    CHECK(write(fd, io.buf, io.len) == (ssize_t)io.len);
    close(fd);
    free(io.buf);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", pdf);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    ya = 0;
    int prev = 0;
    int ch = 0;
    while ((ch = fgetc(pipe)) != EOF) {
        if (prev == 0xD1 && ch == 0x8F) {
            ya++;
        }
        prev = ch;
    }
    CHECK(pclose(pipe) == 0);
    unlink(pdf);
    CHECK(ya == 512);

    memset(&io, 0, sizeof io);
    io.s = "<doc>A B</doc>";
    io.n = strlen(io.s);
    io.chunk = 3;
    opt.io_user = &io;
    CHECK(ki_convert(&opt, &result) == 0);
    char pdf2[] = "/tmp/ki-lib-cb2-XXXXXX";
    fd = mkstemp(pdf2);
    CHECK(fd >= 0);
    CHECK(write(fd, io.buf, io.len) == (ssize_t)io.len);
    close(fd);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", pdf2);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    n = fread(text, 1, sizeof text - 1, pipe);
    text[n] = 0;
    CHECK(pclose(pipe) == 0);
    CHECK(strstr(text, "A B") != NULL);
    free(io.buf);
    unlink(pdf2);

    memset(&io, 0, sizeof io);
    io.s = "<doc>Hi</doc>";
    io.n = strlen(io.s);
    io.chunk = 1;
    io.fail_at = 2;
    opt.io_user = &io;
    rc = ki_convert(&opt, &result);
    CHECK(rc == XML2PDF_ERR_IO);
    CHECK(result.cause == KI_CAUSE_IO);
    free(io.buf);

    CHECK(ki_convert(NULL, &result) == XML2PDF_ERR_USAGE);
    CHECK(result.cause == KI_CAUSE_USAGE);

    char kept[] = "/tmp/ki-lib-kept-XXXXXX";
    fd = mkstemp(kept);
    CHECK(fd >= 0);
    CHECK(write(fd, "OLD", 3) == 3);
    close(fd);
    int seen_text = 0;
    ki_options_init(&opt);
    opt.map = fail_late_map;
    opt.map_user = &seen_text;
    rc = ki_convert_file("tests/fixture.xml", kept, &opt, &result);
    CHECK(rc == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_PROFILE);
    CHECK(strcmp(result.message, "mapping failed") == 0);
    fp = fopen(kept, "rb");
    CHECK(fp != NULL);
    CHECK(fread(old, 1, 3, fp) == 3);
    fclose(fp);
    CHECK(memcmp(old, "OLD", 3) == 0);
    unlink(kept);
    return 0;
}

static int unused_read(void *user, char *dst, int max) {
    (void)dst;
    (void)max;
    (*(int *)user)++;
    return -1;
}

static int unused_write(void *user, const char *src, int n) {
    (void)src;
    (void)n;
    (*(int *)user)++;
    return -1;
}

static int test_callback_usage_result(void) {
    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = "<doc>reused result</doc>";
    io.n = strlen(io.s);
    KiOptions opt;
    ki_options_init(&opt);
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    KiResult result;
    CHECK(ki_convert(&opt, &result) == 0);
    CHECK(result.pages > 0 && result.output_bytes > 0);
    free(io.buf);
    KiResult successful = result;
    for (int reused = 0; reused < 2; reused++) {
        for (int invalid = 0; invalid < 10; invalid++) {
            int calls = 0;
            ki_options_init(&opt);
            opt.read_bytes = unused_read;
            opt.write_bytes = unused_write;
            opt.io_user = &calls;
            switch (invalid) {
                case 0: opt.size = 1; break;
                case 1: opt.max_pages = -1; break;
                case 2: opt.max_output_bytes = -1; break;
                case 3: opt.max_spool_bytes = -1; break;
                case 4: opt.record_offset = -1; break;
                case 5: opt.record_limit = -1; break;
                case 6: opt.profile = 99; break;
                case 7: opt.read_bytes = NULL; break;
                case 8: opt.write_bytes = NULL; break;
                case 9: break; /* NULL options */
            }
            if (reused) {
                result = successful;
            } else {
                memset(&result, 0x5a, sizeof result);
            }
            const KiOptions *options = invalid == 9 ? NULL : &opt;
            CHECK(ki_convert(options, &result) == XML2PDF_ERR_USAGE);
            CHECK(result.status == XML2PDF_ERR_USAGE && result.cause == KI_CAUSE_USAGE);
            const char *message = invalid == 0 ? "options size mismatch" :
                invalid < 7 ? "invalid option" : "read and write callbacks are required";
            CHECK(strcmp(result.message, message) == 0);
            CHECK(result.pages == 0 && result.objects == 0 && result.offset_bytes == 0);
            CHECK(result.output_bytes == 0 && result.spool_bytes == 0);
            CHECK(result.report_total == 0 && result.report_selected == 0 && result.report_skipped == 0);
            CHECK(calls == 0);
            CHECK(ki_convert(options, NULL) == XML2PDF_ERR_USAGE);
            CHECK(calls == 0);
        }
    }
    return 0;
}

static int logical_lines(const char *text, char lines[][128], int max) {
    int n = 0;
    const char *p = text;
    while (*p != '\0' && n < max) {
        while (*p == '\n' || *p == '\f' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        size_t i = 0;
        while (*p != '\0' && *p != '\n' && *p != '\f' && i + 1 < 128) {
            lines[n][i++] = *p++;
        }
        while (i > 0 && lines[n][i - 1] == ' ') {
            i--;
        }
        lines[n][i] = '\0';
        if (i > 0) {
            n++;
        }
        while (*p != '\0' && *p != '\n' && *p != '\f') {
            p++;
        }
    }
    return n;
}

typedef struct {
    MemIo memory;
    FILE *owner_input;
    FILE *owner_output;
    size_t read_total;
    size_t read_fail_after;
    int read_calls;
    int eof_seen;
    int write_calls;
    int write_fail_at;
    /* 0: normal; 1: fail; 2: accept n-1; 3: accept zero; 4: return n+1. */
    int write_mode;
} CallbackFileIo;

static void callback_file_io_init(CallbackFileIo *io, const char *xml, int chunk) {
    memset(io, 0, sizeof *io);
    io->memory.s = xml;
    io->memory.n = strlen(xml);
    io->memory.chunk = chunk;
}

static int callback_file_read(void *user, char *dst, int max) {
    CallbackFileIo *io = user;
    io->read_calls++;
    if (io->read_fail_after > 0 && io->read_total >= io->read_fail_after) {
        return -1;
    }
    if (io->memory.chunk > 0 && max > io->memory.chunk) {
        max = io->memory.chunk;
    }
    if (io->read_fail_after > 0 &&
        (size_t)max > io->read_fail_after - io->read_total) {
        max = (int)(io->read_fail_after - io->read_total);
    }
    int n;
    if (io->owner_input != NULL) {
        n = (int)fread(dst, 1, (size_t)max, io->owner_input);
        if (n == 0 && ferror(io->owner_input)) {
            return -1;
        }
    } else {
        n = mem_read(&io->memory, dst, max);
    }
    if (n == 0) {
        io->eof_seen = 1;
    } else if (n > 0) {
        io->read_total += (size_t)n;
    }
    return n;
}

static int callback_file_write(void *user, const char *src, int n) {
    CallbackFileIo *io = user;
    io->write_calls++;
    int mode = io->write_fail_at > 0 && io->write_calls >= io->write_fail_at ?
        io->write_mode : 0;
    if (mode == 1) {
        return -1;
    }
    if (mode == 3) {
        return 0;
    }
    if (mode == 4) {
        return n + 1;
    }
    int accepted = mode == 2 ? n - 1 : n;
    if (accepted < 0) {
        return -1;
    }
    if (io->owner_output != NULL) {
        size_t got = fwrite(src, 1, (size_t)accepted, io->owner_output);
        return got == (size_t)accepted ? accepted : -1;
    }
    return mem_write(&io->memory, src, accepted);
}

static int callback_cancel_at_eof(void *user) {
    return ((CallbackFileIo *)user)->eof_seen != 0;
}

static void callback_report_options(KiOptions *opt, CallbackFileIo *io, const char *dir) {
    ki_options_init(opt);
    opt->font_path = "fonts/DejaVuSans.ttf";
    opt->temp_dir = dir;
    opt->profile = KI_PROFILE_REPORT;
    opt->read_bytes = callback_file_read;
    opt->write_bytes = callback_file_write;
    opt->io_user = io;
}

static int callback_directory_entries(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) {
        return -1;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(d);
    return count;
}

static int callback_pdf_lines(const char *path, const char *const *expected, int count) {
    char command[600];
    snprintf(command, sizeof command, "pdftotext -layout -nopgbrk -q %s -", path);
    FILE *reader = popen(command, "r");
    CHECK(reader != NULL);
    char text[4096];
    size_t n = fread(text, 1, sizeof text - 1, reader);
    text[n] = '\0';
    int read_failed = ferror(reader);
    int close_status = pclose(reader);
    CHECK(!read_failed && close_status == 0);
    CHECK(n < sizeof text - 1);
    char lines[24][128];
    int actual = logical_lines(text, lines, 24);
    CHECK(actual == count);
    for (int i = 0; i < count; i++) {
        CHECK(strcmp(lines[i], expected[i]) == 0);
    }
    return 0;
}

static int callback_result_matches(const KiResult *a, const KiResult *b) {
    return a->status == b->status && a->cause == b->cause &&
        a->pages == b->pages && a->objects == b->objects &&
        a->offset_bytes == b->offset_bytes && a->output_bytes == b->output_bytes &&
        a->spool_bytes == b->spool_bytes &&
        a->report_total == b->report_total && a->report_selected == b->report_selected &&
        a->report_skipped == b->report_skipped;
}

static int callback_result_is_empty(const KiResult *result) {
    return result->pages == 0 && result->objects == 0 && result->offset_bytes == 0 &&
        result->output_bytes == 0 && result->spool_bytes == 0 &&
        result->report_total == 0 && result->report_selected == 0 &&
        result->report_skipped == 0;
}

typedef struct {
    CallbackFileIo io;
    KiOptions options;
    KiResult result;
    const char *path;
    int status;
} CallbackFileJob;

static void *callback_file_thread(void *user) {
    CallbackFileJob *job = user;
    job->status = ki_convert_to_file(job->path, &job->options, &job->result);
    return NULL;
}

static int test_callback_to_file(void) {
    const char *xml =
        "<report version=\"1\" title=\"Pipeline\">"
        "<record id=\"first\"><field name=\"Value\">SKIPPED_VALUE</field></record>"
        "<record id=\"second\"><field name=\"Value\">B<![CDATA[ & caf]]>&#233;</field></record>"
        "</report>";
    const char *expected[] = {
        "Pipeline",
        "Запись second",
        "Value: B & café",
        "Всего записей: 2",
        "Выбрано записей: 1",
        "Пропущено записей: 1",
    };
    const char *old = "OLD_DESTINATION_BYTES";
    char dir[] = "/tmp/ki-callback-file-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char source[256], reference[256], output[256];
    snprintf(source, sizeof source, "%s/source.xml", dir);
    snprintf(reference, sizeof reference, "%s/reference.pdf", dir);
    snprintf(output, sizeof output, "%s/out.pdf", dir);
    CHECK(write_all(source, xml, strlen(xml)) == 0);
    CHECK(write_all(output, old, strlen(old)) == 0);

    CallbackFileIo io;
    callback_file_io_init(&io, xml, 1);
    io.write_fail_at = 1;
    io.write_mode = 1;
    KiOptions options;
    callback_report_options(&options, &io, dir);
    options.record_offset = 1;
    options.record_limit = 1;
    KiResult baseline;
    CHECK(ki_convert_file(source, reference, &options, &baseline) == XML2PDF_OK);
    /* Existing file API still ignores BOTH I/O callbacks. */
    CHECK(io.read_calls == 0 && io.write_calls == 0);
    CHECK(baseline.status == XML2PDF_OK && baseline.cause == KI_CAUSE_NONE);
    CHECK(callback_pdf_lines(reference, expected, 6) == 0);
    int directory_baseline = callback_directory_entries(dir);
    CHECK(directory_baseline == 3);

    /* One-byte reads are not EOF; selection still consumes the complete input.
       A missing or failing write callback must be ignored for file publication. */
    const int chunks[] = {1, 3, 4096};
    for (size_t i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
        CHECK(write_all(output, old, strlen(old)) == 0);
        callback_file_io_init(&io, xml, chunks[i]);
        io.write_fail_at = 1;
        io.write_mode = 1;
        callback_report_options(&options, &io, dir);
        options.record_offset = 1;
        options.record_limit = 1;
        if (i == 1) {
            options.write_bytes = NULL;
        }
        KiResult result;
        memset(&result, 0x5a, sizeof result);
        CHECK(ki_convert_to_file(output, &options, &result) == XML2PDF_OK);
        CHECK(io.read_total == strlen(xml) && io.memory.i == io.memory.n && io.eof_seen);
        CHECK(io.write_calls == 0);
        CHECK(callback_result_matches(&result, &baseline));
        struct stat published;
        CHECK(stat(output, &published) == 0);
        CHECK((long)published.st_size == result.output_bytes);
        CHECK(!same_bytes(output, old, strlen(old)));
        CHECK(callback_pdf_lines(output, expected, 6) == 0);
        CHECK(count_temps(dir, "out.pdf") == 0);
        CHECK(callback_directory_entries(dir) == directory_baseline);
    }

    /* Invalid setup cannot consume input or retain successful/sentinel metrics. */
    for (int reused = 0; reused < 2; reused++) {
        for (int invalid = 0; invalid < 10; invalid++) {
            CHECK(write_all(output, old, strlen(old)) == 0);
            callback_file_io_init(&io, xml, 1);
            callback_report_options(&options, &io, dir);
            const KiOptions *opt = &options;
            const char *path = output;
            switch (invalid) {
                case 0: opt = NULL; break;
                case 1: options.read_bytes = NULL; break;
                case 2: path = NULL; break;
                case 3: options.size = 1; break;
                case 4: options.max_pages = -1; break;
                case 5: options.max_output_bytes = -1; break;
                case 6: options.max_spool_bytes = -1; break;
                case 7: options.record_offset = -1; break;
                case 8: options.record_limit = -1; break;
                case 9: options.profile = 99; break;
            }
            KiResult result;
            if (reused) {
                result = baseline;
            } else {
                memset(&result, 0x5a, sizeof result);
            }
            CHECK(ki_convert_to_file(path, opt, &result) == XML2PDF_ERR_USAGE);
            CHECK(result.status == XML2PDF_ERR_USAGE && result.cause == KI_CAUSE_USAGE);
            CHECK(result.message[0] != '\0' && callback_result_is_empty(&result));
            CHECK(io.read_calls == 0 && io.write_calls == 0);
            CHECK(ki_convert_to_file(path, opt, NULL) == XML2PDF_ERR_USAGE);
            CHECK(io.read_calls == 0 && io.write_calls == 0);
            CHECK(same_bytes(output, old, strlen(old)));
            CHECK(count_temps(dir, "out.pdf") == 0);
            CHECK(callback_directory_entries(dir) == directory_baseline);
        }
    }

    char missing[300];
    snprintf(missing, sizeof missing, "%s/missing/out.pdf", dir);
    callback_file_io_init(&io, xml, 1);
    callback_report_options(&options, &io, dir);
    KiResult result = baseline;
    CHECK(ki_convert_to_file(missing, &options, &result) == XML2PDF_ERR_IO);
    CHECK(result.status == XML2PDF_ERR_IO && result.cause == KI_CAUSE_IO);
    CHECK(callback_result_is_empty(&result));
    CHECK(io.read_calls == 0 && io.write_calls == 0);
    CHECK(callback_directory_entries(dir) == directory_baseline);

    /* More than one physical page before a late input or mapper failure. */
    char large[8192];
    size_t used = (size_t)snprintf(large, sizeof large,
        "<report version=\"1\" title=\"Large\"><record id=\"1\">"
        "<field name=\"Value\"><![CDATA[");
    for (int i = 0; i < 160; i++) {
        used += (size_t)snprintf(large + used, sizeof large - used, "line %03d\n", i);
    }
    used += (size_t)snprintf(large + used, sizeof large - used,
        "]]></field></record></report>");
    CHECK(used < sizeof large);
    size_t before_end = used - strlen("</report>");
    char malformed[8192];
    memcpy(malformed, large, before_end);
    strcpy(malformed + before_end, "</wrong>");

    for (int failure = 0; failure < 5; failure++) {
        CHECK(write_all(output, old, strlen(old)) == 0);
        callback_file_io_init(&io, failure == 0 ? malformed : large, 13);
        callback_report_options(&options, &io, dir);
        int wanted_status = XML2PDF_ERR_RESOURCE;
        int wanted_cause = KI_CAUSE_RESOURCE;
        switch (failure) {
            case 0: wanted_status = XML2PDF_ERR_XML; wanted_cause = KI_CAUSE_XML; break;
            case 1:
                io.read_fail_after = before_end;
                wanted_status = XML2PDF_ERR_IO;
                wanted_cause = KI_CAUSE_IO;
                break;
            case 2: options.max_pages = 1; break;
            case 3: options.max_output_bytes = 1024; break;
            case 4: options.max_spool_bytes = 16; break;
        }
        result = baseline;
        CHECK(ki_convert_to_file(output, &options, &result) == wanted_status);
        CHECK(result.status == wanted_status && result.cause == wanted_cause);
        CHECK(result.message[0] != '\0' && io.write_calls == 0);
        if (failure < 2) {
            CHECK(result.pages > 0 && io.read_total >= before_end);
        }
        CHECK(same_bytes(output, old, strlen(old)));
        CHECK(count_temps(dir, "out.pdf") == 0);
        CHECK(callback_directory_entries(dir) == directory_baseline);
    }

    CHECK(write_all(output, old, strlen(old)) == 0);
    callback_file_io_init(&io, large, 13);
    callback_report_options(&options, &io, dir);
    int seen_text = 0;
    options.profile = KI_PROFILE_RECORDS;
    options.map = fail_late_map;
    options.map_user = &seen_text;
    CHECK(ki_convert_to_file(output, &options, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_PROFILE && strcmp(result.message, "mapping failed") == 0);
    CHECK(seen_text > 1 && same_bytes(output, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);
    CHECK(callback_directory_entries(dir) == directory_baseline);

    /* Cancellation once the read callback has returned EOF. */
    CHECK(write_all(output, old, strlen(old)) == 0);
    callback_file_io_init(&io, xml, 1);
    callback_report_options(&options, &io, dir);
    options.cancelled = callback_cancel_at_eof;
    options.cancel_user = &io;
    CHECK(ki_convert_to_file(output, &options, &result) == XML2PDF_ERR_RESOURCE);
    CHECK(result.status == XML2PDF_ERR_RESOURCE && result.cause == KI_CAUSE_CANCEL);
    CHECK(io.eof_seen && io.read_total == strlen(xml));
    CHECK(same_bytes(output, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);
    CHECK(callback_directory_entries(dir) == directory_baseline);

    /* The existing fsync wrapper arms cancellation only after real fsync.
       Reset the global before CHECK so a failure cannot affect later groups. */
    CHECK(write_all(output, old, strlen(old)) == 0);
    callback_file_io_init(&io, xml, 3);
    callback_report_options(&options, &io, dir);
    options.cancelled = cancel_after_fsync;
    arm_fsync_cancel = 1;
    int fsync_status = ki_convert_to_file(output, &options, &result);
    int fsync_phase = arm_fsync_cancel;
    arm_fsync_cancel = 0;
    CHECK(fsync_phase == 2);
    CHECK(fsync_status == XML2PDF_ERR_RESOURCE && result.cause == KI_CAUSE_CANCEL);
    CHECK(same_bytes(output, old, strlen(old)));
    CHECK(count_temps(dir, "out.pdf") == 0);
    CHECK(callback_directory_entries(dir) == directory_baseline);
    callback_file_io_init(&io, xml, 3);
    callback_report_options(&options, &io, dir);
    CHECK(ki_convert_to_file(output, &options, &result) == XML2PDF_OK);

    /* ki_convert accepts only a complete write. Accepted bytes are retained on
       negative, short, zero or over-count callback returns. */
    for (int mode = 1; mode <= 4; mode++) {
        callback_file_io_init(&io, xml, 3);
        callback_report_options(&options, &io, dir);
        io.write_fail_at = 2;
        io.write_mode = mode;
        CHECK(ki_convert(&options, &result) == XML2PDF_ERR_IO);
        CHECK(result.status == XML2PDF_ERR_IO && result.cause == KI_CAUSE_IO);
        CHECK(io.write_calls >= 2 && io.memory.len >= 5);
        CHECK(memcmp(io.memory.buf, "%PDF-", 5) == 0);
        free(io.memory.buf);
    }

    /* Caller-owned input FILE remains open on both success and late error. */
    for (int bad = 0; bad < 2; bad++) {
        const char *input = bad ? malformed : xml;
        CHECK(write_all(source, input, strlen(input)) == 0);
        callback_file_io_init(&io, input, 3);
        io.owner_input = fopen(source, "rb");
        CHECK(io.owner_input != NULL);
        int input_fd = fileno(io.owner_input);
        CHECK(input_fd >= 0);
        callback_report_options(&options, &io, dir);
        int status = ki_convert_to_file(output, &options, &result);
        CHECK(status == (bad ? XML2PDF_ERR_XML : XML2PDF_OK));
        CHECK(fcntl(input_fd, F_GETFD) != -1);
        CHECK(fseek(io.owner_input, 0, SEEK_SET) == 0);
        CHECK(fgetc(io.owner_input) == '<');
        CHECK(fclose(io.owner_input) == 0);
        CHECK(io.write_calls == 0);
        CHECK(count_temps(dir, "out.pdf") == 0);
    }

    /* Caller-owned read/write FILEs stay open on success and late failure. */
    for (int bad = 0; bad < 2; bad++) {
        const char *input = bad ? malformed : xml;
        CHECK(write_all(source, input, strlen(input)) == 0);
        callback_file_io_init(&io, input, 3);
        io.owner_input = fopen(source, "rb");
        io.owner_output = tmpfile();
        CHECK(io.owner_input != NULL && io.owner_output != NULL);
        int input_fd = fileno(io.owner_input);
        int output_fd = fileno(io.owner_output);
        callback_report_options(&options, &io, dir);
        int status = ki_convert(&options, &result);
        CHECK(status == (bad ? XML2PDF_ERR_XML : XML2PDF_OK));
        CHECK(fcntl(input_fd, F_GETFD) != -1 && fcntl(output_fd, F_GETFD) != -1);
        CHECK(fseek(io.owner_input, 0, SEEK_SET) == 0 && fgetc(io.owner_input) == '<');
        CHECK(fflush(io.owner_output) == 0 && fseek(io.owner_output, 0, SEEK_SET) == 0);
        char magic[5];
        CHECK(fread(magic, 1, sizeof magic, io.owner_output) == sizeof magic);
        CHECK(memcmp(magic, "%PDF-", sizeof magic) == 0);
        CHECK(fclose(io.owner_input) == 0 && fclose(io.owner_output) == 0);
    }

    /* Restore host dispositions before any CHECK that may return. */
    struct sigaction host, previous_pipe, previous_usr, after_pipe, after_usr;
    memset(&host, 0, sizeof host);
    host.sa_handler = test_on_term;
    host.sa_flags = SA_RESTART;
    sigemptyset(&host.sa_mask);
    sigaddset(&host.sa_mask, SIGUSR2);
    CHECK(sigaction(SIGPIPE, &host, &previous_pipe) == 0);
    int install_usr = sigaction(SIGUSR1, &host, &previous_usr);
    if (install_usr != 0) {
        sigaction(SIGPIPE, &previous_pipe, NULL);
    }
    CHECK(install_usr == 0);
    callback_file_io_init(&io, xml, 3);
    callback_report_options(&options, &io, dir);
    int signal_status = ki_convert_to_file(output, &options, &result);
    callback_file_io_init(&io, malformed, 3);
    int signal_failure = ki_convert_to_file(output, &options, &result);
    int query_pipe = sigaction(SIGPIPE, NULL, &after_pipe);
    int query_usr = sigaction(SIGUSR1, NULL, &after_usr);
    int restore_pipe = sigaction(SIGPIPE, &previous_pipe, NULL);
    int restore_usr = sigaction(SIGUSR1, &previous_usr, NULL);
    CHECK(signal_status == XML2PDF_OK && signal_failure == XML2PDF_ERR_XML);
    CHECK(query_pipe == 0 && query_usr == 0 && restore_pipe == 0 && restore_usr == 0);
    CHECK(after_pipe.sa_handler == test_on_term && after_usr.sa_handler == test_on_term);
    CHECK((after_pipe.sa_flags & SA_RESTART) && (after_usr.sa_flags & SA_RESTART));
    CHECK(sigismember(&after_pipe.sa_mask, SIGUSR2) == 1);
    CHECK(sigismember(&after_usr.sa_mask, SIGUSR2) == 1);

    /* Two independent callback contexts publish independently. */
    const char *thread_xml[] = {
        "<report version=\"1\" title=\"One\"><record id=\"a\">"
        "<field name=\"Value\">FIRST_CONTEXT</field></record></report>",
        "<report version=\"1\" title=\"Two\"><record id=\"b\">"
        "<field name=\"Value\">SECOND_CONTEXT</field></record></report>",
    };
    const char *thread_expected[][6] = {
        {"One", "Запись a", "Value: FIRST_CONTEXT", "Всего записей: 1",
         "Выбрано записей: 1", "Пропущено записей: 0"},
        {"Two", "Запись b", "Value: SECOND_CONTEXT", "Всего записей: 1",
         "Выбрано записей: 1", "Пропущено записей: 0"},
    };
    char thread_paths[2][256];
    CallbackFileJob jobs[2];
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) {
        memset(&jobs[i], 0, sizeof jobs[i]);
        snprintf(thread_paths[i], sizeof thread_paths[i], "%s/thread-%d.pdf", dir, i);
        jobs[i].path = thread_paths[i];
        callback_file_io_init(&jobs[i].io, thread_xml[i], i + 1);
        callback_report_options(&jobs[i].options, &jobs[i].io, dir);
        CHECK(pthread_create(&threads[i], NULL, callback_file_thread, &jobs[i]) == 0);
    }
    for (int i = 0; i < 2; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(jobs[i].status == XML2PDF_OK && jobs[i].result.cause == KI_CAUSE_NONE);
        CHECK(jobs[i].result.report_total == 1 && jobs[i].result.report_selected == 1);
        CHECK(jobs[i].io.read_total == strlen(thread_xml[i]) && jobs[i].io.write_calls == 0);
        CHECK(callback_pdf_lines(thread_paths[i], thread_expected[i], 6) == 0);
        unlink(thread_paths[i]);
    }
    CHECK(count_temps(dir, "out.pdf") == 0);
    CHECK(callback_directory_entries(dir) == directory_baseline);

    unlink(source);
    unlink(reference);
    unlink(output);
    CHECK(rmdir(dir) == 0);
    return 0;
}


static int test_report_v1(void) {
    char out[] = "/tmp/ki-report-XXXXXX";
    int fd = mkstemp(out);
    CHECK(fd >= 0);
    close(fd);
    KiOptions opt;
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    KiResult result;
    CHECK(ki_convert_file("examples/report-v1.xml", out, &opt, &result) == 0);
    CHECK(result.report_total == 2 && result.report_selected == 2 && result.report_skipped == 0);
    char cmd[512];
    snprintf(cmd, sizeof cmd, "pdftotext -layout -nopgbrk -q %s -", out);
    FILE *pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    char got[2048];
    size_t n = fread(got, 1, sizeof got - 1, pipe);
    got[n] = 0;
    CHECK(pclose(pipe) == 0);
    FILE *exp = fopen("examples/report-v1.expected.txt", "rb");
    CHECK(exp != NULL);
    char want[2048];
    n = fread(want, 1, sizeof want - 1, exp);
    want[n] = 0;
    fclose(exp);
    char glines[16][128];
    char wlines[16][128];
    int ng = logical_lines(got, glines, 16);
    int nw = logical_lines(want, wlines, 16);
    CHECK(ng == nw);
    for (int i = 0; i < ng; i++) {
        CHECK(strcmp(glines[i], wlines[i]) == 0);
    }

    const char *empty = "<report version=\"1\" title=\"Пусто\"/>";
    char empty_path[] = "/tmp/ki-report-empty-XXXXXX";
    fd = mkstemp(empty_path);
    CHECK(fd >= 0);
    CHECK(write(fd, empty, strlen(empty)) == (ssize_t)strlen(empty));
    close(fd);
    CHECK(ki_convert_file(empty_path, out, &opt, &result) == 0);
    CHECK(result.report_total == 0 && result.report_selected == 0);
    unlink(empty_path);

    const char *bad = "<report version=\"1\" title=\"T\"><record></record></report>";
    char bad_path[] = "/tmp/ki-report-bad-XXXXXX";
    fd = mkstemp(bad_path);
    CHECK(fd >= 0);
    CHECK(write(fd, bad, strlen(bad)) == (ssize_t)strlen(bad));
    close(fd);
    {
        char kept[] = "/tmp/ki-report-kept-XXXXXX";
        int kfd = mkstemp(kept);
        CHECK(kfd >= 0);
        CHECK(write(kfd, "OLD", 3) == 3);
        close(kfd);
        int rc = ki_convert_file(bad_path, kept, &opt, &result);
        CHECK(rc == XML2PDF_ERR_XML);
        CHECK(result.cause == KI_CAUSE_PROFILE);
        FILE *fp = fopen(kept, "rb");
        char old[4] = {0};
        CHECK(fp != NULL && fread(old, 1, 3, fp) == 3);
        fclose(fp);
        CHECK(memcmp(old, "OLD", 3) == 0);
        unlink(kept);
    }
    unlink(bad_path);

    const char *noid = "<report version=\"1\" title=\"T\"><record><field name=\"A\">x</field></record></report>";
    char noid_path[] = "/tmp/ki-report-noid-XXXXXX";
    fd = mkstemp(noid_path);
    CHECK(fd >= 0);
    CHECK(write(fd, noid, strlen(noid)) == (ssize_t)strlen(noid));
    close(fd);
    opt.record_offset = 0;
    opt.record_limit = 0;
    CHECK(ki_convert_file(noid_path, out, &opt, &result) == 0);
    snprintf(cmd, sizeof cmd, "pdftotext -layout -nopgbrk -q %s -", out);
    pipe = popen(cmd, "r");
    CHECK(pipe != NULL);
    n = fread(got, 1, sizeof got - 1, pipe);
    got[n] = 0;
    CHECK(pclose(pipe) == 0);
    CHECK(strstr(got, "Запись 1") != NULL);
    CHECK(strstr(got, "A: x") != NULL);
    unlink(noid_path);

    opt.record_offset = 1;
    opt.record_limit = 1;
    CHECK(ki_convert_file("examples/report-v1.xml", out, &opt, &result) == 0);
    CHECK(result.report_total == 2 && result.report_selected == 1 && result.report_skipped == 1);

    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    const char *negs[] = {
        "<report version=\"2\" title=\"T\"/>",
        "<report version=\"1\" title=\" \"/>",
        "<report version=\"1\" title=\"T\"><nope/></report>",
        "<report version=\"1\" title=\"T\" xmlns:a=\"u\"/>",
        "<report version=\"1\" title=\"T\"><record><field name=\"A\"><b/></field></record></report>",
        "<report version=\"1\" title=\"T\"><record><field name=\"A\" xml:space=\"foo\">x</field></record></report>",
        "<report version=\"1\" title=\"T\"><record><field>x</field></record></report>",
    };
    for (int i = 0; i < (int)(sizeof negs / sizeof negs[0]); i++) {
        char neg[] = "/tmp/ki-report-neg-XXXXXX";
        int nfd = mkstemp(neg);
        CHECK(nfd >= 0);
        CHECK(write(nfd, negs[i], strlen(negs[i])) == (ssize_t)strlen(negs[i]));
        close(nfd);
        int rc = ki_convert_file(neg, out, &opt, &result);
        CHECK(rc != 0);
        CHECK(result.cause == KI_CAUSE_PROFILE);
        unlink(neg);
    }

    ki_options_init(&opt);
    opt.max_pages = -1;
    CHECK(ki_convert_file("examples/hello.xml", out, &opt, &result) == XML2PDF_ERR_USAGE);
    CHECK(result.cause == KI_CAUSE_USAGE);

    char root[] = "/tmp/ki-longroot-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char longdir[640];
    snprintf(longdir, sizeof longdir, "%s", root);
    for (int i = 0; i < 3; i++) {
        char comp[96];
        memset(comp, 'a' + i, 90);
        comp[90] = '\0';
        size_t used = strlen(longdir);
        snprintf(longdir + used, sizeof longdir - used, "/%s", comp);
        CHECK(mkdir(longdir, 0755) == 0);
    }
    CHECK(strlen(longdir) > 256);
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    opt.temp_dir = longdir;
    CHECK(ki_convert_file("examples/report-v1.xml", out, &opt, &result) == 0);

    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = "<doc>Hello</doc>";
    io.n = strlen(io.s);
    io.chunk = 64;
    ki_options_init(&opt);
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write_see_pages;
    opt.cancelled = mem_cancel_stop;
    opt.cancel_user = &io;
    opt.io_user = &io;
    CHECK(ki_convert(&opt, &result) == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_CANCEL);
    free(io.buf);

    const char *glyph =
        "<report version=\"1\" title=\"T\"><record><field name=\"n\">&#x1F600;</field></record></report>";
    char glyph_path[] = "/tmp/ki-report-glyph-XXXXXX";
    fd = mkstemp(glyph_path);
    CHECK(fd >= 0);
    CHECK(write(fd, glyph, strlen(glyph)) == (ssize_t)strlen(glyph));
    close(fd);
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    CHECK(ki_convert_file(glyph_path, out, &opt, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    CHECK(strstr(result.message, "U+1F600") != NULL);
    unlink(glyph_path);

    const char *extra =
        "<report version=\"1\" title=\"T\"><record extra=\"z\"><field name=\"n\">a</field></record></report>";
    char extra_path[] = "/tmp/ki-report-extra-XXXXXX";
    fd = mkstemp(extra_path);
    CHECK(fd >= 0);
    CHECK(write(fd, extra, strlen(extra)) == (ssize_t)strlen(extra));
    close(fd);
    CHECK(ki_convert_file(extra_path, out, &opt, &result) != 0);
    CHECK(result.cause == KI_CAUSE_PROFILE);
    CHECK(strstr(result.message, "extra") != NULL);
    CHECK(strstr(result.message, "byte") != NULL);
    unlink(extra_path);

    const char *yo =
        "<report version=\"1\" title=\"T\"><record><field name=\"n\">\xd1\x91</field></record></report>";
    char yo_path[] = "/tmp/ki-report-yo-XXXXXX";
    fd = mkstemp(yo_path);
    CHECK(fd >= 0);
    CHECK(write(fd, yo, strlen(yo)) == (ssize_t)strlen(yo));
    close(fd);
    CHECK(ki_convert_file(yo_path, out, &opt, &result) == 0);
    unlink(yo_path);

    const char *broken = "<report version=\"1\" title=\"T\"><record></report>";
    char broken_path[] = "/tmp/ki-report-broken-XXXXXX";
    fd = mkstemp(broken_path);
    CHECK(fd >= 0);
    CHECK(write(fd, broken, strlen(broken)) == (ssize_t)strlen(broken));
    close(fd);
    CHECK(ki_convert_file(broken_path, out, &opt, &result) != 0);
    CHECK(result.cause == KI_CAUSE_XML);
    unlink(broken_path);

    const char *plain = "<doc>&#x1F600;</doc>";
    char plain_path[] = "/tmp/ki-records-glyph-XXXXXX";
    fd = mkstemp(plain_path);
    CHECK(fd >= 0);
    CHECK(write(fd, plain, strlen(plain)) == (ssize_t)strlen(plain));
    close(fd);
    CHECK(ki_convert_file(plain_path, out, NULL, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    CHECK(strstr(result.message, "U+1F600") != NULL);
    ki_options_init(&opt);
    CHECK(ki_convert_file(plain_path, out, &opt, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    unlink(plain_path);

    const char *empty_rec = "<report version=\"1\" title=\"T\"><record/></report>";
    char empty_rec_path[] = "/tmp/ki-empty-rec-XXXXXX";
    fd = mkstemp(empty_rec_path);
    CHECK(fd >= 0);
    CHECK(write(fd, empty_rec, strlen(empty_rec)) == (ssize_t)strlen(empty_rec));
    close(fd);
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    CHECK(ki_convert_file(empty_rec_path, out, &opt, &result) != 0);
    CHECK(result.cause == KI_CAUSE_PROFILE);
    const char *at = strstr(result.message, "at byte ");
    CHECK(at != NULL && atoi(at + 8) > 0);
    unlink(empty_rec_path);

    {
        const char *xml = "<r><a/></r>";
        ChunkIn c;
        memset(&c, 0, sizeof c);
        c.s = xml;
        c.n = strlen(xml);
        c.limit = 1;
        XmlParser p;
        CHECK(xml_parser_init(&p, chunk_read, &c) == 0);
        size_t prev = 0;
        int saw_end = 0;
        int saw_eof = 0;
        for (;;) {
            XmlEvent ev;
            memset(&ev, 0, sizeof ev);
            ev.byte_offset = (size_t)-1;
            int k = xml_next(&p, &ev);
            CHECK(ev.byte_offset != (size_t)-1);
            CHECK(ev.byte_offset >= prev);
            prev = ev.byte_offset;
            if (k == XML_EV_END && strcmp(ev.name, "a") == 0) {
                CHECK(ev.byte_offset == 7);
                saw_end = 1;
            }
            if (k == XML_EV_EOF) {
                CHECK(ev.byte_offset == strlen(xml));
                saw_eof = 1;
                memset(&ev, 0, sizeof ev);
                ev.byte_offset = (size_t)-1;
                CHECK(xml_next(&p, &ev) == XML_EV_EOF);
                CHECK(ev.byte_offset == strlen(xml));
                break;
            }
            CHECK(k != XML_EV_ERROR);
        }
        CHECK(saw_end && saw_eof);
        const char *bad = "<";
        memset(&c, 0, sizeof c);
        c.s = bad;
        c.n = 1;
        c.limit = 1;
        CHECK(xml_parser_init(&p, chunk_read, &c) == 0);
        XmlEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.byte_offset = (size_t)-1;
        CHECK(xml_next(&p, &ev) == XML_EV_ERROR);
        CHECK(ev.byte_offset != (size_t)-1);
        size_t err_at = ev.byte_offset;
        memset(&ev, 0, sizeof ev);
        ev.byte_offset = (size_t)-1;
        CHECK(xml_next(&p, &ev) == XML_EV_ERROR);
        CHECK(ev.byte_offset == err_at);
    }

    arm_fsync_cancel = 1;
    char kept[] = "/tmp/ki-fsync-kept-XXXXXX";
    fd = mkstemp(kept);
    CHECK(fd >= 0);
    CHECK(write(fd, "PREVIOUS", 8) == 8);
    close(fd);
    ki_options_init(&opt);
    opt.cancelled = cancel_after_fsync;
    CHECK(ki_convert_file("examples/hello.xml", kept, &opt, &result) == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_CANCEL);
    {
        FILE *fp = fopen(kept, "rb");
        char old[9] = {0};
        CHECK(fp != NULL && fread(old, 1, 8, fp) == 8);
        fclose(fp);
        CHECK(memcmp(old, "PREVIOUS", 8) == 0);
    }
    arm_fsync_cancel = 0;
    CHECK(ki_convert_file("examples/hello.xml", kept, NULL, &result) == 0);
    unlink(kept);
    unlink(out);
    return 0;
}

static int mem_cancel_stop(void *user) {
    return ((MemIo *)user)->stop != 0;
}

/* Synthetic TTF: directory then head, hhea, hmtx, cmap in that order, so a
   read past the cmap table also runs past the heap copy of the file. */
#define TTF_DIR 76
#define TTF_CAP 1024

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void put32(unsigned char *p, unsigned long v) {
    put16(p, (unsigned)(v >> 16) & 0xFFFFu);
    put16(p + 2, (unsigned)v & 0xFFFFu);
}

static size_t ttf_build(unsigned char *out, size_t head_len, size_t hhea_len, unsigned num_h,
                        size_t hmtx_len, const unsigned char *cmap, size_t cmap_len) {
    const char *tags[4] = {"head", "hhea", "hmtx", "cmap"};
    size_t lens[4] = {head_len, hhea_len, hmtx_len, cmap_len};
    memset(out, 0, TTF_CAP);
    put32(out, 0x00010000ul);
    put16(out + 4, 4);
    size_t at = TTF_DIR;
    for (int i = 0; i < 4; i++) {
        unsigned char *e = out + 12 + 16 * i;
        memcpy(e, tags[i], 4);
        put32(e + 8, at);
        put32(e + 12, lens[i]);
        unsigned char *t = out + at;
        if (i == 0 && head_len >= 20) {
            put16(t + 18, 2048);
        } else if (i == 1 && hhea_len >= 36) {
            put16(t + 4, 1900);
            put16(t + 6, 0xFE00);
            put16(t + 34, num_h);
        } else if (i == 2) {
            for (size_t m = 0; m + 2 <= hmtx_len; m += 4) {
                put16(t + m, 500);
            }
        } else if (i == 3) {
            memcpy(t, cmap, cmap_len);
        }
        at += lens[i];
    }
    return at;
}

/* 40-byte (3,1) format 4 body: U+0020 -> 1, U+0041 -> 2, terminal 0xFFFF. */
static size_t f4_body(unsigned char *p) {
    const unsigned ends[3] = {0x20, 0x41, 0xFFFF};
    const unsigned deltas[3] = {(1u - 0x20u) & 0xFFFFu, (2u - 0x41u) & 0xFFFFu, 1};
    memset(p, 0, 40);
    put16(p, 4);
    put16(p + 2, 40);
    put16(p + 6, 6);
    put16(p + 8, 4);
    put16(p + 10, 1);
    put16(p + 12, 2);
    for (int i = 0; i < 3; i++) {
        put16(p + 14 + 2 * i, ends[i]);
        put16(p + 22 + 2 * i, ends[i]);
        put16(p + 28 + 2 * i, deltas[i]);
    }
    return 40;
}

static size_t cmap_head(unsigned char *p, unsigned num) {
    put16(p, 0);
    put16(p + 2, num);
    return 4;
}

static void cmap_rec(unsigned char *p, unsigned plat, unsigned enc, unsigned long rel) {
    put16(p, plat);
    put16(p + 2, enc);
    put32(p + 4, rel);
}

static size_t valid_cmap(unsigned char *p) {
    cmap_head(p, 1);
    cmap_rec(p + 4, 3, 1, 12);
    return 12 + f4_body(p + 12);
}

static int load_font_bytes(const unsigned char *b, size_t n) {
    char path[] = "/tmp/ki-font-bounds-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return -2;
    }
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, b + done, n - done);
        if (w <= 0) {
            close(fd);
            unlink(path);
            return -2;
        }
        done += (size_t)w;
    }
    close(fd);
    FontFace face;
    int rc = font_load(&face, path);
    if (rc == 0) {
        font_free(&face);
    }
    unlink(path);
    return rc;
}

static int std_font(unsigned char *ttf, const unsigned char *cmap, size_t cmap_len) {
    return load_font_bytes(ttf, ttf_build(ttf, 54, 36, 3, 12, cmap, cmap_len));
}

static int test_font_bounds(void) {
    static unsigned char ttf[TTF_CAP];
    unsigned char cmap[256];
    size_t cl, n;

    /* Positive control: cmap ends exactly at the table and file end. */
    cl = valid_cmap(cmap);
    CHECK(std_font(ttf, cmap, cl) == 0);
    n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
    {
        FontFace face;
        char path[] = "/tmp/ki-font-bounds-XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0);
        CHECK(write(fd, ttf, n) == (ssize_t)n);
        close(fd);
        CHECK(font_load(&face, path) == 0);
        uint16_t gid = 0, adv = 0;
        CHECK(font_lookup(&face, 'A', &gid, &adv) == 0 && gid == 2 && adv == 500);
        CHECK(font_lookup(&face, ' ', &gid, &adv) == 0 && gid == 1);
        CHECK(font_lookup(&face, 'B', &gid, &adv) != 0);
        font_free(&face);
        unlink(path);
    }

    /* Only (3,1) record; its rel walks len-4..len on a 16-byte final cmap,
       with format 4 written at rel wherever two bytes remain. */
    for (unsigned rel = 12; rel <= 16; rel++) {
        memset(cmap, 0, 16);
        cmap_head(cmap, 1);
        cmap_rec(cmap + 4, 3, 1, rel);
        if (rel + 2 <= 16) {
            put16(cmap + rel, 4);
        }
        if (rel + 4 <= 16) {
            put16(cmap + rel + 2, 40);
        }
        CHECK(std_font(ttf, cmap, 16) != 0);
    }
    /* len-4 is fine when a complete header there is simply not format 4. */
    cmap_head(cmap, 2);
    cmap_rec(cmap + 4, 3, 1, 20);
    f4_body(cmap + 20);
    put16(cmap + 60, 12);
    put16(cmap + 62, 0);
    for (unsigned long rel = 60; rel <= 64; rel++) {
        cmap_rec(cmap + 12, 3, 1, rel);
        CHECK(std_font(ttf, cmap, 64) == 0);
        cmap_rec(cmap + 12, 0, 3, rel);
        CHECK(std_font(ttf, cmap, 64) == 0);
    }
    /* ...but a selected format 4 there with an impossible length rejects. */
    cmap_rec(cmap + 12, 3, 1, 60);
    put16(cmap + 60, 4);
    put16(cmap + 62, 40);
    CHECK(std_font(ttf, cmap, 64) != 0);

    /* Near-maximum subtable offsets. */
    const unsigned long rels[] = {0xFFFFFFFFul, 0xFFFFFFFCul, 0xFFFFFFFEul, 0x80000000ul, 53, 52};
    for (size_t i = 0; i < sizeof rels / sizeof rels[0]; i++) {
        cl = valid_cmap(cmap);
        cmap_rec(cmap + 4, 3, 1, rels[i]);
        CHECK(std_font(ttf, cmap, cl) != 0);
    }

    /* Truncated cmap header and encoding records. */
    cl = valid_cmap(cmap);
    for (size_t len = 0; len < 12; len++) {
        CHECK(std_font(ttf, cmap, len) != 0);
    }
    cmap_head(cmap, 0xFFFF);
    CHECK(std_font(ttf, cmap, cl) != 0);
    cmap_head(cmap, 2);
    CHECK(std_font(ttf, cmap, 19) != 0);

    /* Declared subtable length: undersized, past the table, exact. */
    const unsigned sublens[] = {0, 13, 41, 0xFFFF};
    for (size_t i = 0; i < sizeof sublens / sizeof sublens[0]; i++) {
        cl = valid_cmap(cmap);
        put16(cmap + 14, sublens[i]);
        CHECK(std_font(ttf, cmap, cl) != 0);
    }
    cl = valid_cmap(cmap);
    CHECK(std_font(ttf, cmap, cl - 1) != 0);

    /* Segment count and idRangeOffset beyond the subtable. */
    cl = valid_cmap(cmap);
    put16(cmap + 12 + 6, 0xFFFE);
    CHECK(std_font(ttf, cmap, cl) != 0);
    cl = valid_cmap(cmap);
    put16(cmap + 12 + 34 + 2, 0xFFFE);
    CHECK(std_font(ttf, cmap, cl) != 0);
    cl = valid_cmap(cmap);
    put16(cmap + 12 + 34 + 2, 4);
    CHECK(std_font(ttf, cmap, cl) != 0);

    /* Table directory and table spans. */
    cl = valid_cmap(cmap);
    n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
    for (size_t len = 0; len < TTF_DIR; len += 5) {
        CHECK(load_font_bytes(ttf, len) != 0);
    }
    CHECK(load_font_bytes(ttf, TTF_DIR - 1) != 0);
    CHECK(load_font_bytes(ttf, n - 1) != 0);
    put16(ttf + 4, 0xFFFF);
    CHECK(load_font_bytes(ttf, n) != 0);
    const size_t cmap_entry = 12 + 16 * 3;
    const unsigned long bad_off[] = {0xFFFFFFFFul, 0xFFFFFFF0ul, 0x80000000ul};
    for (size_t i = 0; i < sizeof bad_off / sizeof bad_off[0]; i++) {
        n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
        put32(ttf + cmap_entry + 8, bad_off[i]);
        CHECK(load_font_bytes(ttf, n) != 0);
        n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
        put32(ttf + cmap_entry + 12, bad_off[i]);
        CHECK(load_font_bytes(ttf, n) != 0);
    }
    n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
    put32(ttf + cmap_entry + 12, cl + 1);
    CHECK(load_font_bytes(ttf, n) != 0);
    put32(ttf + cmap_entry + 8, n + 1);
    put32(ttf + cmap_entry + 12, 0);
    CHECK(load_font_bytes(ttf, n) != 0);

    /* head, hhea and hmtx sizes. */
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 53, 36, 3, 12, cmap, cl)) != 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 35, 3, 12, cmap, cl)) != 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 36, 3, 11, cmap, cl)) != 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 36, 0, 12, cmap, cl)) != 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 36, 0xFFFF, 12, cmap, cl)) != 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 36, 1, 4, cmap, cl)) == 0);
    CHECK(load_font_bytes(ttf, ttf_build(ttf, 54, 36, 3, 16, cmap, cl)) == 0);
    CHECK(font_load(&(FontFace){0}, "tests/corpus/truncated-hmtx.ttf") != 0);

    /* 8 MiB file cap stays inclusive. */
    {
        size_t cap = 8u * 1024u * 1024u;
        unsigned char *big = calloc(cap + 1, 1);
        CHECK(big != NULL);
        n = ttf_build(ttf, 54, 36, 3, 12, cmap, cl);
        memcpy(big, ttf, n);
        int at_cap = load_font_bytes(big, cap);
        int over_cap = load_font_bytes(big, cap + 1);
        free(big);
        CHECK(at_cap == 0);
        CHECK(over_cap != 0);
    }

    /* The exact 184-byte truncated cmap through loader and public API. */
    const char *bad = "tests/corpus/truncated-cmap-184.ttf";
    struct stat st;
    CHECK(stat(bad, &st) == 0 && st.st_size == 184);
    FontFace face;
    CHECK(font_load(&face, bad) != 0);
    setenv("KI_XML_PDF_FONT", bad, 1);
    CHECK(font_open_default(&face) != 0);
    unsetenv("KI_XML_PDF_FONT");

    char dir[] = "/tmp/ki-font-bounds-dir-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char dest[256];
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    const char *old = "OLD_FONT_DEST";
    CHECK(write_all(dest, old, strlen(old)) == 0);
    KiOptions opt;
    KiResult result;
    ki_options_init(&opt);
    opt.font_path = bad;
    opt.temp_dir = dir;
    CHECK(ki_convert_file("tests/fixture.xml", dest, &opt, &result) == XML2PDF_ERR_XML);
    CHECK(result.status == XML2PDF_ERR_XML && result.cause == KI_CAUSE_FONT);
    CHECK(same_bytes(dest, old, strlen(old)));
    CHECK(callback_directory_entries(dir) == 1);

    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = "<doc>Hello</doc>";
    io.n = strlen(io.s);
    io.chunk = 1;
    ki_options_init(&opt);
    opt.font_path = bad;
    opt.temp_dir = dir;
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    CHECK(ki_convert(&opt, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    CHECK(io.calls == 0 && io.len == 0);
    free(io.buf);

    memset(&io, 0, sizeof io);
    io.s = "<doc>Hello</doc>";
    io.n = strlen(io.s);
    io.chunk = 1;
    CHECK(ki_convert_to_file(dest, &opt, &result) == XML2PDF_ERR_XML);
    CHECK(result.cause == KI_CAUSE_FONT);
    CHECK(io.calls == 0);
    CHECK(same_bytes(dest, old, strlen(old)));
    CHECK(callback_directory_entries(dir) == 1);

    /* The same destination still publishes with the bundled font. */
    opt.font_path = "fonts/DejaVuSans.ttf";
    memset(&io, 0, sizeof io);
    io.s = "<doc>Hello</doc>";
    io.n = strlen(io.s);
    CHECK(ki_convert_to_file(dest, &opt, &result) == XML2PDF_OK);
    CHECK(callback_directory_entries(dir) == 1);
    unlink(dest);
    rmdir(dir);
    return 0;
}

#include "test_xml_subset.c"
#include "test_font_metrics.c"
#include "test_pdf_bounds.c"
#include "test_flate.c"

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"big_stream_fixed_memory", test_big_stream_fixed_memory},
        {"split_mid_tag_and_text", test_split_mid_tag_and_text},
        {"one_byte_entities_and_nesting", test_one_byte_entities_and_nesting},
        {"skips_and_errors", test_skips_and_errors},
        {"cdata_and_numeric", test_cdata_and_numeric},
        {"cdata_stream_fixed_memory", test_cdata_stream_fixed_memory},
        {"page_sink_flushes", test_page_sink_flushes},
        {"pdf_appends_and_releases", test_pdf_appends_and_releases},
        {"convert_pipeline", test_convert_pipeline},
        {"output_keeps_previous", test_output_keeps_previous},
        {"wellformed_profile", test_wellformed_profile},
        {"review_regressions", test_review_regressions},
        {"library_contract", test_library_contract},
        {"callback_usage_result", test_callback_usage_result},
        {"callback_to_file", test_callback_to_file},
        {"report_v1", test_report_v1},
        {"font_bounds", test_font_bounds},
        {"font_names", test_font_names},
        {"font_zero_advance", test_font_zero_advance},
        {"font_mark_runs", test_font_mark_runs},
        {"xml_char_contexts", test_xml_char_contexts},
        {"xml_references", test_xml_references},
        {"xml_audited_corpus", test_xml_audited_corpus},
        {"xml_pi_and_declaration", test_xml_pi_and_declaration},
        {"xml_doctype_and_dtd", test_xml_doctype_and_dtd},
        {"xml_late_tails", test_xml_late_tails},
        {"xml_chunk_boundaries", test_xml_chunk_boundaries},
        {"xml_public_causes", test_xml_public_causes},
        {"xml_selected_tails", test_xml_selected_tails},
        {"pdf_byte_ceiling", test_pdf_byte_ceiling},
        {"output_quota_exact", test_output_quota_exact},
        {"flate_streams", test_flate_streams},
        {"flate_expansion", test_flate_expansion},
        {"flate_errors", test_flate_errors},
        {"flate_reader_controls", test_flate_reader_controls},
    };
    int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; i++) {
        int rc = tests[i].fn();
        if (rc != 0) {
            fprintf(stderr, "FAILED %s\n", tests[i].name);
            failures++;
        } else {
            printf("ok %s\n", tests[i].name);
        }
    }
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
