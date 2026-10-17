# Page geometry
Active contributors: Artem

Version 0.3.0 adds page sizes, orientation, margins and type size without extending the existing `KiOptions`, `KiResult` or `KiEvent` layouts. A separate size-checked `KiConfig` feeds one rounded geometry model shared by line wrapping, glyph placement and the PDF MediaBox.

## Configure a page

Defaults remain Letter portrait, 40 pt margins, 10 pt type and 14 pt leading. All lengths are PostScript points, 1/72 inch (`include/ki_xml_pdf.h`).

```sh
./xml2pdf --page-size a4 --orientation landscape input.xml a4.pdf
./xml2pdf --page-width 360 --page-height 504 --margin 24 --font-size 12 input.xml small.pdf
./xml2pdf --margin 36,40,48,40 input.xml margins.pdf
```

| Setting | Rule |
| --- | --- |
| Letter | 612 × 792 pt |
| A4 | 595.276 × 841.89 pt |
| Landscape | Swap named size's width and height |
| Custom width/height | Both 3–14400 pt; use the supplied dimensions directly |
| Margins | 0–14400 pt each; must leave positive usable width and height |
| Type size | 1–144 pt |
| Leading | Rounded type size × 1.4, floored to 0.001 pt |
| Lines per page | Font-dependent, at most 50 |

Values round to the nearest 0.001 pt before layout and serialization. Custom dimensions exclude CLI `--page-size` and `--orientation`, even explicit portrait. Each geometry option may appear once. CLI points are unsigned plain decimals without units or exponents; API doubles must be finite and in range.

## Public API and ABI

```c
KiOptions opt;
KiConfig cfg;
KiResult result;
ki_options_init(&opt);
if (ki_config_init(&cfg, sizeof cfg) != 0)
    return 2;
cfg.page_size = KI_PAGE_A4;
cfg.orientation = KI_ORIENT_LANDSCAPE;
cfg.margin_top = cfg.margin_right = cfg.margin_bottom = cfg.margin_left = 24.0;
cfg.font_size = 12.0;
return ki_convert_file_cfg("input.xml", "output.pdf", &opt, &cfg, &result);
```

The analogous callback calls are `ki_convert_cfg` and `ki_convert_to_file_cfg`. NULL config uses defaults with no Info, as do the original conversion calls.

`ki_config_init` accepts exactly `sizeof(KiConfig)` or `KI_CONFIG_SIZE_GEOMETRY`; it writes no more than that size and otherwise writes nothing/returns 2. The smaller form ends at `offsetof(KiConfig, title)` and has no metadata. Conversions reject all other sizes. For `KI_PAGE_CUSTOM`, set both dimensions and keep the portrait enum; for named sizes, both dimensions must be zero.

The new functions are additive `KI_0_3` exports in `libki_xml_pdf.map`. Old 0.2 structs retain their layouts and old binaries have ABI-canary coverage, although installed CMake version matching still requires the same pre-1.0 minor version. See [Packaging](packaging.md) and [C API](../api/c-api.md).

## Ink, wrapping and rejection

`src/geometry.c` checks advances **and glyph outlines**. A negative-left-bearing first glyph shifts its line origin right; ascent/descent and outline extremes reserve vertical space. The writer rechecks finished lines and emits the same rounded numbers for placement and MediaBox.

Invalid numeric/config values fail with status 2 / `KI_CAUSE_USAGE` before opening files or reading input. A statically valid page too small for a line or any present repertoire glyph fails after the font loads but before read/write/map callbacks. File destinations still remain unchanged; temporary files are removed. The check is conservative across the loaded repertoire, not only the text in this document.

A multi-glyph base/mark cluster can still exceed a line during conversion; that is a resource failure, distinct from invalid geometry. See [Layout and fonts](../systems/layout-and-fonts.md).

## Implementation and checks

Geometry is one C implementation unit with one focused C test file (`tests/test_geometry.c`, 1:1), plus independent `pdfinfo`/`pdftoppm` checks in `tests/geometry_check.py`.

- Change validation/defaults or frame math in `src/geometry.c`.
- Change CLI syntax in `src/main.c`, keeping the API's numeric contract separate from stricter CLI spelling.
- Do not duplicate page constants in layout/writer or grow the fixed page storage with page size.
- Run combined geometry/wrapping/font/Info checks in `tests/integration_check.py` for cross-feature changes.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Config sizes, enums, configured calls and contract |
| `src/geometry.c`, `src/geometry.h` | Millipoint rounding, ink fitting and page frame |
| `src/convert.c` | Config validation and legacy delegation |
| `src/main.c` | Geometry options and conflicts |
| `tests/test_geometry.c`, `tests/geometry_check.py` | Bounds, ABI-size, legacy, MediaBox and ink checks |
| `tests/abi_canary/run_canary.sh` | Old-binary compatibility checks |

Related: [Geometry internals](../systems/geometry.md), [Configuration](../reference/configuration.md), [Document information](document-information.md).
