/* Page geometry: KiConfig sizing and validation before any I/O, the shared
   MediaBox/placement model, and ink-safe placement with a synthetic outline
   font. Included from test_stream.c after test_layout_wrap.c, which provides
   fm_build/fm_cmap/fm_write_font, fm_count/fm_has and MemIo helpers.

   Expected numbers below follow from the fixture values by hand: the
   synthetic font has unitsPerEm 1000, so at 10 pt one font unit is 0.01 pt. */

#include <dirent.h>

#define GM_GLYPHS 7

/* GID i + 1 maps gm_cps[i]; box is xMin, yMin, xMax, yMax in font units. A
   glyph with an all-zero box has no outline. */
static const unsigned gm_cps[GM_GLYPHS - 1] = {0x20, 0x41, 0x4A, 0x57, 0x301, 0x302};
static const unsigned gm_adv[GM_GLYPHS - 1] = {250, 500, 500, 500, 0, 0};
static const int gm_box[GM_GLYPHS - 1][4] = {
    {0, 0, 0, 0},           /* space: empty */
    {0, 0, 500, 700},       /* A: inside its advance */
    {-150, -300, 450, 700}, /* J: negative left side bearing, deep descender */
    {0, 0, 700, 950},       /* W: ink 200 units past its advance, tall */
    {-400, 500, -100, 800}, /* U+0301: mark over the previous base */
    {0, 500, 5000, 800},    /* U+0302: mark reaching 50 pt to the right */
};

typedef struct {
    int loca_format;   /* head indexToLocFormat */
    int drop_loca;
    int loca_past_end; /* last glyph's end beyond glyf */
    int short_glyph;   /* 'A' outline shorter than its 10-byte header */
    int inverted;      /* 'A' box with xMin > xMax */
} GmSpec;

static int gm_font(const GmSpec *spec, char *path) {
    static unsigned char head[54], hhea[36], maxp[6], hmtx[4 * GM_GLYPHS], cmap[256], loca[4 * (GM_GLYPHS + 1)],
        glyf[12 * GM_GLYPHS], ttf[FM_CAP];
    memset(head, 0, sizeof head);
    put16(head + 18, 1000);
    put16(head + 50, (unsigned)spec->loca_format);
    memset(hhea, 0, sizeof hhea);
    put16(hhea + 4, 800);
    put16(hhea + 6, 0xFF38);
    put16(hhea + 34, GM_GLYPHS);
    put32(maxp, 0x00005000ul);
    put16(maxp + 4, GM_GLYPHS);
    memset(hmtx, 0, sizeof hmtx);
    put16(hmtx, 500);
    memset(glyf, 0, sizeof glyf);
    unsigned gids[GM_GLYPHS - 1];
    size_t at = 0;
    size_t offs[GM_GLYPHS + 1];
    offs[0] = 0;
    offs[1] = 0;
    for (int i = 0; i < GM_GLYPHS - 1; i++) {
        put16(hmtx + 4 * (i + 1), gm_adv[i]);
        gids[i] = (unsigned)i + 1;
        const int *b = gm_box[i];
        if (b[0] != 0 || b[1] != 0 || b[2] != 0 || b[3] != 0) {
            int x0 = b[0], x1 = b[2];
            if (spec->inverted && gm_cps[i] == 0x41) {
                x0 = b[2];
                x1 = b[0];
            }
            put16(glyf + at, 1);
            put16(glyf + at + 2, (unsigned)x0 & 0xFFFFu);
            put16(glyf + at + 4, (unsigned)b[1] & 0xFFFFu);
            put16(glyf + at + 6, (unsigned)x1 & 0xFFFFu);
            put16(glyf + at + 8, (unsigned)b[3] & 0xFFFFu);
            at += spec->short_glyph && gm_cps[i] == 0x41 ? 8 : 12;
        }
        offs[i + 2] = at;
    }
    if (spec->loca_past_end) {
        offs[GM_GLYPHS] = at + 12;
    }
    size_t loca_len = 0;
    for (int i = 0; i <= GM_GLYPHS; i++) {
        if (spec->loca_format == 1) {
            put32(loca + 4 * i, offs[i]);
            loca_len = 4 * (size_t)(GM_GLYPHS + 1);
        } else {
            put16(loca + 2 * i, (unsigned)(offs[i] / 2));
            loca_len = 2 * (size_t)(GM_GLYPHS + 1);
        }
    }
    size_t cl = fm_cmap(cmap, gm_cps, gids, GM_GLYPHS - 1);
    FmTable t[7] = {{"cmap", cmap, cl},          {"glyf", glyf, at},           {"head", head, sizeof head},
                    {"hhea", hhea, sizeof hhea}, {"hmtx", hmtx, sizeof hmtx}, {"loca", loca, loca_len},
                    {"maxp", maxp, sizeof maxp}};
    int n = 7;
    if (spec->drop_loca) {
        t[5] = t[6];
        n = 6;
    }
    return fm_write_font(ttf, fm_build(ttf, t, n), path);
}

