/* Old-header early-error probe. Compiled against the preserved 0.2.0
   header and linked against the preserved 0.2.0 shared library, then run
   unchanged against a replacement libki_xml_pdf.so.0 via LD_LIBRARY_PATH.
   Guard bytes surround KiOptions and KiResult; poisoned results must come
   back with only the current status/cause/message and zero metrics. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ki_xml_pdf.h"

#define GUARD 64
static int failures;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); failures++; } else { printf("ok %s\n", what); } } while (0)

typedef struct { unsigned char pre[GUARD]; KiOptions v; unsigned char post[GUARD]; } GOpt;
typedef struct { unsigned char pre[GUARD]; KiResult v; unsigned char post[GUARD]; } GRes;

static int guards_ok(const unsigned char *a, const unsigned char *b) {
    for (int i = 0; i < GUARD; i++) {
        if (a[i] != 0xA5 || b[i] != 0xA5) return 0;
    }
    return 1;
}

static void poison(GRes *r) {
    memset(r, 0xA5, sizeof *r);
    r->v.pages = 99;
    r->v.output_bytes = 12345;
    r->v.report_selected = 88;
}

static int empty(const KiResult *r) {
    return r->pages == 0 && r->objects == 0 && r->offset_bytes == 0 && r->output_bytes == 0 &&
           r->spool_bytes == 0 && r->report_total == 0 && r->report_selected == 0 && r->report_skipped == 0;
}

static int calls;
static int rd(void *u, char *d, int m) { (void)u; (void)d; (void)m; calls++; return 0; }
static int wr(void *u, const char *s, int n) { (void)u; (void)s; calls++; return n; }

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/tmp";
    printf("header %s sizeof KiOptions=%zu KiResult=%zu KiEvent=%zu\n", KI_XML_PDF_VERSION, sizeof(KiOptions),
           sizeof(KiResult), sizeof(KiEvent));
    GOpt o;
    GRes r;
    memset(&o, 0xA5, sizeof o);
    ki_options_init(&o.v);
    CHECK(guards_ok(o.pre, o.post) && o.v.size == sizeof(KiOptions), "ki_options_init writes only KiOptions");

    poison(&r);
    o.v.size = 1;
    o.v.read_bytes = rd;
    o.v.write_bytes = wr;
    calls = 0;
    int rc = ki_convert(&o.v, &r.v);
    CHECK(rc == 2 && r.v.status == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && calls == 0 &&
          guards_ok(r.pre, r.post), "ki_convert size mismatch: USAGE, zero metrics, no callback");

    poison(&r);
    ki_options_init(&o.v);
    o.v.max_pages = -1;
    o.v.read_bytes = rd;
    o.v.write_bytes = wr;
    calls = 0;
    rc = ki_convert(&o.v, &r.v);
    CHECK(rc == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && calls == 0 && guards_ok(r.pre, r.post),
          "ki_convert negative quota: USAGE before callbacks");

    poison(&r);
    rc = ki_convert(NULL, &r.v);
    CHECK(rc == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && guards_ok(r.pre, r.post),
          "ki_convert NULL options: USAGE");

    char dest[512];
    snprintf(dest, sizeof dest, "%s/early-absent.pdf", dir);
    unlink(dest);
    poison(&r);
    ki_options_init(&o.v);
    o.v.record_limit = -5;
    rc = ki_convert_file("/nonexistent-input.xml", dest, &o.v, &r.v);
    CHECK(rc == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && access(dest, F_OK) != 0 &&
          guards_ok(r.pre, r.post), "ki_convert_file invalid selection: USAGE, nothing created");

    poison(&r);
    ki_options_init(&o.v);
    o.v.read_bytes = NULL;
    rc = ki_convert_to_file(dest, &o.v, &r.v);
    CHECK(rc == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && access(dest, F_OK) != 0 &&
          guards_ok(r.pre, r.post), "ki_convert_to_file without read_bytes: USAGE, nothing created");

    poison(&r);
    ki_options_init(&o.v);
    o.v.profile = 77;
    o.v.read_bytes = rd;
    o.v.write_bytes = wr;
    calls = 0;
    rc = ki_convert(&o.v, &r.v);
    CHECK(rc == 2 && r.v.cause == KI_CAUSE_USAGE && empty(&r.v) && calls == 0 && guards_ok(r.pre, r.post),
          "ki_convert unknown profile: USAGE before callbacks");

    CHECK(guards_ok(o.pre, o.post), "KiOptions guard bytes intact after all calls");
    printf("%s failures=%d\n", failures ? "early probe FAILED" : "early probe ok", failures);
    return failures != 0;
}
