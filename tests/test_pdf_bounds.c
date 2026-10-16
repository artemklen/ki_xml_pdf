/* Output accounting and classic xref bounds. Included by test_stream.c.

   A 10 GB PDF is not practical in a unit test, so the writer-level cases
   start a real writer and then move its byte counter close to the xref
   ceiling. The memstream sink receives only the bytes written after that
   point; positions in it are offset by the jump. */

#include <limits.h>

/* First page object number in pdf_write.c's fixed layout. */
#define OBJ_FIRST_TEST 8

typedef struct {
    PdfWriter w;
    FILE *sink;
    char *buf;
    size_t len;
    long base;
} BoundWriter;

static int bound_open(BoundWriter *b, long max_output) {
    memset(b, 0, sizeof *b);
    b->sink = open_memstream(&b->buf, &b->len);
    CHECK(b->sink != NULL);
    PdfConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.font_path = "fonts/DejaVuSans.ttf";
    cfg.max_output = max_output;
    CHECK(pdf_begin_cfg(&b->w, b->sink, &cfg) == 0);
    CHECK(fflush(b->w.fp) == 0);
    CHECK(fflush(b->sink) == 0);
    CHECK((long)b->len == b->w.written);
    b->base = b->w.written;
    return 0;
}

static size_t bound_sink_len(BoundWriter *b) {
    fflush(b->sink);
    return b->len;
}

static void bound_close(BoundWriter *b) {
    pdf_free(&b->w);
    fclose(b->sink);
    free(b->buf);
}

static int bound_page_and_end(BoundWriter *b) {
    if (pdf_add_page(&b->w, "BOUND", 5) != 0) {
        return -1;
    }
    return pdf_end(&b->w);
}

/* Checks the xref written after a counter jump. sink_at is the logical
   output position of sink byte 0. Every in-sink offset must point at its
   object header; offsets before the jump only need to be plausible. */
static int check_jumped_xref(const char *buf, size_t n, long long sink_at, long long total) {
    const char *key = "startxref\n";
    const char *at = NULL;
    for (size_t i = 0; i + 10 <= n; i++) {
        if (memcmp(buf + i, key, 10) == 0) {
            at = buf + i;
        }
    }
    CHECK(at != NULL);
    char *end = NULL;
    long long xref = strtoll(at + 10, &end, 10);
    CHECK(end != NULL && *end == '\n');
    CHECK(end - (at + 10) <= 10);
    CHECK(xref >= sink_at && xref <= PDF_XREF_MAX_OFFSET && xref < total);
    const char *p = buf + (size_t)(xref - sink_at);
    CHECK(memcmp(p, "xref\n0 ", 7) == 0);
    int count = 0;
    CHECK(sscanf(p + 7, "%d", &count) == 1 && count > OBJ_FIRST_TEST);
    p = strchr(p + 7, '\n') + 1;
    for (int i = 0; i < count; i++) {
        CHECK(p + 20 <= buf + n);
        CHECK(p[10] == ' ' && p[16] == ' ' && p[18] == ' ' && p[19] == '\n');
        for (int d = 0; d < 10; d++) {
            CHECK(p[d] >= '0' && p[d] <= '9');
        }
        long long off = strtoll(p, NULL, 10);
        CHECK(off <= PDF_XREF_MAX_OFFSET);
        if (i > 0 && off >= sink_at) {
            char expect[32];
            int el = snprintf(expect, sizeof expect, "%d 0 obj\n", i);
            CHECK(memcmp(buf + (size_t)(off - sink_at), expect, (size_t)el) == 0);
        }
        p += 20;
    }
    CHECK(memcmp(p, "trailer\n", 8) == 0);
    CHECK(n >= 6 && memcmp(buf + n - 6, "%%EOF\n", 6) == 0);
    return 0;
}