typedef struct {
    MemIo io;
    int reads;
    int writes;
    int maps;
    int cancels;
} GmIo;

static int gm_read(void *user, char *dst, int max) {
    GmIo *g = user;
    g->reads++;
    return mem_read(&g->io, dst, max);
}

static int gm_write(void *user, const char *src, int n) {
    GmIo *g = user;
    g->writes++;
    return mem_write(&g->io, src, n);
}

static int gm_cancel(void *user) {
    ((GmIo *)user)->cancels++;
    return 0;
}

static int gm_map(void *user, KiDoc *doc, const KiEvent *ev) {
    GmIo *g = user;
    g->maps++;
    if (ev->kind == KI_EV_TEXT) {
        return ki_doc_text(doc, ev->text, ev->text_len);
    }
    return ev->kind == KI_EV_END ? ki_doc_break(doc) : 0;
}

static void gm_options(KiOptions *opt, GmIo *g, const char *font, int custom_map) {
    ki_options_init(opt);
    opt->font_path = font;
    opt->temp_dir = "/tmp";
    opt->read_bytes = gm_read;
    opt->write_bytes = gm_write;
    opt->io_user = g;
    opt->cancelled = gm_cancel;
    opt->cancel_user = g;
    if (custom_map) {
        opt->map = gm_map;
        opt->map_user = g;
    }
}

/* Callback conversion of xml with cfg; the PDF stays in g->io.buf. */
static int gm_convert(const char *xml, const char *font, const KiConfig *cfg, GmIo *g, KiResult *r) {
    memset(g, 0, sizeof *g);
    g->io.s = xml;
    g->io.n = strlen(xml);
    KiOptions opt;
    gm_options(&opt, g, font, 0);
    return ki_convert_cfg(&opt, cfg, r);
}

static int gm_dir_entries(const char *dir) {
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

static int gm_file_is(const char *path, const char *want) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    char buf[64];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    return n == strlen(want) && memcmp(buf, want, n) == 0;
}

static int test_geometry_config(void) {
    unsigned char raw[sizeof(KiConfig) + 16];
    memset(raw, 0xAA, sizeof raw);
    KiConfig *cfg = (KiConfig *)(void *)raw;
    CHECK(ki_config_init(NULL, sizeof(KiConfig)) == XML2PDF_ERR_USAGE);
    const size_t bad[] = {0, 1, sizeof(KiConfig) - 1, sizeof(KiConfig) + 1, sizeof(KiConfig) + 16};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(ki_config_init(cfg, bad[i]) == XML2PDF_ERR_USAGE);
        for (size_t k = 0; k < sizeof raw; k++) {
            CHECK(raw[k] == 0xAA);
        }
    }
    CHECK(ki_config_init(cfg, sizeof(KiConfig)) == 0);
    for (size_t k = sizeof(KiConfig); k < sizeof raw; k++) {
        CHECK(raw[k] == 0xAA);
    }
    CHECK(cfg->size == sizeof(KiConfig));
    CHECK(cfg->page_size == KI_PAGE_LETTER && cfg->orientation == KI_ORIENT_PORTRAIT);
    CHECK(cfg->page_width == 0.0 && cfg->page_height == 0.0);
    CHECK(cfg->margin_top == 40.0 && cfg->margin_right == 40.0 && cfg->margin_bottom == 40.0 &&
          cfg->margin_left == 40.0);
    CHECK(cfg->font_size == 10.0);
    return 0;
}

