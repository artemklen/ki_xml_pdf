/* Optional /Info: sized KiConfig versions, exact encoded values, rejection
   before any I/O, and quota/cancel/atomic file behavior with metadata.
   Included from test_stream.c after test_geometry.c, which provides GmIo,
   gm_options, gm_convert, gm_dir_entries, gm_file_is and gm_media.

   Expected dictionary bytes are written out by hand from the documented
   encoding (UTF-16BE hex with FEFF; D:YYYYMMDDHHmmSS with Z or +HH'mm'). */

static const char in_xml[] = "<doc><p>Info leaves the body alone</p><p>second line</p></doc>";

static void in_cfg(KiConfig *cfg, const char *title, const char *producer, const char *date) {
    ki_config_init(cfg, sizeof *cfg);
    cfg->title = title;
    cfg->producer = producer;
    cfg->creation_date = date;
}

static long in_startxref(const GmIo *g) {
    const char *key = "startxref\n";
    size_t k = strlen(key);
    for (size_t i = g->io.len; i-- > k;) {
        if (memcmp(g->io.buf + i - k, key, k) == 0) {
            return strtol(g->io.buf + i, NULL, 10);
        }
    }
    return -1;
}

static size_t in_count(const GmIo *g, const char *needle) {
    size_t k = strlen(needle), hits = 0;
    for (size_t i = 0; i + k <= g->io.len; i++) {
        hits += memcmp(g->io.buf + i, needle, k) == 0;
    }
    return hits;
}

static int test_info_config(void) {
    CHECK(KI_CONFIG_SIZE_GEOMETRY < sizeof(KiConfig));
    CHECK(sizeof(KiConfig) - KI_CONFIG_SIZE_GEOMETRY == 3 * sizeof(const char *));
    unsigned char raw[sizeof(KiConfig) + 8];
    memset(raw, 0xAA, sizeof raw);
    KiConfig *cfg = (KiConfig *)(void *)raw;
    CHECK(ki_config_init(cfg, KI_CONFIG_SIZE_GEOMETRY) == 0);
    CHECK(cfg->size == KI_CONFIG_SIZE_GEOMETRY && cfg->font_size == 10.0 && cfg->margin_left == 40.0);
    for (size_t k = KI_CONFIG_SIZE_GEOMETRY; k < sizeof raw; k++) {
        CHECK(raw[k] == 0xAA);
    }
    KiConfig full;
    memset(&full, 0x55, sizeof full);
    CHECK(ki_config_init(&full, sizeof full) == 0);
    CHECK(full.title == NULL && full.producer == NULL && full.creation_date == NULL);

    /* A geometry-only config allocated at its exact size: nothing past it is
       read (ASan) and the output equals the 0.2 call's. */
    void *short_mem = malloc(KI_CONFIG_SIZE_GEOMETRY);
    KiConfig *short_cfg = short_mem;
    CHECK(short_cfg != NULL);
    CHECK(ki_config_init(short_cfg, KI_CONFIG_SIZE_GEOMETRY) == 0);
    GmIo a, b, c;
    KiResult r;
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", NULL, &a, &r) == 0);
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", short_cfg, &b, &r) == 0);
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", &full, &c, &r) == 0);
    CHECK(a.io.len == b.io.len && memcmp(a.io.buf, b.io.buf, a.io.len) == 0);
    CHECK(a.io.len == c.io.len && memcmp(a.io.buf, c.io.buf, a.io.len) == 0);
    CHECK(in_count(&a, "/Info") == 0 && in_count(&a, "/CreationDate") == 0 && in_count(&a, "/Producer") == 0);
    free(short_cfg);
    free(a.io.buf);
    free(b.io.buf);
    free(c.io.buf);
    return 0;
}

typedef struct {
    const char *title, *producer, *date;
    const char *dict; /* expected Info object body */
} InCase;

