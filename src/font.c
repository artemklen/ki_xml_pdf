#define _XOPEN_SOURCE 700

#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned ru16(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned ru32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static int s16(const unsigned char *p) {
    int v = (int)ru16(p);
    if (v & 0x8000) {
        v -= 0x10000;
    }
    return v;
}

/* True when [off, off + need) lies inside a buffer of len bytes, without
   computing off + need. */
static int span_ok(size_t len, size_t off, size_t need) {
    return off <= len && need <= len - off;
}

static int find_table(const unsigned char *data, size_t len, const char tag[4], size_t *off, size_t *tlen) {
    if (!span_ok(len, 0, 12)) {
        return -1;
    }
    unsigned n = ru16(data + 4);
    if (n > (len - 12) / 16) {
        return -1;
    }
    for (unsigned i = 0; i < n; i++) {
        const unsigned char *e = data + 12 + (size_t)i * 16;
        if (memcmp(e, tag, 4) != 0) {
            continue;
        }
        size_t o = ru32(e + 8);
        size_t nlen = ru32(e + 12);
        if (!span_ok(len, o, nlen)) {
            return -1;
        }
        *off = o;
        *tlen = nlen;
        return 0;
    }
    return -1;
}

static unsigned glyph_of(const unsigned char *cmap, size_t len, unsigned cp) {
    if (!span_ok(len, 0, 14)) {
        return 0;
    }
    unsigned seg = ru16(cmap + 6) / 2;
    if (seg > len / 8) {
        return 0;
    }
    size_t end_at = 14;
    size_t start_at = end_at + 2 * (size_t)seg + 2;
    size_t delta_at = start_at + 2 * (size_t)seg;
    size_t range_at = delta_at + 2 * (size_t)seg;
    if (!span_ok(len, range_at, 2 * (size_t)seg)) {
        return 0;
    }
    for (unsigned i = 0; i < seg; i++) {
        unsigned end = ru16(cmap + end_at + 2 * i);
        if (cp > end) {
            continue;
        }
        unsigned start = ru16(cmap + start_at + 2 * i);
        if (cp < start) {
            return 0;
        }
        int delta = s16(cmap + delta_at + 2 * i);
        unsigned roff = ru16(cmap + range_at + 2 * i);
        if (roff == 0) {
            return (unsigned)((int)cp + delta) & 0xFFFFu;
        }
        if (roff > len || (size_t)(cp - start) > (len / 2)) {
            return 0;
        }
        size_t pos = range_at + 2 * (size_t)i + roff + (size_t)(cp - start) * 2;
        if (!span_ok(len, pos, 2)) {
            return 0;
        }
        unsigned g = ru16(cmap + pos);
        if (g == 0) {
            return 0;
        }
        return (unsigned)((int)g + delta) & 0xFFFFu;
    }
    return 0;
}

static unsigned advance_of(const unsigned char *hmtx, size_t hlen, unsigned num_h, unsigned gid) {
    if (num_h == 0) {
        return 0;
    }
    size_t at = (size_t)(gid < num_h ? gid : num_h - 1) * 4;
    if (!span_ok(hlen, at, 2)) {
        return 0;
    }
    return ru16(hmtx + at);
}

static const unsigned char *format4(const unsigned char *cmap, size_t len, size_t *out_len) {
    if (!span_ok(len, 0, 4)) {
        return NULL;
    }
    unsigned num = ru16(cmap + 2);
    if (num > (len - 4) / 8) {
        return NULL;
    }
    const unsigned char *best = NULL;
    size_t best_len = 0;
    for (unsigned i = 0; i < num; i++) {
        size_t rec = 4 + (size_t)i * 8;
        if (!span_ok(len, rec, 8)) {
            return NULL;
        }
        unsigned plat = ru16(cmap + rec);
        unsigned enc = ru16(cmap + rec + 2);
        if (plat != 3 || enc != 1) {
            continue;
        }
        size_t rel = ru32(cmap + rec + 4);
        /* format and length must both be present before either is read. */
        if (!span_ok(len, rel, 4)) {
            continue;
        }
        unsigned fmt = ru16(cmap + rel);
        if (fmt != 4) {
            continue;
        }
        unsigned sublen = ru16(cmap + rel + 2);
        if (sublen < 14 || !span_ok(len, rel, sublen)) {
            return NULL;
        }
        best = cmap + rel;
        best_len = sublen;
    }
    if (best == NULL) {
        return NULL;
    }
    *out_len = best_len;
    return best;
}

typedef struct {
    int on;
    int x0, y0, x1, y1;
} GlyphBox;

