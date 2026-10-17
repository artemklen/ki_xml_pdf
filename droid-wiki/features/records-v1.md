# records-v1
Active contributors: Artem

`records-v1` is the default diagnostic profile. `map_event` in `src/convert.c` prints element names, attributes and text in source order without requiring a business schema.

## Output model

| Parser event | Rendering |
| --- | --- |
| START | Element name, then each attribute as `name: value`, with a break after each |
| TEXT / CDATA | Text fragments, subject to the indentation rule below |
| END | Break the current line; no closing-tag text |

For example:

```xml
<doc><item id="a">Hello</item></doc>
```

produces logical lines:

```text
doc
item
id: a
Hello
```

Physical wrapping can split these lines. The profile is a diagnostic view, not XML round-tripping: declarations/comments/DOCTYPE are not rendered, references are decoded, attributes normalized, and namespace prefixes are literal names rather than resolved namespaces.

## Whitespace

`MapState` in `src/convert.c` holds current depth and a 64-entry `xml:space` preservation stack. START inherits its parent's flag; a supplied `xml:space="preserve"` enables it and any other supplied value disables it. The attribute itself still prints as ordinary diagnostic content.

An all-whitespace fragment is dropped as indentation only when it:

- Contains LF or CR.
- Arrives while the layout line, pending word and held UTF-8 bytes are empty.
- Is neither CDATA nor within preserved space.

Spaces inside content, whitespace-only fragments without a newline, CDATA whitespace and preserved whitespace are emitted. A pending word counts as content, so a following fragmented newline is not mistaken for indentation.

The parser normalizes literal CR/CRLF to LF and literal attribute whitespace to spaces; references follow their decoded-character rules. The mapper does not trim arbitrary text or assemble a whole element value.

## Limits and configuration

The profile keeps no record collection and no growing ID set. It shares parser bounds, font repertoire, word wrapping, page geometry and quotas with all mappers. See [XML parser](../systems/xml-parser.md), [Layout and fonts](../systems/layout-and-fonts.md) and [Page geometry](page-geometry.md).

`KiOptions.record_offset` and `record_limit` have no effect here; those belong to [report-v1](report-v1.md). A custom `map` callback replaces this profile completely.

This mapper is a small part of the single pipeline implementation unit, not its own module. Shared regression coverage is in `tests/test_stream.c`; `tests/test_layout_wrap.c` checks pending-word/indentation interactions, and `tests/cli_pipeline.py` asserts the default `doc` / `Hello` shape. `tests/integration_check.py` checks complete word order under non-default geometry/fonts and all I/O routes.

## Change entry points

- Output labels/shape: `emit_str` and the START/END cases of `map_event` in `src/convert.c`.
- Indentation and inheritance: its TEXT branch and `MapState`; preserve fragment-boundary spaces.
- Schema-specific behavior: prefer a [Custom mapper](custom-mappers.md) instead of making the diagnostic profile recognize application schemas.
- Resource expectations: use revision-scoped evidence described in [Resource limits](resource-limits.md), not old uncompressed output figures.

## Key source files

| File | Role |
| --- | --- |
| `src/convert.c` | `MapState`, `map_event`, whitespace tests and line emission |
| `include/ki_xml_pdf.h` | Default profile constants and options |
| `tests/test_layout_wrap.c` | Pending word, preserved indentation and profile fragmentation |
| `tests/cli_pipeline.py` | Default-profile output check |
| `tests/integration_check.py` | Complete diagnostic word-order oracle |
| `tests/verify.sh` | Large default-profile resource and completeness gate |
| `docs/contract.md` | Historical core contract, with revision-scoped measurements |

Related: [Conversion pipeline](../systems/conversion-pipeline.md), [report-v1](report-v1.md), [Streaming I/O](streaming-io.md).
