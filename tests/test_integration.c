/* Cross-area contexts: geometry, wrapping, zero-advance marks, Flate,
   /Info, both profiles and a custom mapper in one finite set of jobs that
   use all three _cfg entry points. Each job first runs alone to give its
   reference; the jobs then run together in threads, several rounds, and
   every output, result and mapper counter must equal its own reference.
   A failing context is reused for a valid conversion afterwards.
   Included from test_stream.c after test_info.c (GmIo, gm_map, gm_options,
   gm_file_is, gm_dir_entries, lw_text, lw_free, pdf_inflate_copy). */

#define IT_JOBS 7
#define IT_ROUNDS 3

typedef enum { IT_CALLBACK, IT_FILE, IT_TO_FILE } ItApi;

typedef struct {
    const char *name;
    ItApi api;
    int profile;
    int custom_map;
    int chunk;
    int max_pages;
    long record_offset, record_limit;
    int page_size, orientation;
    double width, height, margin, font_size;
    const char *title, *date;
    const char *xml;
    int want_rc, want_cause;
    const char *marker; /* text expected in this job's PDF only */
} ItSpec;

typedef struct {
    const ItSpec *spec;
    GmIo g;
    KiResult r;
    KiResult first; /* r after the job's own conversion */
    int rc;
    char in_path[40], out_path[40];
    unsigned char *file;
    size_t file_len;
    int again_rc;       /* the failing job then converts it_retry_xml */
    GmIo again;
} ItJob;

static char it_long[8192];
static char it_report[2048];
static char it_mapped[4096];
static const char it_retry_xml[] = "<doc><p>RETRY context is usable</p></doc>";

static void it_fixtures(void) {
    size_t at = (size_t)snprintf(it_long, sizeof it_long, "<doc><p>");
    /* "e" + U+0301: a zero-advance mark that must wrap with its base. */
    while (at + 64 < sizeof it_long - 16) {
        at += (size_t)snprintf(it_long + at, sizeof it_long - at, "ALPHA wrap e\xCC\x81t\xC3\xA9 jumpy W ");
    }
    snprintf(it_long + at, sizeof it_long - at, "END</p></doc>");
    at = (size_t)snprintf(it_report, sizeof it_report, "<report version=\"1\" title=\"BETA report\">");
    for (int i = 0; i < 6; i++) {
        at += (size_t)snprintf(it_report + at, sizeof it_report - at,
                               "<record id=\"r%d\"><field name=\"Value\">BETA value %d &amp; more</field></record>",
                               i, i);
    }
    snprintf(it_report + at, sizeof it_report - at, "</report>");
    at = (size_t)snprintf(it_mapped, sizeof it_mapped, "<doc>");
    for (int i = 0; i < 40; i++) {
        at += (size_t)snprintf(it_mapped + at, sizeof it_mapped - at, "<p>GAMMA <![CDATA[line]]> %d</p>", i);
    }
    snprintf(it_mapped + at, sizeof it_mapped - at, "</doc>");
}