static int test_info_values(void) {
    /* "Отчёт" is U+041E U+0442 U+0447 U+0451 U+0442. */
    const InCase cases[] = {
        {"A", NULL, NULL, "<<\n/Title <FEFF0041>\n>>\n"},
        {NULL, "ki", NULL, "<<\n/Producer <FEFF006B0069>\n>>\n"},
        {NULL, NULL, "2026-10-16T12:34:56Z", "<<\n/CreationDate (D:20261016123456Z)\n>>\n"},
        {NULL, NULL, "2024-02-29T23:59:59-05:30", "<<\n/CreationDate (D:20240229235959-05'30')\n>>\n"},
        {NULL, NULL, "2000-02-29T00:00:00+23:59", "<<\n/CreationDate (D:20000229000000+23'59')\n>>\n"},
        {"\xD0\x9E\xD1\x82\xD1\x87\xD1\x91\xD1\x82", "p", "1999-12-31T23:59:59+00:00",
         "<<\n/Title <FEFF041E0442044704510442>\n/Producer <FEFF0070>\n/CreationDate (D:19991231235959+00'00')\n>>\n"},
        /* Delimiters, backslash, newline, CR, tab and syntax-like text. */
        {"a(b)\\c\n\r\t", ") /JS (x) >> endobj", NULL,
         "<<\n/Title <FEFF0061002800620029005C0063000A000D0009>\n"
         "/Producer <FEFF00290020002F004A005300200028007800290020003E003E00200065006E0064006F0062006A>\n>>\n"},
        /* U+1F600 is the surrogate pair D83D DE00; U+FEFF inside the value stays. */
        {"\xF0\x9F\x98\x80\xEF\xBB\xBF", NULL, NULL, "<<\n/Title <FEFFD83DDE00FEFF>\n>>\n"},
    };
    GmIo base;
    KiResult r;
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", NULL, &base, &r) == 0);
    int base_pages = r.pages;
    int base_objects = r.objects;
    long base_xref = in_startxref(&base);
    CHECK(base_xref > 0);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const char *want = cases[i].dict;
        KiConfig cfg;
        in_cfg(&cfg, cases[i].title, cases[i].producer, cases[i].date);
        GmIo g;
        int rc = gm_convert(in_xml, "fonts/DejaVuSans.ttf", &cfg, &g, &r);
        int ok = rc == 0 && r.pages == base_pages && r.objects == base_objects + 1 && in_count(&g, "/Info ") == 1 &&
                 g.io.len > (size_t)base_xref && memcmp(g.io.buf, base.io.buf, (size_t)base_xref) == 0;
        char obj[600];
        snprintf(obj, sizeof obj, "%d 0 obj\n%sendobj\n", base_objects + 1, want);
        ok = ok && (size_t)base_xref + strlen(obj) <= g.io.len &&
             memcmp(g.io.buf + base_xref, obj, strlen(obj)) == 0;
        char ref[96];
        snprintf(ref, sizeof ref, "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\n", base_objects + 2,
                 base_objects + 1);
        ok = ok && in_count(&g, ref) == 1;
        if (!ok) {
            fprintf(stderr, "info case %zu rc=%d (%s)\n", i, rc, r.message);
            free(g.io.buf);
            free(base.io.buf);
            return 1;
        }
        free(g.io.buf);
    }

    /* Metadata with a non-default geometry: same body and MediaBox. */
    KiConfig a4;
    in_cfg(&a4, NULL, NULL, NULL);
    a4.page_size = KI_PAGE_A4;
    GmIo plain, meta;
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", &a4, &plain, &r) == 0);
    a4.title = "T";
    a4.creation_date = "2026-01-01T00:00:00Z";
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", &a4, &meta, &r) == 0);
    long px = in_startxref(&plain);
    CHECK(px > 0 && memcmp(plain.io.buf, meta.io.buf, (size_t)px) == 0 && gm_media(&meta, "595.276 841.89"));
    free(plain.io.buf);
    free(meta.io.buf);

    /* Byte bound: exactly KI_INFO_TEXT_MAX bytes in 1-, 2-, 3- and 4-byte
       characters is accepted whole; the value is not truncated. */
    static const char *units[] = {"x", "\xD0\xAF", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
    static const char *hex[] = {"0078", "042F", "20AC", "D83DDE00"};
    for (int u = 0; u < 4; u++) {
        size_t w = strlen(units[u]);
        char *title = malloc(KI_INFO_TEXT_MAX + 1);
        char *expect = malloc(8 * KI_INFO_TEXT_MAX + 64);
        CHECK(title != NULL && expect != NULL);
        size_t n = 0, e = 0;
        e += (size_t)sprintf(expect, "/Title <FEFF");
        while (n + w <= KI_INFO_TEXT_MAX) {
            memcpy(title + n, units[u], w);
            n += w;
            e += (size_t)sprintf(expect + e, "%s", hex[u]);
        }
        /* Fill a remainder with ASCII so the value is exactly the bound. */
        while (n < KI_INFO_TEXT_MAX) {
            title[n++] = 'x';
            e += (size_t)sprintf(expect + e, "0078");
        }
        title[n] = '\0';
        sprintf(expect + e, ">\n");
        KiConfig cfg;
        in_cfg(&cfg, title, title, NULL);
        GmIo g;
        CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", &cfg, &g, &r) == 0);
        CHECK(in_count(&g, expect) == 1);
        free(g.io.buf);
        free(title);
        free(expect);
    }
    free(base.io.buf);
    return 0;
}

