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
} FontFace;

int font_load(FontFace *face, const char *path);
void font_free(FontFace *face);
/* 0 = found, -1 = not in the repertoire or missing from the font (GID 0 or
   at/past maxp numGlyphs). A present glyph may have a zero advance. */
int font_lookup(const FontFace *face, unsigned cp, uint16_t *gid, uint16_t *adv);
int font_open_default(FontFace *face);
/* Baseline and line count so the ink, not only the baseline, stays inside the margin. */
int font_first_baseline(const FontFace *face, int page_h, int margin, int pt);
int font_max_lines(const FontFace *face, int page_h, int margin, int pt, int leading);

#endif
