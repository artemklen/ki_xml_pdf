#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include <stdint.h>

/* Fixed tables for the records-v1 repertoire. The face does not grow when a
   document uses more of that repertoire. */

#define FONT_BMP_MAX 0x500
#define FONT_PUNCT_BASE 0x2010
#define FONT_PUNCT_COUNT 24

#define FONT_NAME_MAX 63

typedef struct FontFace {
    unsigned char *ttf;
    size_t ttf_len;
    int units;
    int ascent;
    int descent;
    int bbox[4];
    uint16_t gid[FONT_BMP_MAX];
    uint16_t adv[FONT_BMP_MAX];
    uint16_t punct_gid[FONT_PUNCT_COUNT];
    uint16_t punct_adv[FONT_PUNCT_COUNT];
    /* PostScript name (nameID 6) as printable ASCII, or KiFont-XXXXXXXX
       derived from the file bytes when the font has no usable one. */
    char name[FONT_NAME_MAX + 1];
    int name_fallback;
    /* Horizontal ink of each repertoire glyph in font units from its origin,
       from the glyf header box. ink_on is 0 for an empty outline. Without
       glyf/loca there are no outlines, and the advance box stands in. */
    int outlines;
    int16_t ink_lo[FONT_BMP_MAX];
    int16_t ink_hi[FONT_BMP_MAX];
    unsigned char ink_on[FONT_BMP_MAX];
    int16_t punct_ink_lo[FONT_PUNCT_COUNT];
    int16_t punct_ink_hi[FONT_PUNCT_COUNT];
    unsigned char punct_ink_on[FONT_PUNCT_COUNT];
    /* Highest yMax and lowest yMin of the repertoire's outlines. */
    int ink_top;
    int ink_bottom;
} FontFace;

int font_load(FontFace *face, const char *path);
void font_free(FontFace *face);
/* 0 = found, -1 = not in the repertoire or missing from the font (GID 0 or
   at/past maxp numGlyphs). A present glyph may have a zero advance. */
int font_lookup(const FontFace *face, unsigned cp, uint16_t *gid, uint16_t *adv);
int font_open_default(FontFace *face);
/* PDF /W width of an advance, in 1/1000 em, truncated as written. */
int font_pdf_width(const FontFace *face, unsigned adv);
/* 1 when cp draws ink: [*lo, *hi] in font units from the glyph origin. */
int font_ink(const FontFace *face, unsigned cp, int *lo, int *hi);
/* Ascent above and descent below the baseline (both >= 0, font units) that
   bound every glyph's ink: hhea, head and repertoire outline extremes. */
void font_ink_vertical(const FontFace *face, int *ascent, int *descent);

#endif