/* Every invalid value is USAGE before any callback, input/font open or
   destination/temp creation, through all three entry points, even when the
   input path and the font do not exist. */
static int test_info_usage(void) {
    static char longer[KI_INFO_TEXT_MAX + 2];
    memset(longer, 'y', KI_INFO_TEXT_MAX + 1);
    static char longer_mb[KI_INFO_TEXT_MAX + 2];
    for (int k = 0; k < KI_INFO_TEXT_MAX - 1; k++) {
        longer_mb[k] = 'y';
    }
    memcpy(longer_mb + KI_INFO_TEXT_MAX - 1, "\xD0\xAF", 3); /* 1025 bytes */
    const char *bad_text[] = {"", longer, longer_mb, "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80",
                              "\xF4\x90\x80\x80", "\xE2\x82", "a\xFF", "\x80", "\x01", "a\x1B[0m", "\x7F",
                              "\xC2\x85", "\xF8\x88\x80\x80\x80"};
    const char *bad_date[] = {"", "2026-02-29T00:00:00Z", "1900-02-29T00:00:00Z", "2026-13-01T00:00:00Z",
                              "2026-00-01T00:00:00Z", "2026-04-31T00:00:00Z", "2026-10-00T00:00:00Z",
                              "2026-10-16T24:00:00Z", "2026-10-16T23:60:00Z", "2026-10-16T23:59:60Z",
                              "2026-10-16T12:00:00", "2026-10-16T12:00:00z", "2026-10-16 12:00:00Z",
                              "2026-10-16T12:00:00+24:00", "2026-10-16T12:00:00+03:60", "2026-10-16T12:00:00+0300",
                              "2026-10-16T12:00:00+03:00'", "2026-10-16T12:00:00Z ", "D:20261016120000Z",
                              "20261016T120000Z", "2026-1-16T12:00:00Z", "+2026-10-16T12:00:00Z",
                              "2026-10-16T12:00:00.5Z", "2026-10-16T12:00:00Z)", "２026-10-16T12:00:00Z"};
    size_t nt = sizeof bad_text / sizeof bad_text[0];
    size_t nd = sizeof bad_date / sizeof bad_date[0];
    char dir[] = "/tmp/ki-info-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char in_path[96], missing[96], dest[96];
    snprintf(in_path, sizeof in_path, "%s/in.xml", dir);
    snprintf(missing, sizeof missing, "%s/missing.xml", dir);
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    FILE *fp = fopen(in_path, "wb");
    CHECK(fp != NULL && fputs(in_xml, fp) >= 0 && fclose(fp) == 0);
    fp = fopen(dest, "wb");
    CHECK(fp != NULL && fputs("OLD", fp) >= 0 && fclose(fp) == 0);
    for (size_t i = 0; i < 2 * nt + nd; i++) {
        for (int api = 0; api < 4; api++) {
            for (int font = 0; font < 2; font++) {
                KiConfig cfg;
                in_cfg(&cfg, "ok", "ok", "2026-10-16T12:00:00Z");
                if (i < nt) {
                    cfg.title = bad_text[i];
                } else if (i < 2 * nt) {
                    cfg.producer = bad_text[i - nt];
                } else {
                    cfg.creation_date = bad_date[i - 2 * nt];
                }
                GmIo g;
                memset(&g, 0, sizeof g);
                g.io.s = in_xml;
                g.io.n = strlen(in_xml);
                KiOptions opt;
                gm_options(&opt, &g, font ? "fonts/DejaVuSans.ttf" : "/nonexistent/font.ttf", 1);
                opt.temp_dir = dir;
                KiResult r;
                memset(&r, 0x55, sizeof r);
                int rc = api == 0   ? ki_convert_cfg(&opt, &cfg, &r)
                         : api == 1 ? ki_convert_to_file_cfg(dest, &opt, &cfg, &r)
                         : api == 2 ? ki_convert_file_cfg(in_path, dest, &opt, &cfg, &r)
                                    : ki_convert_file_cfg(missing, dest, &opt, &cfg, &r);
                int ok = rc == XML2PDF_ERR_USAGE && r.status == XML2PDF_ERR_USAGE && r.cause == KI_CAUSE_USAGE &&
                         r.output_bytes == 0 && r.pages == 0 && g.reads == 0 && g.writes == 0 && g.maps == 0 &&
                         g.cancels == 0 && g.io.len == 0 && gm_file_is(dest, "OLD") && gm_dir_entries(dir) == 2 &&
                         strncmp(r.message, "metadata: ", 10) == 0;
                if (!ok) {
                    fprintf(stderr, "info usage %zu api=%d font=%d rc=%d cause=%d reads=%d writes=%d (%s)\n", i,
                            api, font, rc, r.cause, g.reads, g.writes, r.message);
                    return 1;
                }
            }
        }
    }
    unlink(in_path);
    unlink(dest);
    rmdir(dir);
    return 0;
}