static const ItSpec it_specs[IT_JOBS] = {
    {"records-a4-landscape-info", IT_CALLBACK, KI_PROFILE_RECORDS, 0, 1, 0, 0, 0, KI_PAGE_A4, KI_ORIENT_LANDSCAPE,
     0, 0, 36, 12, "ALPHA (a) \\ title", "2026-10-16T12:00:00+03:00", it_long, 0, KI_CAUSE_NONE, "ALPHA"},
    {"report-custom-page", IT_CALLBACK, KI_PROFILE_REPORT, 0, 4096, 0, 1, 3, KI_PAGE_CUSTOM, KI_ORIENT_PORTRAIT, 400,
     300, 20, 10.5, NULL, NULL, it_report, 0, KI_CAUSE_NONE, "BETA"},
    {"mapper-letter-file", IT_FILE, KI_PROFILE_RECORDS, 1, 0, 0, 0, 0, KI_PAGE_LETTER, KI_ORIENT_PORTRAIT, 0, 0, 40,
     10, "GAMMA", NULL, it_mapped, 0, KI_CAUSE_NONE, "GAMMA"},
    {"records-to-file-a4", IT_TO_FILE, KI_PROFILE_RECORDS, 0, 3, 0, 0, 0, KI_PAGE_A4, KI_ORIENT_PORTRAIT, 0, 0, 50,
     9, NULL, "2000-01-01T00:00:00Z", it_long, 0, KI_CAUSE_NONE, "ALPHA"},
    {"late-xml-to-file", IT_TO_FILE, KI_PROFILE_RECORDS, 0, 7, 0, 0, 0, KI_PAGE_LETTER, KI_ORIENT_LANDSCAPE, 0, 0, 40,
     10, "DELTA", NULL, "<doc><p>DELTA one</p><p>DELTA two</q></doc>", XML2PDF_ERR_XML, KI_CAUSE_XML, NULL},
    {"page-quota-callback", IT_CALLBACK, KI_PROFILE_RECORDS, 1, 2, 1, 0, 0, KI_PAGE_A4, KI_ORIENT_PORTRAIT, 0, 0, 40,
     10, NULL, NULL, it_long, XML2PDF_ERR_RESOURCE, KI_CAUSE_RESOURCE, NULL},
    {"bad-report-file", IT_FILE, KI_PROFILE_REPORT, 0, 0, 0, 0, 0, KI_PAGE_LETTER, KI_ORIENT_PORTRAIT, 0, 0, 40, 10,
     NULL, NULL, "<report version=\"2\"><record id=\"x\"/></report>", XML2PDF_ERR_XML, KI_CAUSE_PROFILE, NULL},
};

static const char *it_markers[] = {"ALPHA", "BETA", "GAMMA", "DELTA", "RETRY"};

static void it_config(const ItSpec *s, KiConfig *cfg) {
    ki_config_init(cfg, sizeof *cfg);
    cfg->page_size = s->page_size;
    cfg->orientation = s->orientation;
    cfg->page_width = s->width;
    cfg->page_height = s->height;
    cfg->margin_top = cfg->margin_right = cfg->margin_bottom = cfg->margin_left = s->margin;
    cfg->font_size = s->font_size;
    cfg->title = s->title;
    cfg->creation_date = s->date;
}

