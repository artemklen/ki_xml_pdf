# Security
Active contributors: Artem

XML, TrueType fonts and filesystem operations are trust boundaries. The implementation uses explicit validation and quotas, but this is not a sandbox or a proof that hostile input can never find a defect.

## Supported versions and reporting

`SECURITY.md` supports only the latest 0.y version, currently **0.3.0**.
Older 0.y versions are unsupported; before 1.0 a new minor may change ABI.

Report vulnerabilities privately to **artem.klen@icloud.com**.
Include affected version/commit, impact, reproduction steps and the smallest input you may share.
Do not post security details publicly or send secrets/private source documents.

The policy includes memory safety, unexpected code execution, information leaks from XML/fonts and violations of documented converter-memory/page/output limits.
Ordinary functional bugs belong in public issues under `CONTRIBUTING.md`.
Conduct reports use the same private contact under `CODE_OF_CONDUCT.md`.

## Boundaries

| Input/state | Defensive boundary | Implementation |
| --- | --- | --- |
| XML bytes | Fixed windows/stacks, UTF-8/XML Char checks, bounded tokens and declaration rules | `src/xml_parse.c`, `src/xml_parse.h` |
| Font bytes | 8 MiB cap, table/span/GID/outline checks and pre-embed consistency recheck | `src/font.c` |
| Generated PDF | Counted output, xref ceiling, object/page/output/spool quotas | `src/pdf_write.c` |
| Geometry and metadata | Early validation and copied metadata; safe PDF string serialization | `src/geometry.c`, `src/info.c` |
| Filesystem publication | Beside-destination temporary, flush/fsync/close, rename and cleanup | `src/convert.c` |
| Callbacks | Caller-owned code/resources with strict I/O counts | `include/ki_xml_pdf.h`, `src/convert.c` |

The library has no XML network/entity resolver.
Caller-provided callbacks can perform their own external actions and are not sandboxed by the library.
Likewise, embedding applications remain responsible for trusted path selection and process permissions.

## XML subset

A DOCTYPE may be skipped before the root, but neither its external subset nor declared entities are loaded.
Only the five predefined entities and valid numeric character references are decoded.
There is no document-defined entity table or recursive entity expansion.

Every consumed byte must encode valid UTF-8/XML characters, including comments, processing instructions, DTD bodies and content after the root.
Selection does not bypass these checks or report-profile validation.
Malformed declarations, duplicate attributes, unsupported references and invalid tails fail conversion.

Bounds include depth 64, eight attributes, 63-byte names, 255-byte decoded attribute values and a 4096-byte input window.
These reject out-of-contract shapes; they do not cap total input length or CPU time.
Use application scheduling/cancellation for unbounded streams and time budgets.

## Font safety and limits

`src/font.c` validates the table directory and the spans it reads, format-4 cmap mappings, metrics and optional outline tables.
GID 0/missing mappings fail; zero advance is valid.
Body output has a restricted repertoire, not universal Unicode/shaping support.

The loader releases temporary table spans and retains fixed metrics plus an open file.
Before streaming the complete font to Flate in 16 KiB chunks, it rechecks file size and FNV-1a.
This catches ordinary mutation; **FNV-1a is not a cryptographic integrity guarantee**, and the check is not a lock against concurrent modification.
Keep font inputs stable and choose trusted fonts when possible.

Font names are escaped as PDF names.
Title/Producer are validated UTF-8 and serialized as UTF-16BE hex strings, not interpolated PDF syntax.
CreationDate is explicitly parsed; there is no implicit clock, XMP or PDF/A claim.

## Resource and publication guarantees

The writer limits objects to 8,000,000 and total output to the ten-digit classic-xref ceiling, 9,999,999,999 bytes.
The offset spool defaults to 64 MiB.
Positive caller quotas add page/output/spool limits; zero output quota still keeps the xref ceiling.
Internal zlib state, two-pass CMap scratch and one reusable page-content buffer are bounded, not allocation-free conversion.

The ≤4096 KiB RSS rule is an ordinary-converter synthetic acceptance gate, not a general sandbox limit for every accepted font or user callback.
Generators/readers/sanitizers are measured separately.
The older `de8ba0e` heap profile must not be substituted for current memory evidence.

Atomic file routes validate the entire input and finish/flush/fsync/close before rename.
Handled errors preserve the previous destination.
The temporary PDF is beside the destination; `temp_dir` controls only the spool.
Same-file checks catch ordinary linked aliases, but path validation is not a defense against an adversary concurrently rewriting the directory namespace.

A destination symlink is replaced by rename rather than having its target opened for truncation.
No directory fsync is performed, so atomic replacement is not a power-loss durability guarantee.
SIGKILL cannot run cleanup.
Callback/stdout bytes cannot be rolled back, even if a later XML or font error occurs.

## Validation coverage

`make test-san` runs fatal ASan/UBSan checks.
`tests/fuzz_smoke.py` uses bounded seeded XML/font mutants and external-reader controls, not exhaustive fuzzing.
`tests/test_callback_io.c` covers first-write-failure latching and exact accepted-byte reporting.
`tests/geometry_check.py`, `tests/info_check.py` and `tests/integration_check.py` check combined output independently.
See [Testing](how-to-contribute/testing.md) and [Resource limits](features/resource-limits.md).

## Key source files

| File | Role |
| --- | --- |
| `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md` | Support/reporting/community policy |
| `include/ki_xml_pdf.h` | Public ownership and failure contract |
| `src/xml_parse.c`, `src/font.c` | Untrusted XML/font readers |
| `src/convert.c`, `src/pdf_write.c` | Publication, quotas and output accounting |
| `src/info.c` | Metadata validation and serialization |

Related: [Atomic output](features/atomic-output.md), [Pitfalls](background/pitfalls.md), [XML parser](systems/xml-parser.md).
