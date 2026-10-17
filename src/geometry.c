#include "platform.h"
#include "geometry.h"

#include "ki_xml_pdf.h"

#include <stdio.h>
#include <string.h>

/* 210 x 297 mm at 72/25.4 pt per mm, rounded to 0.001 pt. */
#define A4_WIDTH 595276LL
#define A4_HEIGHT 841890LL
#define LETTER_WIDTH 612000LL
#define LETTER_HEIGHT 792000LL

void geom_default(Geometry *g) {
    g->width = LETTER_WIDTH;
    g->height = LETTER_HEIGHT;
    g->top = g->right = g->bottom = g->left = 40000;
    g->font = 10000;
    g->leading = 14000;
}

/* NaN fails every comparison and infinities fail the range, so only finite
   values in [lo, hi] points pass. */
static int to_mpt(double v, long long lo, long long hi, long long *out) {
    if (!(v >= (double)lo / 1000.0 && v <= (double)hi / 1000.0)) {
        return -1;
    }
    *out = (long long)(v * 1000.0 + 0.5);
    return 0;
}

int geom_from_points(Geometry *g, int page, int orient, double width, double height, const double margin[4],
                     double font, const char **why) {
    if (page != KI_PAGE_LETTER && page != KI_PAGE_A4 && page != KI_PAGE_CUSTOM) {
        *why = "page geometry: unknown page size";
        return -1;
    }
    if (orient != KI_ORIENT_PORTRAIT && orient != KI_ORIENT_LANDSCAPE) {
        *why = "page geometry: unknown orientation";
        return -1;
    }
    if (page == KI_PAGE_CUSTOM) {
        if (orient != KI_ORIENT_PORTRAIT) {
            *why = "page geometry: orientation applies only to a named page size";
            return -1;
        }
        if (to_mpt(width, GEOM_PAGE_MIN, GEOM_PAGE_MAX, &g->width) != 0 ||
            to_mpt(height, GEOM_PAGE_MIN, GEOM_PAGE_MAX, &g->height) != 0) {
            *why = "page geometry: page width and height must be 3..14400 pt";
            return -1;
        }
    } else {
        if (width != 0.0 || height != 0.0) {
            *why = "page geometry: page width/height conflict with a named page size";
            return -1;
        }
        long long w = page == KI_PAGE_A4 ? A4_WIDTH : LETTER_WIDTH;
        long long h = page == KI_PAGE_A4 ? A4_HEIGHT : LETTER_HEIGHT;
        g->width = orient == KI_ORIENT_LANDSCAPE ? h : w;
        g->height = orient == KI_ORIENT_LANDSCAPE ? w : h;
    }
    long long m[4];
    for (int i = 0; i < 4; i++) {
        if (to_mpt(margin[i], 0, GEOM_PAGE_MAX, &m[i]) != 0) {
            *why = "page geometry: margins must be 0..14400 pt";
            return -1;
        }
    }
    g->top = m[0];
    g->right = m[1];
    g->bottom = m[2];
    g->left = m[3];
    if (to_mpt(font, GEOM_FONT_MIN, GEOM_FONT_MAX, &g->font) != 0) {
        *why = "page geometry: font size must be 1..144 pt";
        return -1;
    }
    if (g->left + g->right >= g->width) {
        *why = "page geometry: left and right margins leave no width";
        return -1;
    }
    if (g->top + g->bottom >= g->height) {
        *why = "page geometry: top and bottom margins leave no height";
        return -1;
    }
    g->leading = g->font * 7 / 5;
    return 0;
}

static long long ceil_div(long long a, long long b) {
    return (a + b - 1) / b;
}

void run_reset(InkRun *r) {
    memset(r, 0, sizeof *r);
}

void run_add(InkRun *r, const FontFace *face, unsigned cp, unsigned adv) {
    if (cp != ' ' && cp != '\t') {
        r->box = r->pen + (long)adv;
        r->has_box = 1;
        int lo = 0;
        int hi = 0;
        if (font_ink(face, cp, &lo, &hi)) {
            long long a = r->spen + (long long)lo * 1000;
            long long b = r->spen + (long long)hi * 1000;
            if (!r->has_ink || a < r->lo) {
                r->lo = a;
            }
            if (!r->has_ink || b > r->hi) {
                r->hi = b;
            }
            r->has_ink = 1;
        }
    }
    r->pen += (long)adv;
    r->spen += (long long)font_pdf_width(face, adv) * face->units;
}

