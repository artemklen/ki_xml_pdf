# OSR-16 UTF-16 transcoding spike (016f32c)

**Decision: defer. No internal UTF-16 decoder and no new encoding support were shipped. OSR-16 stays open because no real consumer, source system or target report exists.** The user authorized synthetic data only. Synthetic success shows that the approach is technically feasible. It does not show that anyone needs it. On generated fixtures, an external incremental UTF-16 → UTF-8 adapter in front of the unchanged UTF-8 API is correct, streams with bounded memory and keeps exact source offsets. An internal decoder is also feasible and cheaper than expected for offsets, but it changes the public "UTF-8 input" contract. That change needs a concrete consumer to justify it.

`tests/research_utf16_adapter.c` and `tests/research_utf16.py` are research tools. The library, CLI, installed examples, Make/CMake targets and source archive do not use them.

## Timebox

The spike started at 2026-10-16 13:20:46 MSK. `predictions.txt` was written at 13:22, before any harness code. The final measured run finished at 13:28:31. The `iconv` probes and the reproducibility check ran by 13:30, and this record was finished at about 13:32. The total is about 12 minutes, well under the four-hour limit.

## Inputs and commands

- Source: `016f32c` (OSR-17 head of the shared `osr-8` branch), `make -j2 xml2pdf`. Hashes of `xml2pdf`, `libki_xml_pdf.a`, the font and both research files are in `SHA256SUMS`.
- Font: `fonts/DejaVuSans.ttf`, the default repertoire (U+0009/000A/000D, U+0020–04FF, U+2010–2027).
- Adapter: `gcc -std=c99 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/research_utf16_adapter.c libki_xml_pdf.a -lz`. The harness builds it, plus a control build with `-DLENIENT -DRING=16`.
- Run: `python3 tests/research_utf16.py --scratch ../osr16-scratch/run2 --large-mib 16 --json docs/evidence/osr-16-utf16-spike/results.json`. Result: 168/168 checks passed, exit 0, 9.9 s wall time, 39,208 KiB peak RSS for the Python harness itself. A first run without `--large-mib` (`run1`) produced identical fixture, adapter and PDF hashes for all 167 shared checks.
- Fixtures: generated in the harness from fixed strings, with no private data. Each holds Cyrillic, `«»`, `…`, `–`/`—`, and a comment with U+1F600, U+1D11E (surrogate pairs) and U+4E2D. Comments are parsed and validated but not rendered, so the pairs pass through the library without needing glyphs. The six valid variants are LE+BOM, BE+BOM, LE+BOM with `utf-16` in lower case, BE+BOM with no declaration, LE without a BOM declared `UTF-16LE`, and BE without a BOM declared `UTF-16BE`. Their hashes are in `results.json` (`fixtures`) and `SHA256SUMS`.
- Scratch on the project disk, `/home/artem/projects/osr16-scratch`, outside the repository. The large XML/PDF files were deleted after hashing.

## What the current library does (characterization, no change)

| Input | `xml2pdf` result |
|---|---|
| UTF-16LE with BOM | exit 1, `xml: unsupported encoding at line 1, column 1, byte 0` |
| UTF-16BE with BOM | exit 1, same message |
| UTF-16LE without BOM | exit 1, `xml: bad start tag at line 1, column 2, byte 1` |
| UTF-16BE without BOM | exit 1, `xml: illegal character at line 1, column 2, byte 1` |
| a 2-byte file `FF FE` | exit 1, `xml: illegal character at line 1, column 2, byte 1` |

The library refuses every UTF-16 form, as its contract says. A BOM is reported as an unsupported encoding. Without a BOM, or with fewer than 3 bytes of input, the rejection reads as an ordinary XML error, because `consume_bom` looks for the BOM only when 3 bytes are available. Prediction 1 said "byte 0" for BE without a BOM; the library actually reports byte 1, after consuming the NUL.

## External adapter prototype (`read_bytes` callback)

The design is one incremental state machine, owned by the caller, that is passed as `KiOptions.read_bytes`:

