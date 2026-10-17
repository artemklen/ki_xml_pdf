# Cleanup opportunities
By Artem.

Verified maintenance candidates at **2026-10-17**, HEAD `d573069`. These are bounded follow-ups, not a request to redesign the pipeline. Removed `page_sink` sources, the old whole-TTF allocation, heap-built CMap and joined-page copy are **already resolved**, not current debt.

## 1. Remove the no-op writer hook

`pdf_debug_fail_at_page` is declared in `src/pdf_write.h:97-98` and defined in `src/pdf_write.c:844-846` as `(void)n;`. A tracked-source search finds no call: the only occurrences are that declaration and definition.

The active mechanism is different: `xml2pdf_debug_fail_at_page` in `src/convert.c` feeds `PdfConfig.debug_fail_page`; `pdf_page_begin` enforces it. `tests/test_stream.c` calls the convert-level hook. Removing the no-op pair would reduce ambiguity without removing active failure injection. It is not in `libki_xml_pdf.map`'s public exports.

**Check:** rebuild Make/CMake consumers and run the normal and sanitizer suites. Do not delete the similarly named working convert-level hook. See the [writer](systems/pdf-writer.md).

## 2. Replace historical grid names with test intent

`src/convert.h:6-11` still describes “a letter page in Helvetica 10pt” and defines `XML2PDF_PAGE_COLS` = 90, `XML2PDF_PAGE_LINES` = 48 and `XML2PDF_PAGE_CAP` = 4,368. These no longer describe production glyph-metric layout, configurable geometry or the writer's content-buffer capacity.

Their only non-definition uses are two test padding calculations in `tests/test_stream.c:1075` and `tests/test_stream.c:1564`. The first creates a larger conversion sample; the second puts a malformed closing tag after a long body.

Move an explicitly named fixture-padding constant into the test, or size those inputs for their stated assertions; then remove the misleading internal grid definitions/comment. Preserve the tests' multi-page and late-error behavior rather than substituting a new production “page cap.” See [geometry](systems/geometry.md) and [layout and fonts](systems/layout-and-fonts.md).

## 3. Make the legacy wrapper's byte limit unmistakable

`xml2pdf_convert_cap` in `src/convert.c:979-990` accepts `size_t page_cap`, then sets `KiOptions.max_output_bytes` to it. The name suggests a page count or layout capacity, but the behavior is an **output-byte quota**. `src/convert.h` exposes the same ambiguous parameter name.

Clarify the internal parameter/documentation without changing behavior. Both `xml2pdf_convert` and `xml2pdf_convert_cap` remain exported in `KI_0_1` in `libki_xml_pdf.map`; deleting them is an ABI decision, not dead-code cleanup. Keep their stderr logging and failure-hook reset behavior unless deliberately replacing those contracts. See the [C API](api/c-api.md).

## 4. Decompose the conversion coordinator along ownership boundaries

`convert_streams` spans **`src/convert.c:541-875`**, 335 lines inclusive from signature through closing brace. It initializes parser/layout/writer, dispatches three mapping routes, classifies failures, finalizes output, publishes a file and fills results. The common cleanup label is useful; the size is a reviewability issue, not evidence that bounded memory is broken.

Concrete duplication is the cancellation polling at `src/convert.c:614,730,753,777,788,814`, plus three reader-side polls at `src/convert.c:127,142,156`. Several publication checkpoints repeat the same resource/cancel status, message and logging assignments.

Consider a small cancellation-report helper and a file-publication helper with explicit ownership/result contracts. **Do not collapse checkpoints:** polling before/after I/O, around `fsync`, after close and before `rename` is observable. Preserve the first-error latch, callback-accepted byte accounting, cleanup order and “no publication after cancellation” guarantee.

**Check:** `tests/test_callback_io.c`, `tests/test_stream.c`, `tests/cli_pipeline.py` and `tests/test_integration.c`, including sanitizer/concurrent runs. See the [conversion pipeline](systems/conversion-pipeline.md), [atomic output](features/atomic-output.md) and [testing](how-to-contribute/testing.md).

## 5. Label the review manifest as historical provenance

`baseline-manifest.json` says its hashes compare the “working tree” with the 2026-10-07 review. Comparing its 13 recorded `sha256` values with HEAD blobs gives:

| Current comparison | Count |
| --- | ---: |
| Same bytes (`tests/fixture.xml`) | 1 |
| Different bytes | 10 |
| Paths no longer present | 2 |

The absent paths are historical review entries, not removal tasks. `review_source_revision` is a 64-character identifier; the repository does not establish it as a reachable Git commit. Neither `Makefile`, `CMakeLists.txt` nor `.github/workflows/` regenerates/verifies the manifest, while `tests/distcheck.sh:65` explicitly requires it in the source archive.

Prefer an unambiguous historical-snapshot note and a pointer to the commit-bound release provenance. Do not “repair” old review hashes as if they were current checksums. `SOURCE_COMMIT`, `SHA256SUMS` and the archive commit check in `.github/workflows/release.yml` serve the current-release purpose. See [packaging](features/packaging.md) and [design decisions](background/design-decisions.md).

## What is not an evidenced cleanup finding

- The scoped tracked code (`.c`, `.h`, `.cpp`, `.py`, `.sh`, excluding wiki/evidence) has **zero whole-word `TODO`/`FIXME` markers**. That is a search result, not proof of no remaining work.
- The addon ADR explicitly says “accepted as design” and “not implemented,” and pins older writer assumptions. Its historical CMap description is not a current allocation regression. Updating it before implementation would be documentation maintenance, not evidence of shipped addon support.
- `tests/research_*` and revision-scoped `docs/evidence/` records are intentional research/audit material. Deferred UTF-16, tables and subsetting are not accidentally enabled dependencies or stale production packages.
- Russian historical design records are not obsolete merely because they lack English translations; the wiki already supplies English context. No dependency-staleness claim is made here.

## Key source files and guardrails

| Sources | Evidence / behavior to preserve |
| --- | --- |
| `src/convert.h`, `tests/test_stream.c` | Test-only grid constants and live failure injection |
| `src/convert.c`, `src/pdf_write.c`, `src/pdf_write.h` | Coordinator, legacy byte quota and no-op hook |
| `include/ki_xml_pdf.h`, `libki_xml_pdf.map` | Size-checked structures and versioned public ABI |
| `baseline-manifest.json`, `tests/distcheck.sh`, `.github/workflows/release.yml` | Historical review artifact versus current release provenance |
| `tests/test_callback_io.c`, `tests/test_integration.c`, `tests/cli_pipeline.py` | Failure, lifecycle and publication regression coverage |

[Overview](overview/index.md) · [Architecture](overview/architecture.md) · [Patterns and conventions](how-to-contribute/patterns-and-conventions.md) · [Testing](how-to-contribute/testing.md) · [Background](background/index.md) · [Measurements](by-the-numbers.md)