static int test_pdf_byte_ceiling(void) {
    const long long ceil = PDF_XREF_MAX_OFFSET;
    CHECK(ceil == 9999999999LL);
    CHECK(pdf_byte_ceiling() == (LONG_MAX < ceil ? LONG_MAX : (long)ceil));
    if (LONG_MAX < ceil) {
        return 0;
    }
    BoundWriter b;

    /* Counter at 9,999,999,998: two bytes fit, the third does not. */
    CHECK(bound_open(&b, 0) == 0);
    b.w.written = (long)(ceil - 2);
    CHECK(fputs("ab", b.w.fp) >= 0 && fflush(b.w.fp) == 0);
    CHECK(b.w.written == (long)ceil);
    CHECK(bound_sink_len(&b) == (size_t)b.base + 2);
    CHECK(fputc('c', b.w.fp) != EOF);
    CHECK(fflush(b.w.fp) != 0);
    CHECK(b.w.written == (long)ceil);
    CHECK(b.w.fail_kind == PDF_FAIL_XREF && b.w.hit_limit);
    CHECK(bound_sink_len(&b) == (size_t)b.base + 2);
    bound_close(&b);

    /* Counter at 9,999,999,999: a two-byte chunk is rejected whole. */
    CHECK(bound_open(&b, 0) == 0);
    b.w.written = (long)(ceil - 1);
    CHECK(fputs("ab", b.w.fp) >= 0);
    CHECK(fflush(b.w.fp) != 0);
    CHECK(b.w.written == (long)(ceil - 1));
    CHECK(b.w.fail_kind == PDF_FAIL_XREF);
    CHECK(bound_sink_len(&b) == (size_t)b.base);
    bound_close(&b);

    /* An object may start at exactly 9,999,999,999 but its header can't be
       emitted; nothing reaches the sink. */
    CHECK(bound_open(&b, 0) == 0);
    b.w.written = (long)ceil;
    CHECK(pdf_add_page(&b.w, "X", 1) != 0);
    CHECK(b.w.fail_kind == PDF_FAIL_XREF && b.w.written == (long)ceil);
    CHECK(bound_sink_len(&b) == (size_t)b.base);
    bound_close(&b);

    /* An offset of 10,000,000,000 is rejected at registration, before any
       byte of the object is buffered. */
    CHECK(bound_open(&b, 0) == 0);
    b.w.written = (long)(ceil + 1);
    CHECK(pdf_add_page(&b.w, "X", 1) != 0);
    CHECK(b.w.fail_kind == PDF_FAIL_XREF && b.w.hit_limit);
    CHECK(b.w.nobj == OBJ_FIRST_TEST - 1);
    CHECK(bound_sink_len(&b) == (size_t)b.base);
    bound_close(&b);

    /* Reference tail: one page plus finalization from a fresh writer. */
    CHECK(bound_open(&b, 0) == 0);
    long base = b.base;
    CHECK(bound_page_and_end(&b) == 0);
    long tail = b.w.written - base;
    CHECK(bound_sink_len(&b) == (size_t)b.w.written);
    CHECK(check_xref(b.buf, b.len) == 0);
    bound_close(&b);
    CHECK(tail > 100);

    /* startxref gains digits near the ceiling, so measure the tail again
       from a nearby start with spare room. */
    CHECK(bound_open(&b, 0) == 0);
    long near = (long)(ceil - tail - 1000);
    b.w.written = near;
    CHECK(bound_page_and_end(&b) == 0);
    tail = b.w.written - near;
    bound_close(&b);

    /* The whole late tail lands exactly on the ceiling: xref, trailer and
       EOF all fit, and every offset has ten digits. A user quota of 0 and
       one above the ceiling behave the same. */
    const long quotas[] = {0, LONG_MAX};
    for (size_t q = 0; q < sizeof quotas / sizeof quotas[0]; q++) {
        CHECK(bound_open(&b, quotas[q]) == 0);
        long long start = ceil - tail;
        b.w.written = (long)start;
        CHECK(bound_page_and_end(&b) == 0);
        CHECK(b.w.written == (long)ceil);
        size_t n = bound_sink_len(&b);
        CHECK(n == (size_t)(b.base + tail));
        CHECK(check_jumped_xref(b.buf + b.base, n - (size_t)b.base, start, ceil) == 0);
        bound_close(&b);

        /* One byte later the same tail fails RESOURCE without crossing. */
        CHECK(bound_open(&b, quotas[q]) == 0);
        b.w.written = (long)(start + 1);
        CHECK(bound_page_and_end(&b) != 0);
        CHECK(b.w.fail_kind == PDF_FAIL_XREF && b.w.hit_limit);
        CHECK(b.w.written <= (long)ceil);
        n = bound_sink_len(&b);
        CHECK((long long)(n - (size_t)b.base) == b.w.written - (start + 1));
        pdf_free(&b.w);
        CHECK(b.w.written <= (long)ceil);
        CHECK((long long)(bound_sink_len(&b) - (size_t)b.base) == b.w.written - (start + 1));
        fclose(b.sink);
        free(b.buf);
    }

    /* A user quota below the ceiling reports the quota, not the xref. */
    CHECK(bound_open(&b, (long)(ceil - 10)) == 0);
    b.w.written = (long)(ceil - 12);
    CHECK(fputs("abc", b.w.fp) >= 0);
    CHECK(fflush(b.w.fp) != 0);
    CHECK(b.w.fail_kind == PDF_FAIL_QUOTA && b.w.written == (long)(ceil - 12));
    bound_close(&b);

    /* Object counter maximum still fails RESOURCE before the ceiling. */
    CHECK(bound_open(&b, 0) == 0);
    b.w.npages = (8000000 - OBJ_FIRST_TEST) / 2;
    CHECK(pdf_add_page(&b.w, "X", 1) != 0);
    CHECK(b.w.fail_kind == PDF_FAIL_LIMIT && b.w.hit_limit);
    bound_close(&b);
    return 0;
}