1. **Detection policy** (XML 1.0 §4.3.3 and Appendix F).
   - `FF FE` means LE and `FE FF` means BE. The declaration may then be missing or say `UTF-16` (in any case).
   - Without a BOM, `3C 00 3F 00` means LE and `00 3C 00 3F` means BE. The declaration must then name exactly `UTF-16LE` or `UTF-16BE`.
   - Everything else is refused, including UTF-8 input and a BOM combined with `UTF-16LE`/`BE` or `UTF-8`.
2. **Declaration rewrite.** `encoding="UTF-16"` becomes `encoding="UTF-8" `, with spaces after the closing quote to make up the difference. This keeps the scalar count, so the library's **line and column equal the source line and column**. The library rejects an unmodified `UTF-16` declaration (`iconv` alone fails with `bad xml declaration`, see below). The declaration is staged as at most 255 ASCII scalars; non-ASCII content or a longer declaration is refused.
3. **Decoding.** One code unit is read at a time from a source buffer. One high surrogate waits for its pair, and at most 3 UTF-8 bytes of one scalar wait in the output. The adapter checks only that the UTF-16 is well formed. Whether a scalar is a valid XML Char stays the library's job.
4. **Offsets.** Library positions are UTF-8 byte offsets of the transcoded stream. The adapter keeps a ring of (UTF-8 length, source length) for the last 4,608 emitted scalars. A reported offset is never more than the 4,096-byte parser window plus one pending scalar behind what was handed out, so the ring maps it back to an exact source byte. If the offset falls outside the ring, it returns −1 instead of a wrong value.

### Results (`results.json`)

- **Transcoded bytes:** 72 runs, covering 6 fixtures × source chunk sizes {1, 2, 3, 4096} × output caps {1, 3, unlimited}. In every run the bytes equal Python's codec output of the independently rewritten text, including surrogate pairs split across 1-, 2- and 3-byte source reads. The most bytes left waiting for the next read was 3.
- **PDF output:** 72 runs. Every run produced the same PDF (SHA-256 `81dd77c7…`) as the identical text given as UTF-8 through the same API (`--mode utf8`). That PDF is also identical to `xml2pdf` on a plain `encoding="UTF-8"` file. `pdftotext` shows every line in order. The number of reads ranged from 264 (cap 1) to 2 (no cap).
- **XML errors, with the source byte offset mapped back:**

| Case | Library message | Mapped source byte | Python oracle |
|---|---|---:|---:|
| U+0001 in text | `illegal character at line 7, column 6, byte 259` | 408 | 408 |
| U+FFFE in text | `illegal character at line 7, column 7, byte 263` | 410 | 410 |
| `&bad;` after Cyrillic | `unknown entity at line 7, column 14, byte 274` | 424 | 424 |
| U+0001 after 1.97 MB of UTF-8 (60,007 lines) | `illegal character at line 60007, column 6, byte 1969149` | 2,858,188 | 2,858,188 |

  In every case, line, column and byte equal the UTF-8 run exactly. For the deep error, the library had read 1,028 UTF-8 bytes past the error position. **Negative control:** the 16-entry ring build returns −1 for the deep error rather than a wrong offset.
- **Decoding is separate from glyphs:** U+1F600 in text transcodes correctly and then fails as `KI_CAUSE_FONT` `font: U+1F600`. A decoder adds no CJK, emoji or shaping support.
- **Encoding errors:** 12 cases, each with the exact expected source offset.

