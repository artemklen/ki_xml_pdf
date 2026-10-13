# records-v1

Active contributors: Artem Klen

`records-v1` is the default profile: a schema-free diagnostic dump that prints every element name, every attribute and all text content in document order. It is implemented by the `map_event` function in `src/convert.c` and specified historically in `docs/contract.md` (Russian).

## Purpose

Turn any XML input, including inputs with no known schema, into readable PDF text with bounded memory. The README describes it as the profile for diagnostic use; the report schema and custom formats have their own pages ([report-v1](report-v1.md), [Custom mappers](custom-mappers.md)). `records-v1` renders repeat elements as sequential blocks: there are no tables, sorting, aggregates or forward references, so no second pass is needed.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| `map_event` | `src/convert.c` | The mapper: turns parser events into layout text and breaks |
| `MapState` | `src/convert.c` | Mapper state: current depth plus an `xml:space` flag per open element (64 entries) |
| `text_is_space` | `src/convert.c` | Space, tab, CR and LF test for text fragments |
| `emit_str` | `src/convert.c` | Writes a string to the layout and breaks the line |
| `docs/contract.md` | `docs/contract.md` | Original contract: page rules, limits, measurements, non-promises |

## Output shape

- The element name is printed on its own line.
- Each attribute is printed as a `name: value` line, in source order.
- Element text is preserved, including spaces between words.
- The element end produces a line break.

`<doc>Hello</doc>` therefore renders as two lines, `doc` and `Hello`; an element `<item id="a" kind="x">text</item>` renders as `item`, `id: a`, `kind: x`, then `text`. The default-profile check in `tests/cli_pipeline.py` asserts exactly this shape for `<doc>Hello</doc>`.

## Whitespace and xml:space

`map_event` tracks one `xml:space` flag per open element. On a start element the flag is inherited from the parent, then overridden by the element's own `xml:space` attribute: the value `preserve` turns preservation on, any other value turns it off.

For a text fragment, preservation is on when the fragment comes from CDATA or the enclosing element has its flag set. An all-whitespace fragment that contains a line break is dropped only when all of these hold:

- the fragment is whitespace only (space, tab, CR, LF),
- it contains at least one line break,
- the current output line is empty,
- preservation is off.

Everything else is emitted as-is. The effect, in the words of the code comment in `src/convert.c`: indentation that contains a newline is dropped only when the current line is empty and `xml:space` is not preserve; CDATA whitespace and spaces inside a line are kept, so a split `"A" + " " + "B"` stays `"A B"`. The parser normalizes literal CR and CRLF to LF, keeps `&#10;` as a line break, and in attribute values turns literal whitespace characters into spaces while character references survive unchanged. Comments and DTD sections never reach the PDF, and namespaces are not resolved: a colon simply stays part of the element name.

## Historical measurement

Measured on 7 October 2026 on the first public revision [`7c62fe4`](https://github.com/artemklen/ki_xml_pdf/commit/7c62fe4af72b097885d76a7f9f897492f5c4c2d5), Linux 64-bit with GCC, measuring only the `./xml2pdf` process (the XML generator and the PDF reader are measured separately). The workload repeats a short `<item>...</item>` record, as in `tests/verify.sh`.

| Measure | Value |
| --- | --- |
| XML input | 16,777,313 bytes |
| PDF output | 173,535,332 bytes |
| PDF pages | 41,944 |
| Peak RSS | 2360 KiB |
| Offset spool | 671,168 bytes |

Commands were `make check` and `python3 tests/large_proof.py 1073741824`. These are measurements of one revision and one synthetic workload, not a promise for arbitrary inputs; the RSS acceptance gate is 64 MiB. See [Resource limits](resource-limits.md).

## Input limits inherited from the pipeline

`records-v1` accepts whatever the parser and layout accept: depth up to 64 open elements, names up to 63 bytes, at most 8 attributes per element, decoded attribute values up to 255 UTF-8 bytes, text fragments up to 1023 bytes and a 4096-byte input window. The layout repertoire is U+0009, U+000A, U+000D, U+0020 to U+04FF and U+2010 to U+2027; other characters are font errors. Pages are Letter with 40 pt margins, DejaVu Sans at 10 pt and 14 pt line spacing. Details are on [XML parser](../systems/xml-parser.md) and [Layout and fonts](../systems/layout-and-fonts.md).

`KiOptions.record_offset` and `record_limit` belong to `report-v1`; `map_event` does not read them and they have no effect on this profile.

## Integration points

- `convert_streams` in `src/convert.c` calls `map_event` when `KiOptions.map` is `NULL` and the profile is not `KI_PROFILE_REPORT`, so `records-v1` is also the fallback for a `NULL` options pointer.
- The mapper writes through `Layout` directly rather than through `KiDoc`, because it lives inside the driver; external mappers use `ki_doc_text` and `ki_doc_break` instead.
- Parser, layout and writer behavior, error causes and quotas are shared with the rest of the pipeline ([Conversion pipeline](../systems/conversion-pipeline.md)).

## Entry points for modification

- Whitespace rules: the `XML_EV_TEXT` branch of `map_event` in `src/convert.c` is the only place indentation is dropped; the comment above it documents the intended behavior.
- `xml:space` handling: the flag inheritance and override live at the top of the `XML_EV_START` branch.
- Output shape: `emit_str` calls and the attribute loop. Any change should update `tests/cli_pipeline.py` expectations and add fixtures.

## Key source files

| File | Contents |
| --- | --- |
| `src/convert.c` | `map_event`, `MapState`, `text_is_space`, `emit_str` |
| `docs/contract.md` | Original contract in Russian: rules, limits, measurement, non-promises |
| `tests/cli_pipeline.py` | Default-profile output check (`doc`, `Hello`) |
| `tests/verify.sh` | Large synthetic workload, first/last page completeness checks |
