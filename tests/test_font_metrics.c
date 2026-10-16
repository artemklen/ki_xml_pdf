/* Embedded font names, present zero-advance glyphs against missing GIDs, and
   bounded combining-mark runs. Included from test_stream.c, which provides
   CHECK, MemIo/mem_read/mem_write, put16/put32 and the temporary-file
   helpers. */

#define FM_CAP 4096

typedef struct {
    const char *tag;
    const unsigned char *data;
    size_t len;
} FmTable;

static size_t fm_build(unsigned char *out, const FmTable *t, int n) {
    memset(out, 0, FM_CAP);
    put32(out, 0x00010000ul);
    put16(out + 4, (unsigned)n);
    size_t at = 12 + 16 * (size_t)n;
    for (int i = 0; i < n; i++) {
        unsigned char *e = out + 12 + 16 * i;
        memcpy(e, t[i].tag, 4);
        put32(e + 8, at);
        put32(e + 12, t[i].len);
        memcpy(out + at, t[i].data, t[i].len);
        at += t[i].len;
    }
    return at;
}

/* Format 4 with one single-code-point segment per pair, then 0xFFFF. */
static size_t fm_cmap(unsigned char *p, const unsigned *cps, const unsigned *gids, int k) {
    unsigned seg = (unsigned)k + 1;
    size_t sub = 16 + 8 * (size_t)seg;
    memset(p, 0, 12 + sub);
    put16(p + 2, 1);
    put16(p + 4, 3);
    put16(p + 6, 1);
    put32(p + 8, 12);
    unsigned char *s = p + 12;
    put16(s, 4);
    put16(s + 2, (unsigned)sub);
    put16(s + 6, 2 * seg);
    for (unsigned i = 0; i < seg; i++) {
        unsigned cp = i < (unsigned)k ? cps[i] : 0xFFFF;
        unsigned delta = i < (unsigned)k ? (gids[i] - cp) & 0xFFFFu : 1;
        put16(s + 14 + 2 * i, cp);
        put16(s + 16 + 2 * seg + 2 * i, cp);
        put16(s + 16 + 4 * seg + 2 * i, delta);
    }
    return 12 + sub;
}

typedef struct {
    unsigned plat, enc, lang, id;
    const unsigned char *s;
    size_t n;
} FmName;

static size_t fm_name(unsigned char *p, const FmName *r, int k) {
    size_t store = 6 + 12 * (size_t)k;
    put16(p, 0);
    put16(p + 2, (unsigned)k);
    put16(p + 4, (unsigned)store);
    size_t off = 0;
    for (int i = 0; i < k; i++) {
        unsigned char *e = p + 6 + 12 * i;
        put16(e, r[i].plat);
        put16(e + 2, r[i].enc);
        put16(e + 4, r[i].lang);
        put16(e + 6, r[i].id);
        put16(e + 8, (unsigned)r[i].n);
        put16(e + 10, (unsigned)off);
        memcpy(p + store + off, r[i].s, r[i].n);
        off += r[i].n;
    }
    return store + off;
}

/* UTF-16BE copy of an ASCII string. */
static size_t fm_utf16(unsigned char *out, const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = 0;
        out[2 * i + 1] = (unsigned char)s[i];
    }
    return 2 * n;
}

/* Only space, '!', 'A' and three marks are mapped, so documents use <A>.
   GIDs: 1 space 500, 2 'A' 600, 3 a zero-advance mark, 4 '!' zero advance
   (hmtx's last long metric). U+0302 maps to GID 5 and U+0303 to GID 0: both
   are missing glyphs when maxp says five glyphs. */
typedef struct {
    unsigned char name[1024];
    size_t name_len;
    int has_name;
    unsigned char maxp[6];
    size_t maxp_len;
    int has_maxp;
} FmSpec;