| Case | Adapter error | Source offset |
|---|---|---:|
| lone high surrogate | `unpaired high surrogate` | 134 |
| lone low surrogate | `unpaired low surrogate` | 134 |
| high surrogate at EOF | `high surrogate at end of input` | 418 |
| odd trailing byte | `truncated code unit` | 418 |
| BOM with `UTF-8` declared | declared encoding does not match | 62 |
| BOM with `UTF-16LE` declared | declared encoding does not match | 62 |
| no BOM, `UTF-16` declared | declared encoding does not match | 60 |
| no BOM, LE bytes, `UTF-16BE` declared | declared encoding does not match | 60 |
| no BOM and no declaration | `not UTF-16: no BOM and no <?xml in UTF-16` | 0 |
| UTF-8 input | `not UTF-16: …` | 0 |
| `ё` inside the declaration | `non-ASCII scalar in XML declaration` | 78 |
| lone high surrogate after 577,914 bytes | `unpaired high surrogate` | 577,914 |

  In every case the library reports `KI_CAUSE_IO` with the message `read error`. The precise reason and offset exist only in the adapter's own state, so a consumer must read them from there. With `ki_convert`, the bytes the callback had already accepted stay in place: 412,096 bytes for an error at the start, because the font is written before any input is read, and 823,123 bytes for the mid-stream case. `ki_convert_to_file` kept the previous destination byte-for-byte and left no temporary file in every case. **Negative control:** the lenient-surrogate build passes the lone high surrogate through. The library then rejects it as an XML `illegal character` (cause 1), so the harness's check fails as it should. The library is a second line of defense, but it reports the wrong cause and position.
- **Cancellation:** `cancelled()` returning 1 from the 3rd read gives `KI_CAUSE_CANCEL`, status 4. The callback route had accepted 423,860 bytes. `ki_convert_to_file` kept the previous destination.
- **Ownership:** in every run, the source descriptor was still open after the call returned (`fcntl(F_GETFD)`), and the adapter's owner closed it. This matches the header: callback state belongs to the caller.
- **Memory and streaming:** `--large-mib 16` produced 270,600 lines, 17,609,539 bytes as UTF-8 and 28,724,680 bytes as UTF-16LE. Python generated the UTF-16 into a pipe to the adapter's stdin, in 64 KiB pieces, with no intermediate file.

| Process (converter only, `/usr/bin/time`) | Peak RSS | Time |
|---|---:|---:|
| `xml2pdf` on the UTF-8 file | 2,768 KiB | 2.06 s |
| adapter `--mode utf8` (pass-through) on the same file | 2,700 KiB | 1.95 s |
| adapter, UTF-16 from the pipe | 2,832 KiB | 2.08 s |

  All three PDFs have SHA-256 `fb6b0204…`: 15,159,594 bytes and 16,237 pages. The UTF-16 route costs +64 KiB RSS compared with `xml2pdf`. The adapter state is 15,072 bytes: a 4,096-byte source buffer, a 1,536-byte declaration stage and two 4,608-byte rings. Prediction 7 said under 8 KiB, which was wrong. Packing the two rings into one byte per entry would bring it to about 10 KiB, and it is bounded either way.

### Standard-tool route: glibc `iconv` 2.41

- `iconv -f UTF-16 -t UTF-8 | xml2pdf - out.pdf` fails with `bad xml declaration at line 1, column 38, byte 37`, because the declaration still says UTF-16. Adding `sed '1s/encoding="UTF-16"/encoding="UTF-8" /'` makes the output identical (`81dd77c7…`).
- On 28.7 MB of UTF-16 from a pipe, `iconv` peaked at 1,676 KiB RSS, so it streams. Its output hash matched the Python oracle.
- Without a BOM, `-f UTF-16` assumes **LE**. The BE-without-BOM fixture came out as CJK-looking garbage with no error. The `sed` expression also does not match `UTF-16LE`/`BE`.
- A lone surrogate gives `iconv: illegal input sequence at position 134` (or 577914), which matches the oracle. Downstream, however, `xml2pdf` sees a truncated document and reports a **misleading XML error** (`unclosed element at line 20004 …`). Only `set -o pipefail` exposes `iconv`'s exit status. A file destination was kept, because the XML failure is atomic. On stdout, a partial PDF can remain.

## Internal decoder: feasibility from the parser code (not implemented)