typedef struct {
    const char *name;
    int field; /* 0 size, 1 page, 2 orient, 3 w, 4 h, 5..8 margins, 9 font */
    double value;
    int page;
} GmBad;

static void gm_apply(KiConfig *cfg, const GmBad *b) {
    ki_config_init(cfg, sizeof *cfg);
    if (b->page == KI_PAGE_CUSTOM) {
        cfg->page_size = KI_PAGE_CUSTOM;
        cfg->page_width = 360;
        cfg->page_height = 504;
    }
    double *m[4] = {&cfg->margin_top, &cfg->margin_right, &cfg->margin_bottom, &cfg->margin_left};
    switch (b->field) {
    case 0: cfg->size = (size_t)b->value; break;
    case 1: cfg->page_size = (int)b->value; break;
    case 2: cfg->orientation = (int)b->value; break;
    case 3: cfg->page_width = b->value; break;
    case 4: cfg->page_height = b->value; break;
    case 9: cfg->font_size = b->value; break;
    case 10: /* left + right consume the width */
        cfg->margin_left = 306;
        cfg->margin_right = b->value;
        break;
    case 11:
        cfg->margin_top = 396;
        cfg->margin_bottom = b->value;
        break;
    case 12: /* valid statically, but no glyph of the font fits */
        cfg->margin_left = cfg->margin_right = b->value;
        cfg->font_size = 144;
        break;
    case 13:
        cfg->page_size = KI_PAGE_CUSTOM;
        cfg->page_width = cfg->page_height = 3;
        cfg->margin_top = cfg->margin_right = cfg->margin_bottom = cfg->margin_left = 0;
        cfg->font_size = b->value;
        break;
    default: *m[b->field - 5] = b->value; break;
    }
}

/* Every invalid configuration fails as USAGE through all three entry points
   with no callback, no write and an untouched destination directory. */
static int test_geometry_usage(void) {
    double nan = strtod("nan", NULL);
    double inf = strtod("inf", NULL);
    const GmBad bad[] = {
        {"size-1", 0, (double)(sizeof(KiConfig) - 1), KI_PAGE_LETTER},
        {"size+8", 0, (double)(sizeof(KiConfig) + 8), KI_PAGE_LETTER},
        {"size0", 0, 0, KI_PAGE_LETTER},
        {"page3", 1, 3, KI_PAGE_LETTER},
        {"page-1", 1, -1, KI_PAGE_LETTER},
        {"orient2", 2, 2, KI_PAGE_LETTER},
        {"named+width", 3, 100, KI_PAGE_LETTER},
        {"named+height", 4, 1, KI_PAGE_LETTER},
        {"custom+landscape", 2, KI_ORIENT_LANDSCAPE, KI_PAGE_CUSTOM},
        {"width2.999", 3, 2.999, KI_PAGE_CUSTOM},
        {"width14400.001", 3, 14400.001, KI_PAGE_CUSTOM},
        {"width0", 3, 0, KI_PAGE_CUSTOM},
        {"width-1", 3, -1, KI_PAGE_CUSTOM},
        {"widthNaN", 3, nan, KI_PAGE_CUSTOM},
        {"width+inf", 3, inf, KI_PAGE_CUSTOM},
        {"height-inf", 4, -inf, KI_PAGE_CUSTOM},
        {"height1e300", 4, 1e300, KI_PAGE_CUSTOM},
        {"top-0.001", 5, -0.001, KI_PAGE_LETTER},
        {"rightNaN", 6, nan, KI_PAGE_LETTER},
        {"bottom+inf", 7, inf, KI_PAGE_LETTER},
        {"left14400.001", 8, 14400.001, KI_PAGE_LETTER},
        {"font0", 9, 0, KI_PAGE_LETTER},
        {"font0.999", 9, 0.999, KI_PAGE_LETTER},
        {"font144.001", 9, 144.001, KI_PAGE_LETTER},
        {"font-10", 9, -10, KI_PAGE_LETTER},
        {"fontNaN", 9, nan, KI_PAGE_LETTER},
        {"font+inf", 9, inf, KI_PAGE_LETTER},
        {"left+right=width", 10, 306, KI_PAGE_LETTER},
        {"top+bottom=height", 11, 396, KI_PAGE_LETTER},
        {"glyph-too-wide", 12, 290, KI_PAGE_LETTER},
        {"no-line-3pt", 13, 10, KI_PAGE_CUSTOM},
    };
    const char *xml = "<A>AAAA</A>";
    char dir[] = "/tmp/ki-geometry-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char in_path[96], dest[96];
    snprintf(in_path, sizeof in_path, "%s/in.xml", dir);
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    FILE *fp = fopen(in_path, "wb");
    CHECK(fp != NULL && fputs(xml, fp) >= 0 && fclose(fp) == 0);
    fp = fopen(dest, "wb");
    CHECK(fp != NULL && fputs("OLD", fp) >= 0 && fclose(fp) == 0);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        for (int api = 0; api < 3; api++) {
            for (int custom = 0; custom < 2; custom++) {
                KiConfig cfg;
                gm_apply(&cfg, &bad[i]);
                GmIo g;
                memset(&g, 0, sizeof g);
                g.io.s = xml;
                g.io.n = strlen(xml);
                KiOptions opt;
                gm_options(&opt, &g, "fonts/DejaVuSans.ttf", custom);
                KiResult r;
                memset(&r, 0x55, sizeof r);
                int rc = api == 0   ? ki_convert_cfg(&opt, &cfg, &r)
                         : api == 1 ? ki_convert_to_file_cfg(dest, &opt, &cfg, &r)
                                    : ki_convert_file_cfg(in_path, dest, &opt, &cfg, &r);
                int ok = rc == XML2PDF_ERR_USAGE && r.status == XML2PDF_ERR_USAGE && r.cause == KI_CAUSE_USAGE &&
                         r.output_bytes == 0 && r.pages == 0 && g.reads == 0 && g.writes == 0 && g.maps == 0 &&
                         g.cancels == 0 && g.io.len == 0 && gm_file_is(dest, "OLD") && gm_dir_entries(dir) == 2 &&
                         (strncmp(r.message, "page geometry: ", 15) == 0 ||
                          strcmp(r.message, "config size mismatch") == 0);
                if (!ok) {
                    fprintf(stderr, "geometry usage %s api=%d custom=%d rc=%d cause=%d reads=%d writes=%d maps=%d (%s)\n",
                            bad[i].name, api, custom, rc, r.cause, g.reads, g.writes, g.maps, r.message);
                    return 1;
                }
                free(g.io.buf);
            }
        }
    }
    unlink(in_path);
    unlink(dest);
    rmdir(dir);
    return 0;
}

