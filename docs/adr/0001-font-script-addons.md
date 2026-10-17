# ADR 0001: Optional font-script addons

Status: accepted as design. Not implemented. This document does not change the library.

Latin and Cyrillic stay mandatory. Other scripts are optional addons supplied by the caller. They are not loaded, and they add no tables, unless a document uses a character from that addon's range.

The facts below are the tree after OSR-83 (`9595c0e`), which streams FontFile2 from the open file. Line numbers drift; the names are the contract.

## 1. Base

The base face stays the current repertoire: U+0000–U+04FF plus the 24 punctuation slots U+2010–U+2027 (`FONT_BMP_MAX`, `FONT_PUNCT_BASE`, `FONT_PUNCT_COUNT` in `src/font.h`). The bundled face is DejaVu Sans, selected by `KiOptions.font_path` or `KI_XML_PDF_FONT`, otherwise the usual search in `font_open_default`.

With no addon configured, behavior and PDF bytes stay what they are today. One Type0 font `/F1`, `/Encoding /Identity-H`, `/CIDToGIDMap /Identity`, one ToUnicode CMap, one `/W` array. A character outside the base repertoire is still `KI_CAUSE_FONT`.

## 2. How the caller attaches an addon

An addon is a font path plus one inclusive Unicode range. The caller passes an ordered list. Two shapes were compared:

- Explicit ranges. The caller says which code points a file covers. Selection does not depend on cmap search order across files.
- A fallback chain with no ranges. Each character walks the fonts until one cmap hits. This is shorter to call, and it hides overlaps: two fonts that both draw U+00E9 can switch the glyph when the list is reordered, and a missing glyph is discovered only after opening every file.

Decision: explicit ranges, tried in list order. For each character the layout looks at the base face first. If the character is outside the base repertoire, it walks addons in order and takes the first whose range contains the code point and whose font maps it to a non-zero GID inside `maxp`. If no face covers it, the conversion fails with `KI_CAUSE_FONT` and does not publish the destination. The same rule as today: a missing glyph is an error, not `.notdef` and not a skipped character.

Ranges may abut. Overlapping ranges are allowed and resolved by list order, so the caller, not the library, picks the winner.

## 3. Load and embed only what the document uses

Font objects are written at the start today (`write_font_objects` from `pdf_begin_cfg`), before the first character is known. An addon must not be opened there.

On the first character that selects an addon, before that page's objects are written:

1. Open the file with the same streaming `font_load` as the base face (OSR-83): table spans are read and freed, the TTF bytes are not kept, and the file is checked again by size and FNV-1a before FontFile2 is deflated in 16 KiB chunks. The 8 MiB cap is this check. It is not a check in `pdf_begin_cfg`.
2. Take the next ids from a running object counter, and a font resource name `/F2`, `/F3`, … in first-use order.
3. Write that face's objects then: FontFile2 plus its length, FontDescriptor, CIDFontType2, ToUnicode, and Type0. `/W` and ToUnicode list the addon's repertoire, not every code point of Unicode. ToUnicode stays a direct `/Length` and is not Flate. On this tree the base CMap is still assembled in a 256 KiB buffer and an 8 KiB bitmap, then freed (`write_tounicode`). An addon must not keep that buffer. Count-then-write with nothing retained is the OSR-84 approach; this ADR does not assume that commit has landed.
4. The page content switches font when the face changes. GIDs from two faces do not share one show string. The page resource dictionary names only the faces that page used. Earlier pages do not gain a new font.

Today's numbering cannot do step 2. `pdf_add_page` sets the page's three ids from `OBJ_FIRST + 3 * page_index`, and `pdf_end` rebuilds `/Kids` with the same formula. The xref is one dense subsection `0 .. nobj+1`; a slot whose offset is `<= 0` fails. A six-object font block inserted after a flushed page collides with the next page's content id, and a hole left for a later page fails the xref. The offset spool already lets the catalog and the page tree be written at the end. It does not let a font block be inserted between pages under that formula.

The implementation therefore stores each page id instead of recomputing it from the index, takes every new id from one counter, and writes every id from 1 through `nobj` before `pdf_end`. Unused addons are not opened and add no object.

## 4. Memory

Base path, no addon in the call: zero extra bytes in the library. `FontFace` stays the fixed repertoire arrays it has now. `KiConfig` does not grow for a caller that passes the current size (see §7). No addon table is allocated in `pdf_begin_cfg`.

A configured addon that the document does not use: zero bytes in the library. The caller's array of descriptors stays in the caller.

A used addon whose range holds N code points:

- metrics the library keeps until `pdf_free`: `N` glyph ids and `N` advances, 4N bytes, plus outline ink of 5N bytes when the font has `glyf`/`loca`. This is the range, not a BMP of 65536 slots and not the whole CJK block unless the caller asked for that range.
- the open file: one `FILE`, the path string, the length, and the 32-bit hash. Not the TTF.
- the base CMap on this tree is a 256 KiB buffer freed before the next page. An addon implementation must not keep an equivalent buffer; that is a requirement of the future task, not a claim that OSR-84 is already on this branch.
- example: U+3040–U+30FF is 192 code points, 768 bytes of gid/adv, or 1728 bytes with ink. U+4E00–U+9FFF is 20992 code points, about 84 KiB of gid/adv, or about 189 KiB with ink. A 20 MiB CJK file is not part of that sum; it is read from disk while FontFile2 is written, and it is rejected if it is over the 8 MiB cap.

## 5. Relation to OSR-83