typedef struct {
    int calls;
    int stop_at;
} InCancel;

static int in_cancel(void *user) {
    InCancel *c = user;
    c->calls++;
    return c->stop_at > 0 && c->calls >= c->stop_at;
}

/* Info bytes count against the output quota exactly, and a quota or cancel
   failure keeps the previous file and leaves no temp behind. */
static int test_info_limits(void) {
    KiConfig cfg;
    in_cfg(&cfg, "Quota \xD0\xBE\xD1\x82\xD1\x87\xD1\x91\xD1\x82", "ki_xml_pdf", "2026-10-16T12:00:00+03:00");
    GmIo g, plain;
    KiResult r;
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", &cfg, &g, &r) == 0);
    long total = (long)g.io.len;
    CHECK(r.output_bytes == total);
    CHECK(gm_convert(in_xml, "fonts/DejaVuSans.ttf", NULL, &plain, &r) == 0);
    CHECK((long)plain.io.len < total);
    free(plain.io.buf);
    for (int cap = 0; cap < 2; cap++) {
        GmIo q;
        memset(&q, 0, sizeof q);
        q.io.s = in_xml;
        q.io.n = strlen(in_xml);
        KiOptions opt;
        gm_options(&opt, &q, "fonts/DejaVuSans.ttf", 0);
        opt.max_output_bytes = total - cap;
        int rc = ki_convert_cfg(&opt, &cfg, &r);
        if (cap == 0) {
            CHECK(rc == 0 && r.output_bytes == total && q.io.len == g.io.len &&
                  memcmp(q.io.buf, g.io.buf, g.io.len) == 0);
        } else {
            CHECK(rc == XML2PDF_ERR_RESOURCE && r.cause == KI_CAUSE_RESOURCE && r.output_bytes <= total - 1);
        }
        free(q.io.buf);
    }

    char dir[] = "/tmp/ki-info-limits-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char in_path[96], dest[96];
    snprintf(in_path, sizeof in_path, "%s/in.xml", dir);
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    FILE *fp = fopen(in_path, "wb");
    CHECK(fp != NULL && fputs(in_xml, fp) >= 0 && fclose(fp) == 0);
    fp = fopen(dest, "wb");
    CHECK(fp != NULL && fputs("OLD", fp) >= 0 && fclose(fp) == 0);
    KiOptions opt;
    GmIo unused;
    memset(&unused, 0, sizeof unused);
    gm_options(&opt, &unused, "fonts/DejaVuSans.ttf", 0);
    opt.temp_dir = dir;
    opt.max_output_bytes = total - 1;
    CHECK(ki_convert_file_cfg(in_path, dest, &opt, &cfg, &r) == XML2PDF_ERR_RESOURCE);
    CHECK(r.cause == KI_CAUSE_RESOURCE && gm_file_is(dest, "OLD") && gm_dir_entries(dir) == 2);

    /* Count the cancel checks of a complete run, then stop at each one. */
    InCancel c = {0, 0};
    opt.max_output_bytes = 0;
    opt.cancelled = in_cancel;
    opt.cancel_user = &c;
    char probe[112];
    snprintf(probe, sizeof probe, "%s/probe.pdf", dir);
    CHECK(ki_convert_file_cfg(in_path, probe, &opt, &cfg, &r) == 0);
    int checks = c.calls;
    CHECK(checks > 2);
    fp = fopen(probe, "rb");
    CHECK(fp != NULL);
    char *disk = malloc((size_t)total + 1);
    CHECK(disk != NULL && fread(disk, 1, (size_t)total + 1, fp) == (size_t)total);
    fclose(fp);
    CHECK(memcmp(disk, g.io.buf, (size_t)total) == 0);
    free(disk);
    unlink(probe);
    for (int k = 1; k <= checks; k++) {
        c.calls = 0;
        c.stop_at = k;
        int rc = ki_convert_file_cfg(in_path, dest, &opt, &cfg, &r);
        if (rc != XML2PDF_ERR_RESOURCE || r.cause != KI_CAUSE_CANCEL || !gm_file_is(dest, "OLD") ||
            gm_dir_entries(dir) != 2) {
            fprintf(stderr, "info cancel at %d/%d rc=%d cause=%d (%s)\n", k, checks, rc, r.cause, r.message);
            return 1;
        }
    }
    unlink(in_path);
    unlink(dest);
    rmdir(dir);
    free(g.io.buf);
    return 0;
}