/* The MediaBox of every page, raw (page objects are not compressed). */
static int gm_media(const GmIo *g, const char *want) {
    char needle[96];
    snprintf(needle, sizeof needle, "/MediaBox [0 0 %s]", want);
    size_t k = strlen(needle);
    size_t pages = 0, hits = 0;
    for (size_t i = 0; i + 11 <= g->io.len; i++) {
        if (memcmp(g->io.buf + i, "/Type /Page ", 12) == 0) {
            pages++;
        }
        if (i + k <= g->io.len && memcmp(g->io.buf + i, needle, k) == 0) {
            hits++;
        }
    }
    return pages > 0 && pages == hits;
}

typedef struct {
    int page, orient;
    double w, h;
    double m[4];
    double font;
    const char *media;
    const char *prefix; /* start of the page content: font, leading, first move x */
} GmCase;

/* Named and explicit pages: MediaBox and the content's font, leading and
   left edge agree with the configuration, and the first baseline leaves
   the ink ascent below the top margin. */
static int test_geometry_matrix(void) {
    const GmCase cases[] = {
        {KI_PAGE_LETTER, 0, 0, 0, {40, 40, 40, 40}, 10, "612 792", "BT\n/F1 10 Tf\n14 TL\n40 "},
        {KI_PAGE_LETTER, 1, 0, 0, {40, 40, 40, 40}, 10, "792 612", "BT\n/F1 10 Tf\n14 TL\n40 "},
        {KI_PAGE_A4, 0, 0, 0, {40, 40, 40, 40}, 10, "595.276 841.89", "BT\n/F1 10 Tf\n14 TL\n40 "},
        {KI_PAGE_A4, 1, 0, 0, {40, 40, 40, 40}, 12, "841.89 595.276", "BT\n/F1 12 Tf\n16.8 TL\n40 "},
        {KI_PAGE_CUSTOM, 0, 360, 504, {24, 24, 24, 24}, 10, "360 504", "BT\n/F1 10 Tf\n14 TL\n24 "},
        {KI_PAGE_CUSTOM, 0, 504, 360, {24, 24, 24, 24}, 12, "504 360", "BT\n/F1 12 Tf\n16.8 TL\n24 "},
        {KI_PAGE_CUSTOM, 0, 360, 504, {0, 0, 0, 0}, 10, "360 504", "BT\n/F1 10 Tf\n14 TL\n0 "},
        {KI_PAGE_CUSTOM, 0, 360.0004, 503.9996, {10, 20, 30, 40.25}, 10.5, "360 504",
         "BT\n/F1 10.5 Tf\n14.7 TL\n40.25 "},
        {KI_PAGE_CUSTOM, 0, 14400, 14400, {0, 0, 0, 0}, 144, "14400 14400", "BT\n/F1 144 Tf\n201.6 TL\n0 "},
        {KI_PAGE_CUSTOM, 0, 3, 3, {0, 0, 0, 0}, 1, "3 3", "BT\n/F1 1 Tf\n1.4 TL\n0 "},
    };
    FontFace face;
    CHECK(font_load(&face, "fonts/DejaVuSans.ttf") == 0);
    int asc = 0, desc = 0;
    font_ink_vertical(&face, &asc, &desc);
    int units = face.units;
    font_free(&face);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const GmCase *c = &cases[i];
        KiConfig cfg;
        CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
        cfg.page_size = c->page;
        cfg.orientation = c->orient;
        cfg.page_width = c->w;
        cfg.page_height = c->h;
        cfg.margin_top = c->m[0];
        cfg.margin_right = c->m[1];
        cfg.margin_bottom = c->m[2];
        cfg.margin_left = c->m[3];
        cfg.font_size = c->font;
        GmIo g;
        KiResult r;
        int rc = gm_convert("<A>A</A>", "fonts/DejaVuSans.ttf", &cfg, &g, &r);
        if (rc != 0 || !gm_media(&g, c->media) || !fm_has(&g.io, c->prefix)) {
            fprintf(stderr, "geometry case %zu rc=%d (%s)\n", i, rc, r.message);
            free(g.io.buf);
            return 1;
        }
        char *buf = NULL;
        size_t len = 0;
        CHECK(pdf_inflate_copy(g.io.buf, g.io.len, &buf, &len, NULL) == 0);
        const char *td = NULL;
        size_t k = strlen(c->prefix);
        for (size_t at = 0; td == NULL && at + k <= len; at++) {
            if (memcmp(buf + at, c->prefix, k) == 0) {
                td = buf + at;
            }
        }
        CHECK(td != NULL);
        double y = strtod(td + strlen(c->prefix), NULL);
        double top = (c->page == KI_PAGE_CUSTOM ? (c->h + 0.0005 > 503.9 && c->h < 504.1 ? 504 : c->h)
                      : c->page == KI_PAGE_A4 ? (c->orient ? 595.276 : 841.89)
                                              : (c->orient ? 612 : 792)) -
                     c->m[0];
        double up = (double)asc * c->font / units;
        double down = (double)desc * c->font / units;
        CHECK(y + up <= top + 1e-9);
        CHECK(y + up > top - 0.0011);
        CHECK(y - down >= c->m[2] - 1e-9);
        free(buf);
        free(g.io.buf);
    }
    return 0;
}