typedef struct {
    int status;
    int cause;
    long output;
    size_t delivered;
    char *bytes;
} QuotaRun;

static int quota_run(const char *xml, long quota, int fail_at, QuotaRun *run) {
    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = xml;
    io.n = strlen(xml);
    io.fail_at = fail_at;
    KiOptions opt;
    KiResult result;
    ki_options_init(&opt);
    opt.font_path = "fonts/DejaVuSans.ttf";
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    opt.max_output_bytes = quota;
    int rc = ki_convert(&opt, &result);
    run->status = rc;
    run->cause = result.cause;
    run->output = result.output_bytes;
    run->delivered = io.len;
    run->bytes = io.buf;
    CHECK(rc == result.status);
    return 0;
}

static int count_calls(const char *xml) {
    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = xml;
    io.n = strlen(xml);
    KiOptions opt;
    KiResult result;
    ki_options_init(&opt);
    opt.font_path = "fonts/DejaVuSans.ttf";
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    if (ki_convert(&opt, &result) != XML2PDF_OK) {
        free(io.buf);
        return -1;
    }
    free(io.buf);
    return io.calls;
}

static long find_startxref(const char *buf, size_t n) {
    for (size_t i = n; i-- > 0;) {
        if (i + 10 <= n && memcmp(buf + i, "startxref\n", 10) == 0) {
            return strtol(buf + i + 10, NULL, 10);
        }
    }
    return -1;
}

static int test_output_quota_exact(void) {
    const char *xml = "<doc><p>Quota one</p><p>Quota two</p></doc>";
    QuotaRun ref;
    CHECK(quota_run(xml, 0, 0, &ref) == 0);
    CHECK(ref.status == XML2PDF_OK);
    long total = ref.output;
    CHECK(total > 0 && (size_t)total == ref.delivered);
    CHECK(check_xref(ref.bytes, ref.delivered) == 0);
    long xref_at = find_startxref(ref.bytes, ref.delivered);
    CHECK(xref_at > 0 && xref_at < total);

    QuotaRun run;
    CHECK(quota_run(xml, total, 0, &run) == 0);
    CHECK(run.status == XML2PDF_OK && run.output == total);
    CHECK(run.delivered == (size_t)total && memcmp(run.bytes, ref.bytes, (size_t)total) == 0);
    free(run.bytes);

    /* N-1, a cut inside the trailer, and quotas ending at and just after
       the start of the xref section all fail during finalization. */
    const long late[] = {total - 1, total - 8, xref_at, xref_at + 1, xref_at - 1};
    for (size_t i = 0; i < sizeof late / sizeof late[0]; i++) {
        CHECK(quota_run(xml, late[i], 0, &run) == 0);
        CHECK(run.status == XML2PDF_ERR_RESOURCE && run.cause == KI_CAUSE_RESOURCE);
        CHECK(run.output <= late[i]);
        CHECK(run.delivered == (size_t)run.output);
        CHECK(run.delivered == 0 || memcmp(run.bytes, ref.bytes, run.delivered) == 0);
        free(run.bytes);
    }

    /* A sink that fails on its last call never becomes success. */
    int calls = count_calls(xml);
    CHECK(calls > 0);
    CHECK(quota_run(xml, 0, calls, &run) == 0);
    CHECK(run.status == XML2PDF_ERR_IO && run.cause == KI_CAUSE_IO);
    CHECK(run.delivered < (size_t)total);
    free(run.bytes);

    /* File destination: N publishes exactly N bytes, N-1 keeps the old file. */
    char dir[] = "/tmp/ki-quota-exact-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char src[256];
    char dest[256];
    snprintf(src, sizeof src, "%s/in.xml", dir);
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    CHECK(write_all(src, xml, strlen(xml)) == 0);
    const char *old = "OLD_QUOTA_DEST";
    CHECK(write_all(dest, old, strlen(old)) == 0);
    KiOptions opt;
    KiResult result;
    ki_options_init(&opt);
    opt.font_path = "fonts/DejaVuSans.ttf";
    opt.temp_dir = dir;
    opt.max_output_bytes = total - 1;
    CHECK(ki_convert_file(src, dest, &opt, &result) == XML2PDF_ERR_RESOURCE);
    CHECK(result.cause == KI_CAUSE_RESOURCE && result.output_bytes <= total - 1);
    CHECK(same_bytes(dest, old, strlen(old)));
    CHECK(callback_directory_entries(dir) == 2);
    opt.max_output_bytes = total;
    CHECK(ki_convert_file(src, dest, &opt, &result) == XML2PDF_OK);
    CHECK(result.output_bytes == total);
    CHECK(same_bytes(dest, ref.bytes, (size_t)total));
    free(ref.bytes);
    unlink(dest);
    unlink(src);
    rmdir(dir);
    return 0;
}
