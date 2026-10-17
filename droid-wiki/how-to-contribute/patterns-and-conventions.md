# Patterns and conventions

The implementation uses small C99 modules, bounded document state and explicit error/ownership contracts. `CONTRIBUTING.md` defines review expectations; the patterns below explain how the current code implements them.

## Bounded state

Parser limits are constants in `src/xml_parse.h`; line/page bounds are in `src/layout.h`; font metrics cover fixed repertoire arrays in `src/font.h`. Input length must not grow parser, mapper or page state.

Bounded does not mean allocation-free. The font loader reads temporary table spans, the writer keeps one reusable page-content allocation and zlib state, and callback streams/path handling allocate their own state (`src/font.c`, `src/pdf_write.c`, `src/convert.c`). Whole TTF bytes and a heap-backed ToUnicode CMap are no longer retained. Metadata that scales with PDF object count goes to a quota-controlled disk spool.

## Error mapping and cleanup

Internal functions generally return 0/-1 and record detail on their owning state: `XmlParser.errmsg`, `PdfWriter.fail_kind`, `Layout.bad_cp`/`line_full`/`too_wide`, and the private `KiDoc` error fields. `fail_from_pdf` and the driver translate those into public status/cause pairs (`src/xml_parse.h`, `src/pdf_write.h`, `src/layout.h`, `src/convert.c`).

Use the existing `done:`/`open_fail:` cleanup style when a function owns several resources. Preserve the first failure. In particular, callback write failure is latched and must not be hidden by a later successful flush (`src/convert.c`). File publication happens only after successful completion and final cancellation checks.

## Validation before side effects

Every public conversion entry resets a non-null `KiResult` before validation. Initialize `KiOptions` with `ki_options_init`; its `size` must match exactly. Initialize `KiConfig` with `ki_config_init`; only the current size and `KI_CONFIG_SIZE_GEOMETRY` are accepted (`include/ki_xml_pdf.h`, `src/convert.c`).

Geometry and metadata syntax are checked before opening files or invoking callbacks. Font-dependent fitting happens after loading the font but before reading or mapping input (`src/geometry.c`, `src/info.c`, `src/pdf_write.c`). Do not move validation behind output side effects.

## Ownership and concurrency

Mapper event pointers are borrowed for one callback. Callback resources stay with the caller. Separate conversions need separate mutable options, results, I/O and mapper state (`include/ki_xml_pdf.h`). The normal library path does not install signal handlers or write diagnostics without a log callback; signal handling belongs to `src/main.c`.

Internal debug/failure-injection hooks are test infrastructure, not a concurrent public API. Do not generalize their global state into normal conversion behavior (`src/convert.c`, `src/pdf_write.c`, `tests/test_stream.c`).

## ABI and dependencies

The shared-library export map has `KI_0_1`, `KI_0_2` and additive `KI_0_3` nodes. Geometry and metadata use `KiConfig` rather than changing the older `KiOptions`, `KiResult` or `KiEvent` layouts (`libki_xml_pdf.map`, `include/ki_xml_pdf.h`). The soname remains `libki_xml_pdf.so.0`; before 1.0 the CMake package accepts only its own minor version (`cmake/ki_xml_pdfConfigVersion.cmake.in`).

zlib is a required dependency. Keep CLI, shared/static targets, examples, pkg-config private libraries and CMake dependency discovery consistent (`Makefile`, `CMakeLists.txt`, `cmake/ki_xml_pdf.pc.in`, `cmake/ki_xml_pdfConfig.cmake.in`).

## Style and evidence

Follow four-space indentation, same-line braces, `snake_case` functions and `Ki`/`ki_` public names. Headers document lifetime and limits; comments explain constraints rather than restating operations. The supported platform is explicitly Linux/glibc, not portable ISO C (`src/platform.h`, `README.md`).

A behavior change needs a regression that fails before the fix and checks an observable result. PDF bytes alone do not establish correct text or rendering; use the independent reader/oracle checks described in [Testing](testing.md). Historical performance evidence must name its revision and must not be presented as a fresh measurement.

See [Conversion pipeline](../systems/conversion-pipeline.md) and [C API](../api/c-api.md).