/* NULL, a default KiConfig and the 0.2 calls give identical bytes. */
static int test_geometry_legacy(void) {
    const char *xml = "<doc><p>legacy geometry stays Letter 612 x 792</p></doc>";
    MemIo old;
    KiResult r;
    CHECK(fm_convert(xml, strlen(xml), 0, "fonts/DejaVuSans.ttf", &old, &r) == 0);
    GmIo a, b;
    CHECK(gm_convert(xml, "fonts/DejaVuSans.ttf", NULL, &a, &r) == 0);
    KiConfig cfg;
    CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
    CHECK(gm_convert(xml, "fonts/DejaVuSans.ttf", &cfg, &b, &r) == 0);
    CHECK(old.len == a.io.len && memcmp(old.buf, a.io.buf, old.len) == 0);
    CHECK(old.len == b.io.len && memcmp(old.buf, b.io.buf, old.len) == 0);
    CHECK(gm_media(&b, "612 792"));
    CHECK(fm_has(&b.io, "BT\n/F1 10 Tf\n14 TL\n40 "));
    free(old.buf);
    free(a.io.buf);
    free(b.io.buf);
    return 0;
}

static int gm_with_font(const char *xml, const GmSpec *spec, const KiConfig *cfg, GmIo *g, KiResult *r) {
    char path[64];
    if (gm_font(spec, path) != 0) {
        return -100;
    }
    int rc = gm_convert(xml, path, cfg, g, r);
    unlink(path);
    return rc;
}