void run_join(InkRun *a, const InkRun *b) {
    if (b->has_box) {
        a->box = a->pen + b->box;
        a->has_box = 1;
    }
    if (b->has_ink) {
        long long lo = a->spen + b->lo;
        long long hi = a->spen + b->hi;
        if (!a->has_ink || lo < a->lo) {
            a->lo = lo;
        }
        if (!a->has_ink || hi > a->hi) {
            a->hi = hi;
        }
        a->has_ink = 1;
    }
    a->pen += b->pen;
    a->spen += b->spen;
}

long long geom_shift(const PageFrame *f, const InkRun *r) {
    if (!r->has_ink || r->lo >= 0) {
        return 0;
    }
    return ceil_div(-r->lo * f->g.font, 1000LL * f->units);
}

static long long usable_scaled(const PageFrame *f) {
    return (f->g.width - f->g.left - f->g.right) * 1000LL * f->units;
}

int geom_fits(const PageFrame *f, const InkRun *r) {
    long long right = 0;
    if (r->has_box && (long long)r->box * 1000 > right) {
        right = (long long)r->box * 1000;
    }
    if (r->has_ink && r->hi > right) {
        right = r->hi;
    }
    return geom_shift(f, r) * 1000LL * f->units + right * f->g.font <= usable_scaled(f);
}

int geom_past_right(const PageFrame *f, long long shift, long units) {
    return shift * 1000LL * f->units + (long long)units * 1000 * f->g.font > usable_scaled(f);
}

static int glyph_fits(const PageFrame *f, const FontFace *face, unsigned cp) {
    uint16_t gid = 0;
    uint16_t adv = 0;
    if (cp == ' ' || font_lookup(face, cp, &gid, &adv) != 0) {
        return 1;
    }
    InkRun r;
    run_reset(&r);
    run_add(&r, face, cp, adv);
    return geom_fits(f, &r);
}

int geom_frame(PageFrame *f, const Geometry *g, const FontFace *face) {
    memset(f, 0, sizeof *f);
    f->g = *g;
    f->units = face->units;
    int ascent = 0;
    int descent = 0;
    font_ink_vertical(face, &ascent, &descent);
    long long up = ceil_div((long long)ascent * g->font, face->units);
    long long down = ceil_div((long long)descent * g->font, face->units);
    f->baseline = g->height - g->top - up;
    long long floor_at = g->bottom + down;
    if (f->baseline < floor_at || g->leading <= 0) {
        return -1;
    }
    long long lines = (f->baseline - floor_at) / g->leading + 1;
    f->max_lines = lines > GEOM_MAX_LINES ? GEOM_MAX_LINES : (int)lines;
    for (unsigned cp = 0; cp < FONT_BMP_MAX; cp++) {
        if (!glyph_fits(f, face, cp)) {
            return -1;
        }
    }
    for (unsigned cp = FONT_PUNCT_BASE; cp < FONT_PUNCT_BASE + FONT_PUNCT_COUNT; cp++) {
        if (!glyph_fits(f, face, cp)) {
            return -1;
        }
    }
    return 0;
}

int geom_fmt(char *out, size_t cap, long long mpt) {
    const char *sign = mpt < 0 ? "-" : "";
    unsigned long long a = mpt < 0 ? (unsigned long long)(-mpt) : (unsigned long long)mpt;
    unsigned long long whole = a / 1000;
    unsigned frac = (unsigned)(a % 1000);
    int n;
    if (frac == 0) {
        n = snprintf(out, cap, "%s%llu", sign, whole);
    } else if (frac % 100 == 0) {
        n = snprintf(out, cap, "%s%llu.%u", sign, whole, frac / 100);
    } else if (frac % 10 == 0) {
        n = snprintf(out, cap, "%s%llu.%02u", sign, whole, frac / 10);
    } else {
        n = snprintf(out, cap, "%s%llu.%03u", sign, whole, frac);
    }
    return n < 0 || (size_t)n >= cap ? -1 : n;
}