static int it_read_file(const char *path, unsigned char **out, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    size_t cap = 1 << 16, n = 0;
    unsigned char *buf = malloc(cap);
    size_t got;
    while (buf != NULL && (got = fread(buf + n, 1, cap - n, fp)) > 0) {
        n += got;
        if (n == cap) {
            unsigned char *g = realloc(buf, cap * 2);
            if (g == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = g;
            cap *= 2;
        }
    }
    fclose(fp);
    *out = buf;
    *len = n;
    return buf == NULL ? -1 : 0;
}

static const char *it_old = "OLD_INTEGRATION_DESTINATION";

static int it_prepare(ItJob *job, const ItSpec *s, int k) {
    memset(job, 0, sizeof *job);
    job->spec = s;
    snprintf(job->in_path, sizeof job->in_path, "/tmp/ki-it-in-%d-XXXXXX", k);
    snprintf(job->out_path, sizeof job->out_path, "/tmp/ki-it-out-%d-XXXXXX", k);
    int fi = mkstemp(job->in_path);
    int fo = mkstemp(job->out_path);
    if (fi < 0 || fo < 0) {
        return -1;
    }
    size_t n = strlen(s->xml);
    int ok = write(fi, s->xml, n) == (ssize_t)n && write(fo, it_old, strlen(it_old)) == (ssize_t)strlen(it_old);
    close(fi);
    close(fo);
    return ok ? 0 : -1;
}

static void it_options(ItJob *job, KiOptions *opt, GmIo *g, const char *xml) {
    const ItSpec *s = job->spec;
    memset(g, 0, sizeof *g);
    g->io.s = xml;
    g->io.n = strlen(xml);
    g->io.chunk = s->chunk;
    gm_options(opt, g, "fonts/DejaVuSans.ttf", s->custom_map);
    opt->profile = s->profile;
    opt->max_pages = s->max_pages;
    opt->record_offset = s->record_offset;
    opt->record_limit = s->record_limit;
}

static void *it_run(void *arg) {
    ItJob *job = arg;
    const ItSpec *s = job->spec;
    KiConfig cfg;
    it_config(s, &cfg);
    KiOptions opt;
    it_options(job, &opt, &job->g, s->xml);
    if (s->api == IT_CALLBACK) {
        job->rc = ki_convert_cfg(&opt, &cfg, &job->r);
    } else if (s->api == IT_FILE) {
        job->rc = ki_convert_file_cfg(job->in_path, job->out_path, &opt, &cfg, &job->r);
    } else {
        job->rc = ki_convert_to_file_cfg(job->out_path, &opt, &cfg, &job->r);
    }
    if (s->api != IT_CALLBACK) {
        it_read_file(job->out_path, &job->file, &job->file_len);
    }
    job->first = job->r;
    if (job->rc != 0) {
        /* The same options, config and result serve a valid input next. */
        GmIo *g = &job->again;
        memset(g, 0, sizeof *g);
        g->io.s = it_retry_xml;
        g->io.n = strlen(it_retry_xml);
        g->io.chunk = s->chunk;
        opt.write_bytes = gm_write;
        opt.io_user = g;
        opt.cancel_user = g;
        opt.map_user = g;
        opt.max_pages = 0;
        opt.profile = KI_PROFILE_RECORDS;
        job->again_rc = ki_convert_cfg(&opt, &cfg, &job->r);
    }
    return NULL;
}

static void it_release(ItJob *job) {
    free(job->g.io.buf);
    free(job->again.io.buf);
    free(job->file);
    job->g.io.buf = job->again.io.buf = NULL;
    job->file = NULL;
}

/* PDF bytes of a job: the callback buffer or the published file. */
static void it_pdf(const ItJob *job, const char **pdf, size_t *n) {
    if (job->spec->api == IT_CALLBACK) {
        *pdf = job->g.io.buf;
        *n = job->g.io.len;
    } else {
        *pdf = (const char *)job->file;
        *n = job->file_len;
    }
}

static int it_same(const ItJob *a, const ItJob *b) {
    const char *pa, *pb;
    size_t na, nb;
    it_pdf(a, &pa, &na);
    it_pdf(b, &pb, &nb);
    return a->rc == b->rc && a->first.cause == b->first.cause && strcmp(a->first.message, b->first.message) == 0 &&
           a->first.pages == b->first.pages && a->first.objects == b->first.objects &&
           a->first.output_bytes == b->first.output_bytes && a->r.output_bytes == b->r.output_bytes &&
           a->g.maps == b->g.maps && a->g.reads == b->g.reads && na == nb && (na == 0 || memcmp(pa, pb, na) == 0) &&
           a->again_rc == b->again_rc && a->again.io.len == b->again.io.len &&
           (a->again.io.len == 0 || memcmp(a->again.io.buf, b->again.io.buf, a->again.io.len) == 0);
}

/* Whether marker occurs in the PDF's ActualText lines, read independently. */
static int it_text_has(const char *pdf, size_t n, const char *marker, int want) {
    MemIo io;
    memset(&io, 0, sizeof io);
    io.buf = (char *)pdf;
    io.len = n;
    LwBuf text;
    if (lw_text(&io, &text) != 0) {
        return 0;
    }
    int hit = strstr(text.s, marker) != NULL;
    lw_free(&text);
    return hit == want;
}

static int it_check_reference(const ItJob *job) {
    const ItSpec *s = job->spec;
    if (job->rc != s->want_rc || job->first.cause != s->want_cause) {
        fprintf(stderr, "%s: rc %d cause %d (%s)\n", s->name, job->rc, job->first.cause, job->first.message);
        return -1;
    }
    const char *pdf;
    size_t n;
    it_pdf(job, &pdf, &n);
    if (s->want_rc != 0) {
        CHECK(job->again_rc == 0 && job->r.cause == KI_CAUSE_NONE && job->again.io.len > 0);
        CHECK(it_text_has(job->again.io.buf, job->again.io.len, "RETRY", 1));
        if (s->api != IT_CALLBACK) {
            CHECK(n == strlen(it_old) && memcmp(pdf, it_old, n) == 0);
        }
        return 0;
    }
    CHECK(n > 0 && job->first.output_bytes == (long)n && job->first.pages > 0);
    char *flat = NULL;
    size_t flat_n = 0;
    PdfStreamList *list = malloc(sizeof *list);
    CHECK(list != NULL);
    CHECK(pdf_inflate_copy(pdf, n, &flat, &flat_n, list) == 0);
    /* Every page's contents and the font are Flate; ToUnicode is not. */
    CHECK(list->nflate == job->first.pages + 1);
    free(list);
    free(flat);
    for (size_t m = 0; m < sizeof it_markers / sizeof it_markers[0]; m++) {
        CHECK(it_text_has(pdf, n, it_markers[m], strcmp(it_markers[m], s->marker) == 0));
    }
    MemIo io;
    memset(&io, 0, sizeof io);
    io.buf = (char *)pdf;
    io.len = n;
    GmIo view;
    view.io = io;
    char media[64];
    if (s->page_size == KI_PAGE_CUSTOM) {
        snprintf(media, sizeof media, "/MediaBox [0 0 %g %g]", s->width, s->height);
    } else if (s->page_size == KI_PAGE_A4) {
        snprintf(media, sizeof media, "%s",
                 s->orientation == KI_ORIENT_LANDSCAPE ? "/MediaBox [0 0 841.89 595.276]" : "/MediaBox [0 0 595.276 841.89]");
    } else {
        snprintf(media, sizeof media, "/MediaBox [0 0 612 792]");
    }
    CHECK(gm_count_in(&view, media) == job->first.pages);
    CHECK(gm_count_in(&view, "/Info ") == (s->title != NULL || s->date != NULL));
    if (s->title != NULL && strchr(s->title, '(') != NULL) {
        /* Delimiters stay inside the UTF-16BE hex string. */
        CHECK(gm_count_in(&view, "(a)") == 0);
    }
    return 0;
}

static int test_integration_contexts(void) {
    it_fixtures();
    static ItJob ref[IT_JOBS], run[IT_JOBS];
    for (int i = 0; i < IT_JOBS; i++) {
        CHECK(it_prepare(&ref[i], &it_specs[i], i) == 0);
        it_run(&ref[i]);
        if (it_check_reference(&ref[i]) != 0) {
            fprintf(stderr, "integration reference %s failed\n", it_specs[i].name);
            return 1;
        }
    }
    /* The records profile keeps every word of the long text in order. */
    {
        const char *pdf;
        size_t n;
        it_pdf(&ref[0], &pdf, &n);
        MemIo io;
        memset(&io, 0, sizeof io);
        io.buf = (char *)pdf;
        io.len = n;
        LwBuf text;
        CHECK(lw_text(&io, &text) == 0);
        LwBuf want;
        memset(&want, 0, sizeof want);
        CHECK(lw_puts(&want, "doc p ") == 0);
        const char *body = it_long + strlen("<doc><p>");
        CHECK(lw_put(&want, body, strlen(body) - strlen("</p></doc>")) == 0);
        size_t a = 0, b = 0;
        int same = 1;
        for (;;) {
            while (a < text.n && strchr(" \n\f", text.s[a]) != NULL) {
                a++;
            }
            while (b < want.n && want.s[b] == ' ') {
                b++;
            }
            if (a == text.n || b == want.n) {
                same = same && a == text.n && b == want.n;
                break;
            }
            if (text.s[a] != want.s[b]) {
                same = 0;
                break;
            }
            a++;
            b++;
        }
        CHECK(ref[0].first.pages >= 2);
        lw_free(&text);
        lw_free(&want);
        CHECK(same);
    }
    for (int round = 0; round < IT_ROUNDS; round++) {
        pthread_t t[IT_JOBS];
        for (int i = 0; i < IT_JOBS; i++) {
            CHECK(it_prepare(&run[i], &it_specs[i], i) == 0);
        }
        for (int i = 0; i < IT_JOBS; i++) {
            CHECK(pthread_create(&t[i], NULL, it_run, &run[i]) == 0);
        }
        for (int i = 0; i < IT_JOBS; i++) {
            CHECK(pthread_join(t[i], NULL) == 0);
        }
        for (int i = 0; i < IT_JOBS; i++) {
            if (!it_same(&run[i], &ref[i])) {
                fprintf(stderr, "round %d: %s differs from its serial reference\n", round, it_specs[i].name);
                return 1;
            }
            unlink(run[i].in_path);
            unlink(run[i].out_path);
            it_release(&run[i]);
        }
    }
    for (int i = 0; i < IT_JOBS; i++) {
        unlink(ref[i].in_path);
        unlink(ref[i].out_path);
        it_release(&ref[i]);
    }
    return 0;
}