static size_t fm_font(unsigned char *ttf, const FmSpec *spec) {
    static unsigned char head[54], hhea[36], hmtx[16], cmap[256];
    memset(head, 0, sizeof head);
    put16(head + 18, 2048);
    memset(hhea, 0, sizeof hhea);
    put16(hhea + 4, 1900);
    put16(hhea + 6, 0xFE00);
    put16(hhea + 34, 4);
    const unsigned adv[4] = {500, 500, 600, 0};
    for (int i = 0; i < 4; i++) {
        put16(hmtx + 4 * i, adv[i]);
        put16(hmtx + 4 * i + 2, 0);
    }
    const unsigned cps[] = {0x20, 0x21, 0x41, 0x300, 0x302, 0x303};
    const unsigned gids[] = {1, 4, 2, 3, 5, 0};
    size_t cl = fm_cmap(cmap, cps, gids, 6);
    FmTable t[6];
    int n = 0;
    t[n++] = (FmTable){"head", head, sizeof head};
    t[n++] = (FmTable){"hhea", hhea, sizeof hhea};
    if (spec->has_maxp) {
        t[n++] = (FmTable){"maxp", spec->maxp, spec->maxp_len};
    }
    t[n++] = (FmTable){"hmtx", hmtx, sizeof hmtx};
    t[n++] = (FmTable){"cmap", cmap, cl};
    if (spec->has_name) {
        t[n++] = (FmTable){"name", spec->name, spec->name_len};
    }
    return fm_build(ttf, t, n);
}

static void fm_spec(FmSpec *spec) {
    memset(spec, 0, sizeof *spec);
    put32(spec->maxp, 0x00005000ul);
    put16(spec->maxp + 4, 5);
    spec->maxp_len = 6;
    spec->has_maxp = 1;
}

static void fm_spec_ps(FmSpec *spec, const char *ps) {
    unsigned char u[256];
    fm_spec(spec);
    FmName r = {3, 1, 0x409, 6, u, fm_utf16(u, ps)};
    spec->name_len = fm_name(spec->name, &r, 1);
    spec->has_name = 1;
}

