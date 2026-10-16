/* Flate page and font streams. Included from test_stream.c after
   test_font_metrics.c, so it uses CHECK, MemIo/mem_read/mem_write,
   pdf_inflate_copy, recovered_pages, check_xref, load_file, the deflate
   fault wrappers and the fm_* synthetic font builder. */

typedef struct {
    MemIo io;
    KiResult res;
    int rc;
} FlRun;

static int fl_convert(const char *xml, const char *font, FlRun *run) {
    memset(run, 0, sizeof *run);
    run->io.s = xml;
    run->io.n = strlen(xml);
    run->io.chunk = 7;
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = font;
    opt.temp_dir = "/tmp";
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &run->io;
    run->rc = ki_convert(&opt, &run->res);
    return run->rc;
}

static char *fl_lines_doc(int lines) {
    size_t cap = 64 + (size_t)lines * 40;
    char *xml = malloc(cap);
    if (xml == NULL) {
        return NULL;
    }
    size_t o = (size_t)sprintf(xml, "<doc>");
    for (int i = 0; i < lines; i++) {
        o += (size_t)sprintf(xml + o, "<p>Flate line %04d</p>", i);
    }
    strcpy(xml + o, "</doc>");
    return xml;
}

/* Checks the stream inventory of a converted PDF: every page contents and
   the embedded font are Flate streams whose decoded font equals font_bytes,
   and only the ToUnicode CMap stays uncompressed. */
static int fl_check_inventory(const char *pdf, size_t n, int pages, const char *font_bytes, size_t font_len,
                              PdfStreamList *list) {
    char *dec = NULL;
    size_t dn = 0;
    CHECK(check_xref(pdf, n) == 0);
    CHECK(pdf_inflate_copy(pdf, n, &dec, &dn, list) == 0);
    CHECK(list->n == pages + 2);
    CHECK(list->nflate == pages + 1);
    int font_seen = 0, plain = 0;
    for (int i = 0; i < list->n; i++) {
        const PdfStreamInfo *e = &list->s[i];
        if (e->length1 >= 0) {
            font_seen++;
            CHECK(e->flate && e->obj == 6);
            CHECK((size_t)e->length1 == font_len && e->inflated == font_len);
            CHECK(memcmp(dec + e->out_at, font_bytes, font_len) == 0);
        } else if (!e->flate) {
            plain++;
            CHECK(memcmp(dec + e->out_at, "/CIDInit", 8) == 0);
        } else {
            CHECK(memcmp(dec + e->out_at, "BT\n", 3) == 0);
            CHECK(e->inflated >= 3 && memcmp(dec + e->out_at + e->inflated - 3, "ET\n", 3) == 0);
        }
    }
    CHECK(font_seen == 1 && plain == 1);
    free(dec);
    return 0;
}

static int test_flate_streams(void) {
    static PdfStreamList list;
    char *font = NULL;
    size_t font_len = 0;
    CHECK(load_file("fonts/DejaVuSans.ttf", &font, &font_len) == 0);

    /* Minimal document: one page holding an empty text object. */
    FlRun run;
    CHECK(fl_convert("<doc/>", "fonts/DejaVuSans.ttf", &run) == 0);
    CHECK(run.res.pages == 1 && run.res.output_bytes == (long)run.io.len);
    CHECK(fl_check_inventory(run.io.buf, run.io.len, 1, font, font_len, &list) == 0);
    /* The compressed font is the bulk of the file and is smaller than raw. */
    CHECK(list.s[0].declared < font_len);
    free(run.io.buf);

    /* Multipage document: every line comes back in order from its page. */
    char *xml = fl_lines_doc(160);
    CHECK(xml != NULL);
    CHECK(fl_convert(xml, "fonts/DejaVuSans.ttf", &run) == 0);
    free(xml);
    CHECK(run.res.pages >= 3);
    CHECK(fl_check_inventory(run.io.buf, run.io.len, run.res.pages, font, font_len, &list) == 0);
    char *texts[64];
    size_t tl[64];
    int npages = 0;
    int ns = recovered_pages(run.io.buf, run.io.len, texts, tl, 64, &npages);
    CHECK(ns == run.res.pages && npages == ns);
    /* The default profile also prints element names, so lines are found in
       order rather than compared as whole pages. Each page holds at least
       one line and pages never repeat or reorder lines. */
    int next = 0;
    for (int p = 0; p < ns; p++) {
        char *page = malloc(tl[p] + 1);
        CHECK(page != NULL);
        memcpy(page, texts[p], tl[p]);
        page[tl[p]] = '\0';
        const char *at = page;
        int first = next;
        char want[32];
        while (next < 160) {
            snprintf(want, sizeof want, "Flate line %04d", next);
            const char *hit = strstr(at, want);
            if (hit == NULL) {
                break;
            }
            at = hit + strlen(want);
            next++;
        }
        CHECK(next > first && strstr(at, "Flate line") == NULL);
        free(page);
        free(texts[p]);
    }
    CHECK(next == 160);
    free(run.io.buf);
    free(font);
    return 0;
}