typedef struct {
    const unsigned char *glyf;
    size_t glyf_len;
    const unsigned char *loca;
    size_t loca_len;
    int long_loca;
} Outlines;

/* -1 when the loca entries or the glyph header lie outside their tables or
   the header box is inverted. An empty glyph has no ink. */
static int glyph_box(const Outlines *o, unsigned gid, GlyphBox *box) {
    memset(box, 0, sizeof *box);
    size_t width = o->long_loca ? 4 : 2;
    size_t at = (size_t)gid * width;
    if (!span_ok(o->loca_len, at, 2 * width)) {
        return -1;
    }
    size_t start = o->long_loca ? ru32(o->loca + at) : (size_t)ru16(o->loca + at) * 2;
    size_t end = o->long_loca ? ru32(o->loca + at + 4) : (size_t)ru16(o->loca + at + 2) * 2;
    if (start > end || end > o->glyf_len) {
        return -1;
    }
    if (start == end) {
        return 0;
    }
    if (end - start < 10) {
        return -1;
    }
    const unsigned char *g = o->glyf + start;
    box->x0 = s16(g + 2);
    box->y0 = s16(g + 4);
    box->x1 = s16(g + 6);
    box->y1 = s16(g + 8);
    if (box->x0 > box->x1 || box->y0 > box->y1) {
        return -1;
    }
    box->on = 1;
    return 0;
}

static void remember(FontFace *face, unsigned cp, unsigned gid, unsigned adv, const GlyphBox *box) {
    if (gid == 0) {
        return;
    }
    int on = face->outlines ? box->on : 1;
    int lo = face->outlines ? box->x0 : 0;
    int hi = face->outlines ? box->x1 : (int)(adv > 0x7FFF ? 0x7FFF : adv);
    if (face->outlines && box->on) {
        if (box->y1 > face->ink_top) {
            face->ink_top = box->y1;
        }
        if (box->y0 < face->ink_bottom) {
            face->ink_bottom = box->y0;
        }
    }
    if (cp < FONT_BMP_MAX) {
        face->gid[cp] = (uint16_t)gid;
        face->adv[cp] = (uint16_t)adv;
        face->ink_on[cp] = (unsigned char)on;
        face->ink_lo[cp] = (int16_t)lo;
        face->ink_hi[cp] = (int16_t)hi;
        return;
    }
    if (cp >= FONT_PUNCT_BASE && cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT) {
        unsigned i = cp - FONT_PUNCT_BASE;
        face->punct_gid[i] = (uint16_t)gid;
        face->punct_adv[i] = (uint16_t)adv;
        face->punct_ink_on[i] = (unsigned char)on;
        face->punct_ink_lo[i] = (int16_t)lo;
        face->punct_ink_hi[i] = (int16_t)hi;
    }
}

/* Maps cp, bounds-checks its outline and records it. -1 on a malformed
   outline entry for a glyph the repertoire uses. */
static int add_glyph(FontFace *face, const unsigned char *sub, size_t sublen, unsigned num_glyphs,
                     const unsigned char *hmtx, size_t hmtx_len, unsigned num_h, const Outlines *o, unsigned cp) {
    unsigned gid = glyph_of(sub, sublen, cp);
    if (gid >= num_glyphs) {
        gid = 0;
    }
    GlyphBox box;
    memset(&box, 0, sizeof box);
    if (gid != 0 && face->outlines && glyph_box(o, gid, &box) != 0) {
        return -1;
    }
    remember(face, cp, gid, advance_of(hmtx, hmtx_len, num_h, gid), &box);
    return 0;
}

/* Decodes one PostScript-name record into out. Only printable ASCII names of
   1..FONT_NAME_MAX characters qualify; the PDF writer escapes delimiters. */
static int ps_name(const unsigned char *s, size_t n, int wide, char *out) {
    size_t chars = wide ? n / 2 : n;
    if ((wide && n % 2 != 0) || chars == 0 || chars > FONT_NAME_MAX) {
        return -1;
    }
    for (size_t i = 0; i < chars; i++) {
        unsigned c = wide ? ru16(s + 2 * i) : s[i];
        if (c < 0x20 || c > 0x7E) {
            return -1;
        }
        out[i] = (char)c;
    }
    out[chars] = '\0';
    return 0;
}

/* -1 when the table or any record lies outside its bounds. Otherwise name
   holds nameID 6, preferring Windows Unicode en-US, then Macintosh Roman,
   then any other Windows record; an unusable name leaves it empty. */