static int gm_count_in(const GmIo *g, const char *needle) {
    return g->io.buf != NULL ? (int)fm_count(g->io.buf, g->io.len, needle) : -1;
}

/* Synthetic outlines whose ink leaves the advance box: the line origin,
   the wrap point and the first baseline follow the ink. */
static int test_geometry_ink(void) {
    GmSpec spec;
    memset(&spec, 0, sizeof spec);
    for (int fmt = 0; fmt < 2; fmt++) {
        spec.loca_format = fmt;
        GmIo g;
        KiResult r;
        /* Root name line "A", then "J" whose ink starts 150 units (1.5 pt)
           left of its origin, then "A" again. W's yMax 950 sets the ascent:
           baseline 792 - 40 - 9.5. */
        CHECK(gm_with_font("<A>J<A>A</A></A>", &spec, NULL, &g, &r) == 0);
        CHECK(gm_count_in(&g, "40 742.5 Td\n") == 1);
        CHECK(gm_count_in(&g, "\n1.5 -14 Td\n") == 1);
        CHECK(gm_count_in(&g, "-1.5 -14 Td\n") == 1);
        free(g.io.buf);
    }
    spec.loca_format = 0;
    /* Right margin 41: 531 pt = 53100 units. 106 W advance to 53000, but the
       last W's ink ends at 53200; 105 W end at 52700. */
    LwBuf xml = {0};
    CHECK(lw_puts(&xml, "<A>") == 0 && lw_rep(&xml, "W", 106) == 0 && lw_puts(&xml, "</A>") == 0);
    KiConfig cfg;
    CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
    cfg.margin_right = 41;
    GmIo g;
    KiResult r;
    CHECK(gm_with_font(xml.s, &spec, &cfg, &g, &r) == 0);
    LwBuf lines;
    CHECK(lw_text(&g.io, &lines) == 0);
    LwBuf want = {0};
    CHECK(lw_puts(&want, "A\n") == 0 && lw_rep(&want, "W", 105) == 0 && lw_puts(&want, "\nW\n\f") == 0);
    CHECK(strcmp(lines.s, want.s) == 0);
    lw_free(&lines);
    lw_free(&want);
    free(g.io.buf);
    cfg.margin_right = 40;
    CHECK(gm_with_font(xml.s, &spec, &cfg, &g, &r) == 0);
    CHECK(lw_text(&g.io, &lines) == 0);
    CHECK(lw_puts(&want, "A\n") == 0 && lw_rep(&want, "W", 106) == 0 && lw_puts(&want, "\n\f") == 0);
    CHECK(strcmp(lines.s, want.s) == 0);
    lw_free(&lines);
    lw_free(&want);
    lw_free(&xml);
    free(g.io.buf);

    /* Lines per page from the ink: height 200, margins 40, ascent 9.5 and
       descent 3 (J's yMin -300): baselines 150.5 down to >= 43, so
       floor(107.5 / 14) + 1 = 8 lines. */
    CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
    cfg.page_size = KI_PAGE_CUSTOM;
    cfg.page_width = 300;
    cfg.page_height = 200;
    CHECK(lw_puts(&xml, "<A>") == 0 && lw_rep(&xml, "<A>A</A>", 20) == 0 && lw_puts(&xml, "</A>") == 0);
    CHECK(gm_with_font(xml.s, &spec, &cfg, &g, &r) == 0);
    CHECK(lw_text(&g.io, &lines) == 0);
    int first_page = 0;
    for (const char *p = lines.s; *p != '\0' && *p != '\f'; p++) {
        first_page += *p == '\n';
    }
    CHECK(first_page == 8);
    /* 41 lines (each child adds its name line): six pages. */
    CHECK(gm_count_in(&g, "40 150.5 Td\n") == 6);
    lw_free(&lines);
    lw_free(&xml);
    free(g.io.buf);

    /* U+0302's ink reaches 5000 units right of its pen. With 60 pt usable,
       "AA" + mark ends at 60 pt and fits; "AAA" + mark is cut before its
       base, keeping the mark with the last A. */
    CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
    cfg.page_size = KI_PAGE_CUSTOM;
    cfg.page_width = 140;
    cfg.page_height = 200;
    CHECK(gm_with_font("<A>AA\xCC\x82 AAA\xCC\x82</A>", &spec, &cfg, &g, &r) == 0);
    CHECK(lw_text(&g.io, &lines) == 0);
    CHECK(strcmp(lines.s, "A\nAA\xCC\x82 \nAA\nA\xCC\x82\n\f") == 0);
    lw_free(&lines);
    free(g.io.buf);
    /* 52 pt usable: the mark fits alone, but no line holds A + mark. */
    cfg.page_width = 132;
    int rc = gm_with_font("<A>A\xCC\x82</A>", &spec, &cfg, &g, &r);
    CHECK(rc == XML2PDF_ERR_RESOURCE && r.cause == KI_CAUSE_RESOURCE);
    CHECK(strcmp(r.message, "layout: glyph cluster wider than the line") == 0);
    free(g.io.buf);

    /* A glyph wider than the line (W's 700 units at 144 pt = 100.8 pt on
       60 pt) or no room for one line fails before any callback. */
    cfg.page_width = 140;
    cfg.font_size = 144;
    CHECK(gm_with_font("<A>A</A>", &spec, &cfg, &g, &r) == XML2PDF_ERR_USAGE);
    CHECK(r.cause == KI_CAUSE_USAGE && g.reads == 0 && g.writes == 0 && g.cancels == 0);
    /* Zero margins: 12.5 pt of ink (9.5 + 3) needs 12.5 pt of height. */
    CHECK(ki_config_init(&cfg, sizeof cfg) == 0);
    cfg.page_size = KI_PAGE_CUSTOM;
    cfg.page_width = 100;
    cfg.page_height = 12.499;
    cfg.margin_top = cfg.margin_right = cfg.margin_bottom = cfg.margin_left = 0;
    CHECK(gm_with_font("<A>A</A>", &spec, &cfg, &g, &r) == XML2PDF_ERR_USAGE);
    CHECK(r.cause == KI_CAUSE_USAGE && g.reads == 0 && g.writes == 0);
    cfg.page_height = 12.5;
    CHECK(gm_with_font("<A>J</A>", &spec, &cfg, &g, &r) == 0);
    CHECK(gm_count_in(&g, "0 3 Td\n") == 1 && gm_count_in(&g, "\n1.5 -14 Td\n") == 0);
    CHECK(gm_media(&g, "100 12.5"));
    free(g.io.buf);
    return 0;
}

/* glyf and loca must agree and stay inside their tables. */
static int test_geometry_outline_bounds(void) {
    GmSpec spec;
    GmIo g;
    KiResult r;
    for (int k = 0; k < 6; k++) {
        memset(&spec, 0, sizeof spec);
        switch (k) {
        case 0: spec.drop_loca = 1; break;
        case 1: spec.loca_format = 2; break;
        case 2: spec.loca_past_end = 1; break;
        case 3: spec.short_glyph = 1; break;
        case 4: spec.inverted = 1; break;
        case 5: spec.loca_past_end = 1; spec.loca_format = 1; break;
        }
        int rc = gm_with_font("<A>A</A>", &spec, NULL, &g, &r);
        if (rc != XML2PDF_ERR_XML || r.cause != KI_CAUSE_FONT || g.writes != 0) {
            fprintf(stderr, "outline case %d rc=%d cause=%d\n", k, rc, r.cause);
            return 1;
        }
        free(g.io.buf);
    }
    memset(&spec, 0, sizeof spec);
    CHECK(gm_with_font("<A>A</A>", &spec, NULL, &g, &r) == 0);
    free(g.io.buf);
    return 0;
}