static int fm_write_font(const unsigned char *b, size_t n, char *path) {
    strcpy(path, "/tmp/ki-font-metrics-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    int ok = write(fd, b, n) == (ssize_t)n;
    close(fd);
    return ok ? 0 : -1;
}

/* Loads the spec's font; on success name receives the decoded name. */
static int fm_load(const FmSpec *spec, char *name, size_t cap, int *fallback) {
    static unsigned char ttf[FM_CAP];
    char path[64];
    size_t n = fm_font(ttf, spec);
    if (fm_write_font(ttf, n, path) != 0) {
        return -2;
    }
    FontFace face;
    int rc = font_load(&face, path);
    unlink(path);
    if (rc == 0) {
        snprintf(name, cap, "%s", face.name);
        *fallback = face.name_fallback;
        font_free(&face);
    }
    return rc;
}

static int fm_convert(const char *xml, size_t n, int chunk, const char *font, MemIo *io, KiResult *r) {
    memset(io, 0, sizeof *io);
    io->s = xml;
    io->n = n;
    io->chunk = chunk;
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = font;
    opt.temp_dir = "/tmp";
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = io;
    return ki_convert(&opt, r);
}

/* Counts needle in the PDF with every Flate stream decoded. A PDF the
   independent reader rejects aborts the run instead of counting zero. */
static size_t fm_count(const char *raw, size_t raw_len, const char *needle) {
    char *buf = NULL;
    size_t len = 0;
    if (pdf_inflate_copy(raw, raw_len, &buf, &len, NULL) != 0) {
        fprintf(stderr, "FAIL fm_count: PDF streams do not decode\n");
        abort();
    }
    size_t k = strlen(needle), c = 0;
    for (size_t i = 0; i + k <= len; i++) {
        if (memcmp(buf + i, needle, k) == 0) {
            c++;
        }
    }
    free(buf);
    return c;
}

static int fm_has(const MemIo *io, const char *needle) {
    return io->buf != NULL && fm_count(io->buf, io->len, needle) > 0;
}

static int fm_is_fallback(const char *name) {
    if (strncmp(name, "KiFont-", 7) != 0 || strlen(name) != 15) {
        return 0;
    }
    for (int i = 7; i < 15; i++) {
        if (strchr("0123456789ABCDEF", name[i]) == NULL) {
            return 0;
        }
    }
    return 1;
}

static int test_font_names(void) {
    FontFace face;
    CHECK(font_load(&face, "fonts/DejaVuSans.ttf") == 0);
    CHECK(strcmp(face.name, "DejaVuSans") == 0 && face.name_fallback == 0);
    font_free(&face);
    const char *bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    if (access(bold, R_OK) == 0) {
        CHECK(font_load(&face, bold) == 0);
        CHECK(strcmp(face.name, "DejaVuSans-Bold") == 0 && face.name_fallback == 0);
        font_free(&face);
    }

    FmSpec spec;
    char name[128];
    int fb = -1;
    unsigned char u[256];

    fm_spec_ps(&spec, "KiTest-Alpha");
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, "KiTest-Alpha") == 0 && fb == 0);

    /* Macintosh Roman record alone, and the Windows record winning over it. */
    fm_spec(&spec);
    FmName mac = {1, 0, 0, 6, (const unsigned char *)"KiTest-Beta", 11};
    spec.name_len = fm_name(spec.name, &mac, 1);
    spec.has_name = 1;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, "KiTest-Beta") == 0 && fb == 0);
    FmName both[3] = {{1, 0, 0, 6, (const unsigned char *)"KiTest-Mac", 10},
                      {3, 1, 0x409, 1, u, fm_utf16(u, "Family")},
                      {3, 1, 0x409, 6, u, fm_utf16(u, "KiTest-Win")}};
    spec.name_len = fm_name(spec.name, both, 3);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, "KiTest-Win") == 0);

    /* 63 characters is the longest accepted name; 64 falls back. */
    char longest[65];
    memset(longest, 'N', 64);
    longest[63] = '\0';
    fm_spec_ps(&spec, longest);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, longest) == 0 && fb == 0);
    longest[63] = 'N';
    longest[64] = '\0';
    fm_spec_ps(&spec, longest);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name) && fb == 1);

    /* Deterministic fallback: no name table, empty name, a control
       character, a non-ASCII UTF-16 unit, an odd UTF-16 length. */
    char first[128];
    fm_spec(&spec);
    CHECK(fm_load(&spec, first, sizeof first, &fb) == 0 && fm_is_fallback(first) && fb == 1);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, first) == 0);
    fm_spec_ps(&spec, "");
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name));
    CHECK(strcmp(name, first) != 0);
    fm_spec_ps(&spec, "Ki\x01Name");
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name));
    fm_spec(&spec);
    size_t un = fm_utf16(u, "KiCyr");
    put16(u + 4, 0x0416);
    FmName cyr = {3, 1, 0x409, 6, u, un};
    spec.name_len = fm_name(spec.name, &cyr, 1);
    spec.has_name = 1;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name));
    cyr.n = un - 1;
    spec.name_len = fm_name(spec.name, &cyr, 1);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name));
    /* Delimiters, space and # are kept in the decoded name. */
    fm_spec_ps(&spec, "Ki Evil/Name(#)>>");
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && strcmp(name, "Ki Evil/Name(#)>>") == 0);

    /* Malformed name tables reject the font. */
    fm_spec_ps(&spec, "KiTest-Alpha");
    size_t good = spec.name_len;
    for (size_t len = 0; len < 6; len++) {
        spec.name_len = len;
        CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    }
    spec.name_len = 6 + 11;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    spec.name_len = good - 1;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    spec.name_len = good;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0);
    put16(spec.name + 2, 0xFFFF);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    fm_spec_ps(&spec, "KiTest-Alpha");
    put16(spec.name + 4, 0xFFFF);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    fm_spec_ps(&spec, "KiTest-Alpha");
    put16(spec.name + 4, (unsigned)good + 1);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    /* The record's string is 24 bytes at offset 0 of a 24-byte storage. */
    const unsigned bad_off[] = {0xFFFF, 0xFFFE, 0x8000, 1};
    const unsigned bad_len[] = {0xFFFF, 0x8000, 25, 0xFFFE};
    for (size_t i = 0; i < sizeof bad_off / sizeof bad_off[0]; i++) {
        fm_spec_ps(&spec, "KiTest-Alpha");
        put16(spec.name + 6 + 10, bad_off[i]);
        CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
        fm_spec_ps(&spec, "KiTest-Alpha");
        put16(spec.name + 6 + 8, bad_len[i]);
        CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    }
    fm_spec_ps(&spec, "KiTest-Alpha");
    put16(spec.name + 6 + 10, 24);
    put16(spec.name + 6 + 8, 0);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0 && fm_is_fallback(name));
    /* An out-of-range record that is not the PostScript name also rejects. */
    fm_spec(&spec);
    spec.name_len = fm_name(spec.name, both, 3);
    spec.has_name = 1;
    put16(spec.name + 6 + 12 + 10, 0x8000);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);

    /* The PDF carries the font's own name, escaped as a PDF name. */
    static unsigned char ttf[FM_CAP];
    char path[64];
    MemIo io;
    KiResult res;
    const char *doc = "<A>A A</A>";
    fm_spec_ps(&spec, "Ki Evil/Name(#)>>");
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    CHECK(fm_convert(doc, strlen(doc), 0, path, &io, &res) == XML2PDF_OK);
    unlink(path);
    CHECK(fm_count(io.buf, io.len, "/BaseFont /Ki#20Evil#2FName#28#23#29#3E#3E\n") == 2);
    CHECK(fm_has(&io, "/FontName /Ki#20Evil#2FName#28#23#29#3E#3E "));
    CHECK(!fm_has(&io, "DejaVu"));
    free(io.buf);

    fm_spec(&spec);
    CHECK(fm_load(&spec, first, sizeof first, &fb) == 0);
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    CHECK(fm_convert(doc, strlen(doc), 0, path, &io, &res) == XML2PDF_OK);
    unlink(path);
    char want[160];
    snprintf(want, sizeof want, "/BaseFont /%s\n", first);
    CHECK(fm_count(io.buf, io.len, want) == 2);
    CHECK(!fm_has(&io, "DejaVu"));
    free(io.buf);

    CHECK(fm_convert(doc, strlen(doc), 0, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(fm_count(io.buf, io.len, "/BaseFont /DejaVuSans\n") == 2);
    CHECK(fm_has(&io, "/FontName /DejaVuSans "));
    free(io.buf);
    if (access(bold, R_OK) == 0) {
        CHECK(fm_convert(doc, strlen(doc), 0, bold, &io, &res) == XML2PDF_OK);
        CHECK(fm_count(io.buf, io.len, "/BaseFont /DejaVuSans-Bold\n") == 2);
        CHECK(fm_has(&io, "/FontName /DejaVuSans-Bold "));
        free(io.buf);
    }

    /* A malformed explicit font fails FONT instead of using the default. */
    fm_spec_ps(&spec, "KiTest-Alpha");
    put16(spec.name + 4, 0xFFFF);
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    CHECK(fm_convert(doc, strlen(doc), 0, path, &io, &res) == XML2PDF_ERR_XML);
    CHECK(res.cause == KI_CAUSE_FONT && io.len == 0);
    unlink(path);
    free(io.buf);
    return 0;
}

static int test_font_zero_advance(void) {
    FontFace face;
    uint16_t gid = 0, adv = 0;
    CHECK(font_load(&face, "fonts/DejaVuSans.ttf") == 0);
    CHECK(font_lookup(&face, 0x65, &gid, &adv) == 0 && gid == 72 && adv == 1260);
    CHECK(font_lookup(&face, 0x301, &gid, &adv) == 0 && gid == 690 && adv == 0);
    CHECK(font_lookup(&face, 0x306, &gid, &adv) == 0 && gid == 695 && adv == 0);
    CHECK(font_lookup(&face, 0x438, &gid, &adv) == 0 && gid == 973 && adv == 1331);
    CHECK(font_lookup(&face, 0x483, &gid, &adv) == 0 && gid == 1048 && adv == 0);
    CHECK(font_lookup(&face, 0x1E00, &gid, &adv) != 0);
    font_free(&face);

    FmSpec spec;
    static unsigned char ttf[FM_CAP];
    char path[64];
    fm_spec(&spec);
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    CHECK(font_load(&face, path) == 0);
    CHECK(font_lookup(&face, 0x300, &gid, &adv) == 0 && gid == 3 && adv == 0);
    CHECK(font_lookup(&face, 0x21, &gid, &adv) == 0 && gid == 4 && adv == 0);
    CHECK(font_lookup(&face, 0x41, &gid, &adv) == 0 && gid == 2 && adv == 600);
    CHECK(font_lookup(&face, 0x302, &gid, &adv) != 0);
    CHECK(font_lookup(&face, 0x303, &gid, &adv) != 0);
    CHECK(font_lookup(&face, 0x42, &gid, &adv) != 0);
    font_free(&face);

    MemIo io;
    KiResult res;
    const char *mark_missing = "<A>A\xCC\x82</A>";
    CHECK(fm_convert(mark_missing, strlen(mark_missing), 1, path, &io, &res) == XML2PDF_ERR_XML);
    CHECK(res.cause == KI_CAUSE_FONT && strstr(res.message, "U+0302") != NULL);
    free(io.buf);
    const char *gid_zero = "<A>A\xCC\x83</A>";
    CHECK(fm_convert(gid_zero, strlen(gid_zero), 1, path, &io, &res) == XML2PDF_ERR_XML);
    CHECK(res.cause == KI_CAUSE_FONT && strstr(res.message, "U+0303") != NULL);
    free(io.buf);
    const char *present = "<A>A\xCC\x80!</A>";
    CHECK(fm_convert(present, strlen(present), 1, path, &io, &res) == XML2PDF_OK);
    CHECK(fm_has(&io, "\n3 [0]\n") && fm_has(&io, "\n4 [0]\n") && fm_has(&io, "\n2 [292]\n"));
    CHECK(fm_has(&io, "<0003> <0300>") && fm_has(&io, "<000200030004> Tj"));
    free(io.buf);
    unlink(path);

    /* maxp: absent leaves GID 5 to hmtx's last metric; truncated or zero
       glyphs rejects the font. */
    char name[128];
    int fb;
    spec.has_maxp = 0;
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    CHECK(font_load(&face, path) == 0);
    CHECK(font_lookup(&face, 0x302, &gid, &adv) == 0 && gid == 5 && adv == 0);
    font_free(&face);
    unlink(path);
    fm_spec(&spec);
    spec.maxp_len = 5;
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    fm_spec(&spec);
    put16(spec.maxp + 4, 0);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == -1);
    fm_spec(&spec);
    put16(spec.maxp + 4, 3);
    CHECK(fm_load(&spec, name, sizeof name, &fb) == 0);

    /* Bundled font: base + mark pairs and an isolated mark. */
    const char *doc = "<doc>e\xCC\x81 \xD0\xB8\xCC\x86\n\xCC\x81</doc>";
    CHECK(fm_convert(doc, strlen(doc), 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(fm_has(&io, "\n690 [0]\n") && fm_has(&io, "\n695 [0]\n") && fm_has(&io, "\n72 [615]\n"));
    CHECK(fm_has(&io, "<02B2> <0301>") && fm_has(&io, "<02B7> <0306>"));
    CHECK(fm_count(io.buf, io.len, "<02B2> <") == 1);
    CHECK(fm_has(&io, "<004802B2000303CD02B7> Tj") && fm_has(&io, "<02B2> Tj"));
    CHECK(fm_has(&io, "/ActualText <FEFF00650301002004380306>") && fm_has(&io, "/ActualText <FEFF0301>"));
    free(io.buf);

    const char *outside = "<doc>e\xE1\xB8\x80</doc>";
    CHECK(fm_convert(outside, strlen(outside), 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_ERR_XML);
    CHECK(res.cause == KI_CAUSE_FONT && strstr(res.message, "U+1E00") != NULL);
    free(io.buf);
    return 0;
}

static char *fm_doc(const char *pre, const char *unit, size_t reps, const char *post, size_t *n) {
    size_t a = strlen(pre), u = strlen(unit), z = strlen(post);
    char *s = malloc(a + u * reps + z + 1);
    if (s == NULL) {
        return NULL;
    }
    memcpy(s, pre, a);
    for (size_t i = 0; i < reps; i++) {
        memcpy(s + a + u * i, unit, u);
    }
    memcpy(s + a + u * reps, post, z);
    s[a + u * reps + z] = '\0';
    *n = a + u * reps + z;
    return s;
}

static int fm_same_pdf(const char *x, size_t xn, const char *y, size_t yn, int chunk) {
    MemIo a, b;
    KiResult ra, rb;
    int ok = fm_convert(x, xn, 0, "fonts/DejaVuSans.ttf", &a, &ra) == XML2PDF_OK &&
             fm_convert(y, yn, chunk, "fonts/DejaVuSans.ttf", &b, &rb) == XML2PDF_OK && a.len == b.len &&
             memcmp(a.buf, b.buf, a.len) == 0;
    free(a.buf);
    free(b.buf);
    return ok;
}

static int test_font_mark_runs(void) {
    MemIo io;
    KiResult res;
    size_t n;
    char *s;

    /* "a" + 511 marks is 1023 bytes, the line capacity. */
    s = fm_doc("<doc>a", "\xCC\x81", 511, "</doc>", &n);
    CHECK(s != NULL);
    CHECK(fm_convert(s, n, 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(res.pages == 1 && fm_count(io.buf, io.len, "02B2") >= 511);
    free(io.buf);
    free(s);
    s = fm_doc("<doc>a", "\xCC\x81", 512, "</doc>", &n);
    CHECK(fm_convert(s, n, 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_ERR_RESOURCE);
    CHECK(res.cause == KI_CAUSE_RESOURCE && strstr(res.message, "1023") != NULL);
    free(io.buf);
    free(s);
    /* Same capacity through the report-v1 mapper (ki_doc_text). */
    s = fm_doc("<report version=\"1\" title=\"t\"><record><field name=\"n\">a", "\xCC\x81", 600,
               "</field></record></report>", &n);
    {
        memset(&io, 0, sizeof io);
        io.s = s;
        io.n = n;
        io.chunk = 1;
        KiOptions opt;
        ki_options_init(&opt);
        opt.font_path = "fonts/DejaVuSans.ttf";
        opt.profile = KI_PROFILE_REPORT;
        opt.read_bytes = mem_read;
        opt.write_bytes = mem_write;
        opt.io_user = &io;
        CHECK(ki_convert(&opt, &res) == XML2PDF_ERR_RESOURCE);
        CHECK(res.cause == KI_CAUSE_RESOURCE && strstr(res.message, "1023") != NULL);
        free(io.buf);
    }
    free(s);

    /* Text, CDATA, references and one-byte reads give the same PDF. */
    s = fm_doc("<doc>a", "\xCC\x81", 300, "</doc>", &n);
    char *t = fm_doc("<doc>a", "&#x301;<![CDATA[\xCC\x81]]>\xCC\x81", 100, "</doc>", &n);
    size_t sn = strlen(s), tn = strlen(t);
    CHECK(fm_same_pdf(s, sn, s, sn, 1));
    CHECK(fm_same_pdf(s, sn, t, tn, 1));
    CHECK(fm_same_pdf(s, sn, t, tn, 3));
    free(s);
    free(t);

    /* Width wrapping never separates a mark from its base. */
    s = fm_doc("<doc>", "e\xCC\x81", 200, "</doc>", &n);
    CHECK(fm_convert(s, n, 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(fm_count(io.buf, io.len, "BDC\n<02B2") == 0 && fm_count(io.buf, io.len, "02B2") >= 200);
    CHECK(fm_count(io.buf, io.len, "> Tj") >= 4);
    free(io.buf);
    free(s);

    /* Marks across many lines and pages. */
    s = fm_doc("<doc>", "<r>\xD0\xB8\xCC\x86\xCC\x81</r>", 120, "</doc>", &n);
    CHECK(fm_convert(s, n, 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(res.pages == 5 && fm_count(io.buf, io.len, "<03CD02B702B2> Tj") == 120);
    free(io.buf);
    free(s);

    /* 50 full lines of 1-byte zero-advance glyphs fit one page stream. */
    FmSpec spec;
    static unsigned char ttf[FM_CAP];
    char path[64];
    fm_spec(&spec);
    CHECK(fm_write_font(ttf, fm_font(ttf, &spec), path) == 0);
    char unit[1100];
    unit[0] = 'A';
    memset(unit + 1, '!', 1022);
    memcpy(unit + 1023, "\n", 2);
    s = fm_doc("<A>", unit, 49, "</A>", &n);
    CHECK(fm_convert(s, n, 4096, path, &io, &res) == XML2PDF_OK);
    CHECK(res.pages == 1);
    free(io.buf);
    free(s);
    unlink(path);
    return 0;
}