- **Where it would go.** `fill()` in `src/xml_parse.c` reads raw bytes into the 4,096-byte `buf`, which every rule then reads as UTF-8. A decoder layer would read raw UTF-16 into a second buffer of about 4 KiB per parser and transcode into `buf`. The pending state is one code unit byte and one high surrogate. `consume_bom` would switch modes instead of returning −2 for `FF FE`/`FE FF`. The no-BOM `3C 00 3F 00` / `00 3C 00 3F` detection and a `UTF-16`/`UTF-16LE`/`UTF-16BE` check in `parse_xml_decl` would be added there.
- **Offsets need no map inside the library.** Every UTF-8 lead byte of 1–3 bytes corresponds to 2 source bytes, and every 4-byte lead to 4. `consume()` can therefore add 2 or 4 to `p->offset` on lead bytes and 0 on continuation bytes. Then `KiEvent.byte_offset` and the "byte B" in error messages stay raw input bytes, as the header promises, and line and column are unchanged.
- **Better errors than the external route.** Malformed UTF-16 would become `KI_CAUSE_XML` with line, column and byte, instead of `KI_CAUSE_IO`/`read error` plus adapter-private detail.
- **Cost estimate.** About 150–250 lines of C, plus about 200 lines of tests (the matrix above, with one-byte reads), fuzz seeds, GCC/Clang sanitizer runs, and README EN/RU, CLI help, contract and CHANGELOG updates. ABI 0.2 is unchanged if detection is automatic. **However, the behavior and contract change:** inputs that are honestly rejected today would be accepted. There is also the policy question of what "the 4,096-byte input window" means, raw bytes or transcoded bytes. Every later parser change would need UTF-16 coverage.
- **Out of scope for any decoder.** Glyph repertoire, CJK/emoji/shaping, and windows-1251 or ISO-8859-1. Those each need their own proven input.

## Alternatives compared

| | Keep UTF-8 only (today) | External adapter (`read_bytes` or `iconv` + `sed`) | Internal UTF-16 decoder |
|---|---|---|---|
| Correctness on synthetic LE/BE/BOM | rejects | identical PDFs; exact offsets with a ring | expected the same; not built |
| Error cause and position | n/a | `KI_CAUSE_IO`/`read error`; detail and source offset only in the adapter. `iconv` turns the failure into a misleading XML error unless `pipefail` is set | `KI_CAUSE_XML` with raw line/column/byte |
| No-BOM handling | n/a | the adapter enforces a strict policy; `iconv` assumes LE and silently garbles BE | the strict policy in one place |
| RAM | baseline | +64 KiB RSS (adapter state 15 KB); `iconv` 1.7 MB in a separate process | about +4 KiB per parser |
| Build/licensing | none | none (C99; glibc `iconv` is already on the system) | none new |
| Maintenance | none | every consumer writes and maintains a transcoder, declaration rewrite and offset map (about 490 lines of C99 in this prototype before `main`) | ~200 lines in the library, a permanent fuzz/test burden, a contract change |

## Decision and conditions for reopening

**Defer.** No real consumer, source system or target report exists, so nothing shows which side of this trade-off matters. The external route already works today without any library change:

- `iconv` with the declaration `sed` and `set -o pipefail`, for BOM input on the CLI;
- a `read_bytes` adapter like the prototype, for API users who need strict detection and source offsets.

OSR-16 stays open in Linear. The return condition is a concrete input and consumer, as the card states.

If a consumer appears, measure their real file against this harness first. Choose the internal decoder only if the consumer needs `KI_CAUSE_XML` with raw positions for malformed UTF-16, or cannot run an adapter. A separate narrow implementation card would then include:

- BOM and no-BOM detection exactly as in the policy above;
- `UTF-16`/`UTF-16LE`/`UTF-16BE` declaration checks;
- raw-byte offsets through the lead-byte rule;
- `KI_CAUSE_XML` for malformed or truncated units and surrogates;
- one-byte-read tests reusing this matrix;
- fuzz and sanitizer coverage;
- no glyph change, ABI 0.2 unchanged, and documented input-window semantics.

This spike does not deliver that card.

## Files

- `predictions.txt`: expected values, written before any measurement.
- `results.json`: all 168 check records, the current-library characterization, fixture hashes and the large-run measurements.
- `SHA256SUMS`: fixtures, adapter builds, the binary, the library, the font and the research files.

## Linear status snapshot

Read with the Linear tools on 2026-10-16 at about 13:33 MSK, after the measurements: OSR-16 is in state **Backlog**, with `completed_at` null and last updated 2026-10-15T14:16:35Z. This spike did not change it. The card's return condition, a concrete input and consumer, is still unmet.