/* Low-compressibility font: a valid synthetic TTF followed by pseudorandom
   bytes. Deflate output is larger than its input and spans many windows. */
static int test_flate_expansion(void) {
    static PdfStreamList list;
    static unsigned char ttf[FM_CAP + 200000];
    FmSpec spec;
    fm_spec_ps(&spec, "KiNoise");
    size_t base = fm_font(ttf, &spec);
    size_t n = base + 200000;
    unsigned long x = 0x9E3779B97F4A7C15ul;
    for (size_t i = base; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        ttf[i] = (unsigned char)(x >> 24);
    }
    char path[64];
    CHECK(fm_write_font(ttf, n, path) == 0);
    FlRun run;
    int rc = fl_convert("<A>A A!</A>", path, &run);
    unlink(path);
    CHECK(rc == 0 && run.res.pages == 1);
    CHECK(fl_check_inventory(run.io.buf, run.io.len, 1, (const char *)ttf, n, &list) == 0);
    /* Incompressible bytes cannot shrink: stored blocks add framing. */
    CHECK(list.s[0].declared > n - base && list.s[0].declared > 4 * 16384);
    free(run.io.buf);
    return 0;
}

static int fl_count_deflate(const char *xml) {
    deflate_calls = 0;
    FlRun run;
    int rc = fl_convert(xml, "fonts/DejaVuSans.ttf", &run);
    free(run.io.buf);
    return rc == 0 ? deflate_calls : -1;
}

/* Converts to a file over an existing destination with a compressor fault
   armed by arm(); the old destination must survive with no temp left. */
static int fl_file_fault(const char *xml, int fail_in, int fail_rc, int init_rc, int want_cause) {
    char dir[] = "/tmp/ki-flate-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char dest[96];
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    FILE *fp = fopen(dest, "wb");
    CHECK(fp != NULL && fputs("OLD", fp) >= 0 && fclose(fp) == 0);
    MemIo in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    KiOptions opt;
    KiResult res;
    ki_options_init(&opt);
    opt.font_path = "fonts/DejaVuSans.ttf";
    opt.temp_dir = dir;
    opt.read_bytes = mem_read;
    opt.io_user = &in;
    deflate_fail_in = fail_in;
    deflate_fail_rc = fail_rc;
    deflate_init_fail_rc = init_rc;
    int rc = ki_convert_to_file(dest, &opt, &res);
    deflate_fail_in = 0;
    deflate_init_fail_rc = 0;
    CHECK(rc != 0 && res.cause == want_cause);
    char *got = NULL;
    size_t gn = 0;
    CHECK(load_file(dest, &got, &gn) == 0);
    CHECK(gn == 3 && memcmp(got, "OLD", 3) == 0);
    free(got);
    CHECK(callback_directory_entries(dir) == 1);
    unlink(dest);
    rmdir(dir);
    return 0;
}

