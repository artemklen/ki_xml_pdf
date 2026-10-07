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

static int find_table(const unsigned char *data, size_t len, const char tag[4], size_t *off, size_t *tlen) {
    if (len < 12) {
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
        if (o > len || nlen > len - o) {
            return -1;
        }
        *off = o;
        *tlen = nlen;
        return 0;
    }
    return -1;
}

static unsigned glyph_of(const unsigned char *cmap, size_t len, unsigned cp) {
    if (len < 14) {
        return 0;
    }
    unsigned seg = ru16(cmap + 6) / 2;
    if (seg > len / 8) {
        return 0;
    }
    size_t end_at = 14;
    size_t start_at = end_at + 2 * seg + 2;
    size_t delta_at = start_at + 2 * seg;
    size_t range_at = delta_at + 2 * seg;
    if (range_at + 2 * seg > len) {
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
        if (pos < range_at || pos + 2 > len) {
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
    if (num_h == 0 || hlen < 2) {
        return 0;
    }
    size_t at = (size_t)(gid < num_h ? gid : num_h - 1) * 4;
    if (at + 2 > hlen) {
        return 0;
    }
    return ru16(hmtx + at);
}

static const unsigned char *format4(const unsigned char *cmap, size_t len, size_t *out_len) {
    if (len < 4) {
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
        if (rec + 8 > len) {
            return NULL;
        }
        unsigned plat = ru16(cmap + rec);
        unsigned enc = ru16(cmap + rec + 2);
        size_t rel = ru32(cmap + rec + 4);
        if (rel + 2 > len) {
            continue;
        }
        unsigned fmt = ru16(cmap + rel);
        if (fmt != 4 || plat != 3 || enc != 1) {
            continue;
        }
        unsigned sublen = ru16(cmap + rel + 2);
        if (sublen < 14 || sublen > len || rel > len - sublen) {
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

static void remember(FontFace *face, unsigned cp, unsigned gid, unsigned adv) {
    if (gid == 0 || adv == 0) {
        return;
    }
    if (cp < FONT_BMP_MAX) {
        face->gid[cp] = (uint16_t)gid;
        face->adv[cp] = (uint16_t)adv;
        return;
    }
    if (cp >= FONT_PUNCT_BASE && cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT) {
        unsigned i = cp - FONT_PUNCT_BASE;
        face->punct_gid[i] = (uint16_t)gid;
        face->punct_adv[i] = (uint16_t)adv;
    }
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
    const unsigned char *hmtx = data + hmtx_off;

    for (unsigned cp = 0; cp < FONT_BMP_MAX; cp++) {
        unsigned gid = glyph_of(sub, sublen, cp);
        remember(face, cp, gid, advance_of(hmtx, hmtx_len, num_h, gid));
    }
    for (unsigned cp = FONT_PUNCT_BASE; cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT; cp++) {
        unsigned gid = glyph_of(sub, sublen, cp);
        remember(face, cp, gid, advance_of(hmtx, hmtx_len, num_h, gid));
    }
    if (face->gid[0x41] == 0 || face->gid[0x20] == 0) {
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
    if (g == 0 || a == 0) {
        return -1;
    }
    *gid = g;
    *adv = a;
    return 0;
}

static int ink_ascent(const FontFace *face) {
    int a = face->ascent > 0 ? face->ascent : 0;
    if (face->bbox[3] > a) {
        a = face->bbox[3];
    }
    return a > 0 ? a : face->units;
}

static int ink_descent(const FontFace *face) {
    int d = face->descent < 0 ? -face->descent : 0;
    if (face->bbox[1] < 0 && -face->bbox[1] > d) {
        d = -face->bbox[1];
    }
    return d;
}

int font_first_baseline(const FontFace *face, int page_h, int margin, int pt) {
    int ascent = ink_ascent(face) * pt / face->units;
    if (ascent < 1) {
        ascent = pt;
    }
    int baseline = page_h - margin - ascent;
    if (baseline < margin) {
        return margin;
    }
    return baseline;
}

int font_max_lines(const FontFace *face, int page_h, int margin, int pt, int leading) {
    int ascent = ink_ascent(face) * pt / face->units;
    if (ascent < 1) {
        ascent = pt;
    }
    int descent = ink_descent(face) * pt / face->units;
    int baseline = page_h - margin - ascent;
    int bottom = margin + descent;
    if (leading <= 0 || baseline < bottom) {
        return 1;
    }
    return (baseline - bottom) / leading + 1;
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
