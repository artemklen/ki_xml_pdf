#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "font.h"

#include <stddef.h>

/* One validated page geometry is shared by layout line breaking and the PDF
   MediaBox and text placement. Lengths are integer thousandths of a point
   (mpt), so both sides use the same rounded values. */

#define GEOM_PAGE_MIN 3000LL      /* PDF 1.4 implementation limits, 3 pt */
#define GEOM_PAGE_MAX 14400000LL  /* and 14400 pt */
#define GEOM_FONT_MIN 1000LL
#define GEOM_FONT_MAX 144000LL
#define GEOM_MAX_LINES 50

typedef struct Geometry {
    long long width;
    long long height;
    long long top;
    long long right;
    long long bottom;
    long long left;
    long long font;
    long long leading;
} Geometry;

/* Geometry fitted to a loaded font: first baseline and lines per page. */
typedef struct PageFrame {
    Geometry g;
    int units;
    long long baseline;
    int max_lines;
} PageFrame;

/* Horizontal extent of a run of glyphs set from one origin. pen/box are in
   font units: pen is the advance sum, box the pen after the last glyph that
   is not a space (the advance box). spen and ink use S = 1/(1000 * units)
   em: spen sums the PDF /W widths that viewers actually advance by, and
   [lo, hi] bounds the outlines placed at those pens. */
typedef struct InkRun {
    long pen;
    long box;
    long long spen;
    long long lo;
    long long hi;
    int has_box;
    int has_ink;
} InkRun;

void geom_default(Geometry *g);
/* page is a KI_PAGE_* value and orient a KI_ORIENT_* value; margin is top,
   right, bottom, left. Sizes are points. On failure returns -1 with *why. */
int geom_from_points(Geometry *g, int page, int orient, double width, double height, const double margin[4],
                     double font, const char **why);
/* -1 when no line, or some single glyph of the font, fits. */
int geom_frame(PageFrame *f, const Geometry *g, const FontFace *face);

void run_reset(InkRun *r);
/* Appends one code point; a space or tab advances without box or ink. */
void run_add(InkRun *r, const FontFace *face, unsigned cp, unsigned adv);
/* Appends b, set right after a. */
void run_join(InkRun *a, const InkRun *b);
/* Distance in mpt from the left margin to the run's origin so that its ink
   does not cross the margin. */
long long geom_shift(const PageFrame *f, const InkRun *r);
/* True when the run's advance box and ink fit between the margins. */
int geom_fits(const PageFrame *f, const InkRun *r);
/* True when a pen at units (font units) from an origin shifted by shift is
   past the right margin. */
int geom_past_right(const PageFrame *f, long long shift, long units);
/* Writes mpt as a PDF number without trailing zeros, e.g. 595.276. */
int geom_fmt(char *out, size_t cap, long long mpt);

#endif
