/* One-shot output callback faults. A single negative, short, zero or
   over-count write_bytes return must fail ki_convert and ki_convert_cfg
   with KI_CAUSE_IO even when every later call would succeed. The callback
   is not called again after the fault, the accepted bytes are an exact
   prefix of the reference PDF and output_bytes counts exactly them. The
   same options and result are then reused for a valid conversion.
   Included from test_stream.c (CHECK, check_xref). */

typedef struct {
    const char *xml;
    size_t n, i;
    char *buf;
    size_t len, cap;
    int calls;
    int fault_at;
    int persistent;
    int mode; /* 1: -1; 2: accept n/2; 3: accept 0; 4: claim n + 1 */
    int faults;
} OsIo;

static int os_read(void *user, char *dst, int max) {
    OsIo *io = user;
    size_t left = io->n - io->i;
    size_t step = left < 7 ? left : 7;
    if ((size_t)max < step) {
        step = (size_t)max;
    }
    memcpy(dst, io->xml + io->i, step);
    io->i += step;
    return (int)step;
}

static int os_keep(OsIo *io, const char *src, int n) {
    if (io->len + (size_t)n > io->cap) {
        size_t cap = io->cap == 0 ? 64 * 1024 : io->cap;
        while (cap < io->len + (size_t)n) {
            cap *= 2;
        }
        char *grown = realloc(io->buf, cap);
        if (grown == NULL) {
            return -1;
        }
        io->buf = grown;
        io->cap = cap;
    }
    memcpy(io->buf + io->len, src, (size_t)n);
    io->len += (size_t)n;
    return n;
}

static int os_write(void *user, const char *src, int n) {
    OsIo *io = user;
    io->calls++;
    int hit = io->fault_at > 0 &&
        (io->persistent ? io->calls >= io->fault_at : io->calls == io->fault_at);
    if (!hit) {
        return os_keep(io, src, n);
    }
    io->faults++;
    switch (io->mode) {
        case 1: return -1;
        case 2: return n > 1 ? os_keep(io, src, n / 2) : 0;
        case 3: return 0;
        default: return n + 1;
    }
}

static void os_reset(OsIo *io, const char *xml) {
    free(io->buf);
    memset(io, 0, sizeof *io);
    io->xml = xml;
    io->n = strlen(xml);
}

static int os_run(int use_cfg, KiOptions *opt, KiResult *result) {
    if (!use_cfg) {
        return ki_convert(opt, result);
    }
    KiConfig cfg;
    if (ki_config_init(&cfg, sizeof cfg) != 0) {
        return -100;
    }
    return ki_convert_cfg(opt, &cfg, result);
}

static int test_callback_one_shot_write(void) {
    char xml[16384];
    size_t used = (size_t)snprintf(xml, sizeof xml, "<doc>");
    for (int i = 0; i < 160; i++) {
        used += (size_t)snprintf(xml + used, sizeof xml - used, "<p>line %03d lifecycle words</p>", i);
    }
    used += (size_t)snprintf(xml + used, sizeof xml - used, "</doc>");
    CHECK(used < sizeof xml);

    OsIo io;
    memset(&io, 0, sizeof io);
    KiOptions opt;
    ki_options_init(&opt);
    opt.read_bytes = os_read;
    opt.write_bytes = os_write;
    opt.io_user = &io;

    for (int use_cfg = 0; use_cfg < 2; use_cfg++) {
        os_reset(&io, xml);
        KiResult ref_result;
        CHECK(os_run(use_cfg, &opt, &ref_result) == XML2PDF_OK);
        CHECK(ref_result.status == XML2PDF_OK && ref_result.cause == KI_CAUSE_NONE);
        CHECK(ref_result.pages > 1 && ref_result.output_bytes == (long)io.len);
        CHECK(check_xref(io.buf, io.len) == 0);
        int ref_calls = io.calls;
        CHECK(ref_calls >= 4);
        size_t ref_len = io.len;
        char *ref = io.buf;
        io.buf = NULL;

        const int points[] = {1, 2, ref_calls / 2, ref_calls};
        for (size_t p = 0; p < sizeof points / sizeof points[0]; p++) {
            for (int mode = 1; mode <= 4; mode++) {
                for (int persistent = 0; persistent < 2; persistent++) {
                    for (int with_result = 0; with_result < 2; with_result++) {
                        os_reset(&io, xml);
                        io.fault_at = points[p];
                        io.mode = mode;
                        io.persistent = persistent;
                        KiResult result;
                        memset(&result, 0x5a, sizeof result);
                        int rc = os_run(use_cfg, &opt, with_result ? &result : NULL);
                        if (rc != XML2PDF_ERR_IO || io.calls != points[p] || io.faults != 1) {
                            fprintf(stderr, "one-shot cfg=%d at=%d mode=%d persistent=%d result=%d: rc=%d calls=%d\n",
                                    use_cfg, points[p], mode, persistent, with_result, rc, io.calls);
                        }
                        CHECK(rc == XML2PDF_ERR_IO);
                        /* The fault is latched: the callback is not called again. */
                        CHECK(io.calls == points[p] && io.faults == 1);
                        CHECK(io.len < ref_len && (io.len == 0 || memcmp(io.buf, ref, io.len) == 0));
                        if (points[p] == 1) {
                            CHECK((io.len == 0) == (mode != 2));
                        }
                        if (with_result) {
                            CHECK(result.status == XML2PDF_ERR_IO && result.cause == KI_CAUSE_IO);
                            CHECK(result.message[0] != '\0' && strlen(result.message) < sizeof result.message);
                            CHECK(strncmp(result.message, "xml2pdf:", 8) != 0);
                            CHECK(result.output_bytes == (long)io.len);
                        }

                        /* The same caller state converts correctly afterwards. */
                        os_reset(&io, xml);
                        CHECK(os_run(use_cfg, &opt, &result) == XML2PDF_OK);
                        CHECK(result.status == XML2PDF_OK && result.cause == KI_CAUSE_NONE);
                        CHECK(io.len == ref_len && memcmp(io.buf, ref, ref_len) == 0);
                        CHECK(result.output_bytes == (long)ref_len && io.calls == ref_calls);
                    }
                }
            }
        }
        free(ref);
    }
    free(io.buf);
    return 0;
}