static int test_flate_errors(void) {
    char *xml = fl_lines_doc(160);
    CHECK(xml != NULL);
    int calls = fl_count_deflate(xml);
    CHECK(calls > 3);

    FlRun run;
    /* deflateInit2 out of memory: RESOURCE before any page. */
    deflate_init_fail_rc = Z_MEM_ERROR;
    fl_convert(xml, "fonts/DejaVuSans.ttf", &run);
    deflate_init_fail_rc = 0;
    CHECK(run.rc == 4 && run.res.cause == KI_CAUSE_RESOURCE);
    free(run.io.buf);

    /* Any other zlib error, on the first (font) and the last (final page
       finish) deflate call, is a controlled IO failure. */
    const int at[] = {1, calls};
    const int codes[] = {Z_STREAM_ERROR, Z_DATA_ERROR, Z_BUF_ERROR};
    for (size_t a = 0; a < sizeof at / sizeof at[0]; a++) {
        for (size_t c = 0; c < sizeof codes / sizeof codes[0]; c++) {
            deflate_fail_in = at[a];
            deflate_fail_rc = codes[c];
            fl_convert(xml, "fonts/DejaVuSans.ttf", &run);
            deflate_fail_in = 0;
            CHECK(run.rc == 3 && run.res.cause == KI_CAUSE_IO);
            CHECK(strcmp(run.res.message, "pdf compression failed") == 0);
            CHECK(run.res.output_bytes == (long)run.io.len);
            free(run.io.buf);
        }
    }
    CHECK(fl_count_deflate(xml) == calls);

    CHECK(fl_file_fault(xml, 0, 0, Z_MEM_ERROR, KI_CAUSE_RESOURCE) == 0);
    CHECK(fl_file_fault(xml, 1, Z_STREAM_ERROR, 0, KI_CAUSE_IO) == 0);
    CHECK(fl_file_fault(xml, calls, Z_STREAM_ERROR, 0, KI_CAUSE_IO) == 0);
    free(xml);
    return 0;
}

/* The reader rejects damaged, truncated and mislabelled Flate streams. */
static int test_flate_reader_controls(void) {
    static PdfStreamList list;
    FlRun run;
    CHECK(fl_convert("<doc><p>Flate control text</p></doc>", "fonts/DejaVuSans.ttf", &run) == 0);
    char *pdf = run.io.buf;
    size_t n = run.io.len;
    char *dec = NULL;
    size_t dn = 0;
    CHECK(pdf_inflate_copy(pdf, n, &dec, &dn, &list) == 0);
    free(dec);
    CHECK(list.n == 3);
    const PdfStreamInfo *page = &list.s[2];
    CHECK(page->flate && page->length1 < 0);

    const char *hdr = pi_find(pdf, n, "9 0 obj\n");
    CHECK(hdr != NULL);
    size_t body = (size_t)(pi_find(hdr, n - (size_t)(hdr - pdf), ">>\nstream\n") - pdf) + 10;
    char *bad = malloc(n);
    CHECK(bad != NULL);

    /* Damaged body byte. */
    memcpy(bad, pdf, n);
    bad[body + page->declared / 2] ^= 0x55;
    CHECK(pdf_inflate_copy(bad, n, &dec, &dn, NULL) != 0);

    /* Truncated deflate data inside an unchanged frame. */
    memcpy(bad, pdf, n);
    memset(bad + body + page->declared - 4, ' ', 4);
    CHECK(pdf_inflate_copy(bad, n, &dec, &dn, NULL) != 0);

    /* Output cut inside the page stream. */
    CHECK(pdf_inflate_copy(pdf, body + page->declared / 2, &dec, &dn, NULL) != 0);

    /* Wrong length in the referenced length object (one digit changed). */
    memcpy(bad, pdf, n);
    long off = pi_obj_offset(bad, n, 10);
    CHECK(off > 0);
    char *digit = bad + off + 9;
    CHECK(*digit >= '0' && *digit <= '9');
    *digit = *digit == '9' ? '1' : (char)(*digit + 1);
    CHECK(pdf_inflate_copy(bad, n, &dec, &dn, NULL) != 0);

    /* Font /Length1 that does not match the decoded font. */
    memcpy(bad, pdf, n);
    char *l1 = (char *)pi_find(bad, n, "/Length1 ");
    CHECK(l1 != NULL);
    l1[9] = l1[9] == '9' ? '1' : (char)(l1[9] + 1);
    CHECK(pdf_inflate_copy(bad, n, &dec, &dn, NULL) != 0);
    free(bad);
    free(pdf);
    return 0;
}