static int read_name(const unsigned char *t, size_t len, char *name) {
    name[0] = '\0';
    if (!span_ok(len, 0, 6)) {
        return -1;
    }
    unsigned count = ru16(t + 2);
    size_t store = ru16(t + 4);
    if (!span_ok(len, 6, (size_t)count * 12) || store > len) {
        return -1;
    }
    int best = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *r = t + 6 + (size_t)i * 12;
        unsigned plat = ru16(r), enc = ru16(r + 2), lang = ru16(r + 4), id = ru16(r + 6);
        size_t n = ru16(r + 8), off = ru16(r + 10);
        if (!span_ok(len - store, off, n)) {
            return -1;
        }
        if (id != 6) {
            continue;
        }
        int rank = 0;
        int wide = 0;
        if (plat == 3 && enc == 1 && lang == 0x409) {
            rank = 3;
            wide = 1;
        } else if (plat == 1 && enc == 0) {
            rank = 2;
        } else if (plat == 3 && (enc == 0 || enc == 1)) {
            rank = 1;
            wide = 1;
        }
        char cand[FONT_NAME_MAX + 1];
        if (rank > best && ps_name(t + store + off, n, wide, cand) == 0) {
            memcpy(name, cand, sizeof cand);
            best = rank;
        }
    }
    return 0;
}

/* FNV-1a over the whole file, so equal bytes always get the same fallback. */
static void fallback_name(FontFace *face, const unsigned char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ data[i]) * 16777619u;
    }
    snprintf(face->name, sizeof face->name, "KiFont-%08X", (unsigned)h);
    face->name_fallback = 1;
}

int font_load(FontFace *face, const char *path) {
    memset(face, 0, sizeof *face);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 12 || sz > 8 * 1024 * 1024) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    unsigned char *data = malloc((size_t)sz);
    if (data == NULL || fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
        free(data);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    size_t hoff = 0, hlen = 0, cmap_off = 0, cmap_len = 0, hhea_off = 0, hhea_len = 0;
    size_t hmtx_off = 0, hmtx_len = 0;
    if (find_table(data, (size_t)sz, "head", &hoff, &hlen) != 0 || hlen < 54 ||
        find_table(data, (size_t)sz, "cmap", &cmap_off, &cmap_len) != 0 ||
        find_table(data, (size_t)sz, "hhea", &hhea_off, &hhea_len) != 0 || hhea_len < 36 ||
        find_table(data, (size_t)sz, "hmtx", &hmtx_off, &hmtx_len) != 0) {
        free(data);
        return -1;
    }
    size_t sublen = 0;
    const unsigned char *sub = format4(data + cmap_off, cmap_len, &sublen);
    if (sub == NULL) {
        free(data);
        return -1;
    }
    face->units = (int)ru16(data + hoff + 18);
    if (face->units <= 0) {
        free(data);
        return -1;
    }
    face->bbox[0] = s16(data + hoff + 36);
    face->bbox[1] = s16(data + hoff + 38);
    face->bbox[2] = s16(data + hoff + 40);
    face->bbox[3] = s16(data + hoff + 42);
    face->ascent = s16(data + hhea_off + 4);
    face->descent = s16(data + hhea_off + 6);
    unsigned num_h = ru16(data + hhea_off + 34);
    if (num_h == 0 || hmtx_len / 4 < num_h) {
        free(data);
        return -1;
    }
    const unsigned char *hmtx = data + hmtx_off;
    /* maxp is optional for the loader; when present a GID at or past
       numGlyphs is a missing glyph, not a present one. */
    unsigned num_glyphs = 0x10000;
    size_t maxp_off = 0, maxp_len = 0;
    if (find_table(data, (size_t)sz, "maxp", &maxp_off, &maxp_len) == 0) {
        if (maxp_len < 6 || ru16(data + maxp_off + 4) == 0) {
            free(data);
            return -1;
        }
        num_glyphs = ru16(data + maxp_off + 4);
    }
    size_t name_off = 0, name_len = 0;
    if (find_table(data, (size_t)sz, "name", &name_off, &name_len) == 0 &&
        read_name(data + name_off, name_len, face->name) != 0) {
        free(data);
        return -1;
    }
    if (face->name[0] == '\0') {
        fallback_name(face, data, (size_t)sz);
    }

    /* glyf and loca come together or not at all; with both, every
       repertoire glyph's loca range and header must lie inside them. */
    Outlines outl;
    memset(&outl, 0, sizeof outl);
    size_t glyf_off = 0, glyf_len = 0, loca_off = 0, loca_len = 0;
    int has_glyf = find_table(data, (size_t)sz, "glyf", &glyf_off, &glyf_len) == 0;
    int has_loca = find_table(data, (size_t)sz, "loca", &loca_off, &loca_len) == 0;
    if (has_glyf != has_loca) {
        free(data);
        return -1;
    }
    if (has_glyf) {
        int fmt = s16(data + hoff + 50);
        if (fmt != 0 && fmt != 1) {
            free(data);
            return -1;
        }
        outl.glyf = data + glyf_off;
        outl.glyf_len = glyf_len;
        outl.loca = data + loca_off;
        outl.loca_len = loca_len;
        outl.long_loca = fmt == 1;
        face->outlines = 1;
    }
    int bad = 0;
    for (unsigned cp = 0; cp < FONT_BMP_MAX && !bad; cp++) {
        bad = add_glyph(face, sub, sublen, num_glyphs, hmtx, hmtx_len, num_h, &outl, cp);
    }
    for (unsigned cp = FONT_PUNCT_BASE; cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT && !bad; cp++) {
        bad = add_glyph(face, sub, sublen, num_glyphs, hmtx, hmtx_len, num_h, &outl, cp);
    }
    if (bad || face->gid[0x41] == 0 || face->gid[0x20] == 0) {
        free(data);
        return -1;
    }
    face->ttf = data;
    face->ttf_len = (size_t)sz;
    return 0;
}