Addon FontFile2 uses the OSR-83 path: the face keeps the open file, checks size and checksum immediately before the embed, and deflates the file in 16 KiB chunks. `/Length1` is the file size. A change to the file between load and embed is `KI_CAUSE_FONT`. The base face does not start holding `face->ttf` again.

## 6. Subsetting

OSR-10 measured a GID-preserving subset of DejaVu Sans after Flate and decided no-go for the large-report goal: the subset removed under 3% of the 16 MiB report PDF, and an internal subsetter would be new C plus a visual oracle (`docs/evidence/osr-10-font-spike/README.md`). That decision stands for the base face.

A CJK file of tens of megabytes does not fit the existing 8 MiB font cap. The check runs at first use, before that page's objects are written, and fails with `KI_CAUSE_FONT`. Earlier pages may already have been handed to a `write_bytes` callback; OSR-10 recorded that late font failure as a reason the base font stays at the start. A file destination is still not published: `rename` runs only after success. This design does not add a subsetter to get past the cap, and it does not move the base font off the start of the file. Subsetting, if it is ever wanted, is a separate task that must keep the subsetter off the base path, preserve CID = GID or redesign `/W` and ToUnicode together, and check renders rather than `pdftotext` alone (OSR-10's `.notdef` control still extracts the same text). It is not part of implementing this ADR.

## 7. API and ABI

No change in this task. A later implementation extends `KiConfig` at the end, the same way `/Info` followed geometry:

- `KI_CONFIG_SIZE_GEOMETRY` and the current `sizeof(KiConfig)` keep their meaning. `ki_config_init` still accepts those sizes and leaves any new tail zeroed only when the caller passes the new size.
- New tail, present only when `cfg->size` is the new size: `const KiFontAddon *addons` and `size_t addon_count`. `KiFontAddon` is `{ const char *path; uint32_t first; uint32_t last; }`.
- Old binaries keep calling `ki_convert_file_cfg` with a smaller `KiConfig`. The library reads `cfg->size` and does not touch bytes past it. `KiOptions`, `KiResult`, `KiEvent`, and the `KI_0_2` / `KI_0_3` symbol versions stay. A new size constant can live in `KI_0_3` if it is only a macro, or in a new `KI_0_4` node if a new function is added. That choice is part of the implementation task, and it is not made here.
- `include/ki_xml_pdf.h` is not edited by this ADR.

`addon_count == 0` or a NULL list is the base path, byte-identical to today.

## 8. Layout and limits

Layout must not add an addon's raw advance to the base face's advance. `PageFrame.units` is fixed from the base face in `geom_frame`, and `run_add` / `geom_fits` scale by that one value. Before a mixed run is measured, each advance is converted to one unit: PDF thousandths, or the base face's `unitsPerEm`. Vertical frame fitting stays on the base face unless the implementation also folds addon ink into `font_ink_vertical`.

The page stream is one `/F1 Tf` and one hex show string today. Naming `/F2` in the resource dictionary does not draw the addon glyph. When the face changes, the content stream selects the new font and starts a new show string. GIDs from two faces never share one string. Wrap, the 1023-byte line, and the 50-line page do not change. There is no shaping, no bidi, and no composition: an Arabic addon draws the font's cmap glyphs one by one, the same way the base face draws Cyrillic.

Limits that stay:

- 8 MiB per font file, base and addon, inclusive. One byte over is `KI_CAUSE_FONT`.
- 8,000,000 PDF objects. Each used addon adds six objects (file, length, descriptor, CID font, ToUnicode, Type0) plus the widths stored inside the CID font. Unused addons add none.
- The output-byte quota and the xref ceiling count the extra font streams.
- The base repertoire tables do not grow to cover the addon's range.

## Alternatives rejected

- One font file that already contains every script. It puts CJK into the base process and hits the 8 MiB cap.
- A fallback chain without ranges. Shorter call, unstable overlaps.
- Embedding every configured addon at `pdf_begin_cfg`. Matches today's write point, and it spends file handles, metric tables, and PDF bytes on fonts the document never uses.
- A full Unicode `gid[]` on `FontFace`. Tens or hundreds of kilobytes on every conversion, including Latin-only ones.
- Subsetting inside `src/` as part of this work. Rejected in §6.

## Decision

Caller-supplied addons with explicit ranges, base face first, first match wins, `KI_CAUSE_FONT` otherwise. Open and embed an addon only on first use, through the OSR-83 stream. ToUnicode stays a direct `/Length` and is not kept after the face is written; this tree still buffers the base CMap, and dropping that buffer is OSR-84, not a premise of this ADR. Page ids are stored, not recomputed from the page index, so the new font objects can take the next ids from one counter. Mixed-face widths share one unit, and the content stream switches font. Zero library bytes on the base path. No subsetter, no shaping, no public-header change in this task.

## Future tasks

These are not started here and do not add code in this change:

1. Add `KiFontAddon` and the size-checked `KiConfig` tail. Old `cfg->size` values stay valid.
2. Select a face per code point in layout, with `KI_CAUSE_FONT` when none covers it.
3. Store page ids, take object numbers from one counter, and stream-embed the chosen addon on first use. Switch font in the content stream. Do not insert objects into the current `OBJ_FIRST + 3 * page_index` sequence.
4. Tests use a synthetic second TTF from `tests/font_fixtures.py` covering a private range. No real CJK or Arabic document.
5. Revisit subsetting only as its own task if the 8 MiB cap is raised. Keep it off the Latin/Cyrillic path.
