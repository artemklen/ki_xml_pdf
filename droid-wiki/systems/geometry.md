# Geometry
Active contributors: Artem

`src/geometry.c` is the shared source of page dimensions, margin fitting and text placement. It converts public point values to integer thousandths of a point, then fits them to the loaded font so layout and PDF output cannot disagree about page edges.

## Abstractions

| Type in `src/geometry.h` | Responsibility |
| --- | --- |
| `Geometry` | Rounded width/height, four margins, type size and leading in millipoints |
| `PageFrame` | Geometry plus units-per-em, first baseline and maximum line count |
| `InkRun` | Advance sum/last non-space advance box, actual PDF-width pen and outline bounds |

There is one implementation unit and one focused C test file (`tests/test_geometry.c`, 1:1), plus the independent MediaBox/raster oracle in `tests/geometry_check.py`. Geometry is call-owned fixed state, not a page-dependent allocation.

## Two validation phases

`reject_config` in `src/convert.c` calls `geom_from_points` before opening files or invoking conversion callbacks. This checks enums, finite numeric ranges, named/custom-size conflicts and usable width/height.

| Setting | Accepted values |
| --- | --- |
| Named size | Letter 612 × 792 pt; A4 595.276 × 841.89 pt |
| Orientation | Portrait or landscape for named sizes; landscape swaps sides |
| Custom size | 3–14400 pt per side, portrait enum only; dimensions are used as given |
| Margins | Each 0–14400 pt; horizontal/vertical sums strictly smaller than page sides |
| Type size | 1–144 pt |

Named sizes require zero explicit width/height. Values round to the nearest 0.001 pt; leading is rounded type size × 7/5, floored to a millipoint. NaN/infinity fail range comparisons.

After font loading, `geom_frame` computes the first baseline and line count from conservative font ascent/descent and repertoire outline extremes. It also verifies every present single repertoire glyph fits the horizontal space, even if the input would never use it. No line or a too-wide glyph gives status 2 / `KI_CAUSE_USAGE` before read/write/map callbacks. A file API may already have temporary files open; these are removed and the destination is preserved.

## Horizontal fitting

`run_add` and `run_join` track two width notions:

- Font advances up to the last non-space glyph determine the logical advance box.
- Outline bounds are placed using the truncated `/W` widths that PDF viewers actually advance by.

`geom_shift` moves a negative-left-bearing line origin right by the smallest whole millipoint that keeps its ink inside the left margin. `geom_fits` checks the shifted advance box and rightmost ink against usable width. Spaces/tabs advance the pen without extending the non-space box or ink; trailing whitespace can hang outside the line without drawing beyond the right margin.

`src/layout.c` uses these operations while deciding where to wrap. `pdf_page_line` in `src/pdf_write.c` remeasures each finished line with the same functions, applies its shift, and emits rounded PDF numbers via `geom_fmt`. The page's MediaBox uses that same `Geometry`.

## Vertical fitting

`font_ink_vertical` takes the largest bounds from font metrics, head bbox and repertoire outlines. `geom_frame` rounds ascent/descent outward, sets:

```text
baseline = height - top - scaled_ascent
last_allowed_baseline = bottom + scaled_descent
lines = floor((baseline - last_allowed_baseline) / leading) + 1
```

The maximum is 50 lines, regardless of page size or type size. A larger page does not allocate a larger page grid. Default geometry is Letter portrait, 40 pt margins and 10 pt type with 14 pt leading.

## Change entry points and checks

- Numeric conversion/defaults: `to_mpt`, `geom_from_points`, `geom_default`.
- Ink arithmetic and PDF-width rounding: `run_add`, `geom_shift`, `geom_fits`, `font_pdf_width`; change all affected assumptions together.
- Baselines/cap: `geom_frame`, `GEOM_MAX_LINES`; retain fixed layout and writer capacity.
- Public behavior/ABI: `KiConfig` and configured calls, not added fields in the established `KiOptions` layout.

`tests/test_geometry.c` covers size-aware initialization, invalid values, legacy equivalence, varied sizes/fonts, negative bearings and malformed outline spans. `tests/geometry_check.py` checks every page using `pdfinfo` and `pdftoppm`; `tests/integration_check.py` combines geometry with wrapping, Flate, metadata and all I/O routes.

## Key source files

| File | Role |
| --- | --- |
| `src/geometry.c`, `src/geometry.h` | Rounded geometry, frame and run arithmetic |
| `include/ki_xml_pdf.h` | Config layout, geometry enums and public contract |
| `src/convert.c` | Early configuration validation |
| `src/font.c` | Advance rounding and conservative font ink |
| `src/layout.c`, `src/pdf_write.c` | Wrap decisions, revalidation, placement and MediaBox |
| `tests/test_geometry.c`, `tests/geometry_check.py` | Internal and independent visual/box checks |

Related: [Page geometry](../features/page-geometry.md), [Layout and fonts](layout-and-fonts.md), [Configuration](../reference/configuration.md).