void font_free(FontFace *face) {
    free(face->ttf);
    face->ttf = NULL;
    face->ttf_len = 0;
}

int font_lookup(const FontFace *face, unsigned cp, uint16_t *gid, uint16_t *adv) {
    uint16_t g = 0;
    uint16_t a = 0;
    if (cp < FONT_BMP_MAX) {
        g = face->gid[cp];
        a = face->adv[cp];
    } else if (cp >= FONT_PUNCT_BASE && cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT) {
        unsigned i = cp - FONT_PUNCT_BASE;
        g = face->punct_gid[i];
        a = face->punct_adv[i];
    }
    if (g == 0) {
        return -1;
    }
    *gid = g;
    *adv = a;
    return 0;
}

int font_pdf_width(const FontFace *face, unsigned adv) {
    return (int)((long)adv * 1000 / face->units);
}

int font_ink(const FontFace *face, unsigned cp, int *lo, int *hi) {
    if (cp < FONT_BMP_MAX) {
        if (face->gid[cp] == 0 || !face->ink_on[cp]) {
            return 0;
        }
        *lo = face->ink_lo[cp];
        *hi = face->ink_hi[cp];
        return 1;
    }
    if (cp >= FONT_PUNCT_BASE && cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT) {
        unsigned i = cp - FONT_PUNCT_BASE;
        if (face->punct_gid[i] == 0 || !face->punct_ink_on[i]) {
            return 0;
        }
        *lo = face->punct_ink_lo[i];
        *hi = face->punct_ink_hi[i];
        return 1;
    }
    return 0;
}

void font_ink_vertical(const FontFace *face, int *ascent, int *descent) {
    int a = face->ascent > 0 ? face->ascent : 0;
    if (face->bbox[3] > a) {
        a = face->bbox[3];
    }
    if (face->ink_top > a) {
        a = face->ink_top;
    }
    int d = face->descent < 0 ? -face->descent : 0;
    if (-face->bbox[1] > d) {
        d = -face->bbox[1];
    }
    if (-face->ink_bottom > d) {
        d = -face->ink_bottom;
    }
    *ascent = a > 0 ? a : face->units;
    *descent = d;
}

static int exe_rel(const char *suffix, char *out, size_t cap) {
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0 || (size_t)n >= sizeof exe) {
        return -1;
    }
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash == NULL) {
        return -1;
    }
    *slash = '\0';
    int wrote = snprintf(out, cap, "%s/%s", exe, suffix);
    if (wrote < 0 || (size_t)wrote >= cap) {
        return -1;
    }
    return 0;
}

int font_open_default(FontFace *face) {
    const char *env = getenv("KI_XML_PDF_FONT");
    if (env != NULL && env[0] != '\0') {
        return font_load(face, env);
    }
#ifdef XML2PDF_FONT_PATH
    if (font_load(face, XML2PDF_FONT_PATH) == 0) {
        return 0;
    }
#endif
    char beside[640];
    const char *rels[] = {"fonts/DejaVuSans.ttf", "../fonts/DejaVuSans.ttf",
                          "../share/ki_xml_pdf/DejaVuSans.ttf"};
    for (size_t i = 0; i < sizeof rels / sizeof rels[0]; i++) {
        if (exe_rel(rels[i], beside, sizeof beside) == 0 && font_load(face, beside) == 0) {
            return 0;
        }
    }
    return font_load(face, "fonts/DejaVuSans.ttf");
}
