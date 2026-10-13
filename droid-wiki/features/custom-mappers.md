# Custom mappers

Active contributors: Artem Klen

A mapper is a caller-supplied callback that replaces both built-in profiles: when `KiOptions.map` is set, `records-v1` and `report-v1` are not used at all. The catalog example (`examples/catalog_map.c`, `examples/catalog_driver.c`) shows the full pattern for a small project-owned schema, and `docs/mapper-guide.md` is the step-by-step guide.

## Purpose

Let applications render their own XML schemas with the same parser, layout, font and PDF writer, without changing library code. The library contract keeps memory bounded, so a mapper must work from fixed-size state and streaming fragments rather than by collecting a document in memory.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| `KiOptions.map` / `map_user` | `include/ki_xml_pdf.h` | The callback and its state pointer |
| `KiEvent` | `include/ki_xml_pdf.h` | One START, END or TEXT event; pointers valid only during the callback |
| `KiDoc` | `include/ki_xml_pdf.h` | Opaque handle the mapper writes to |
| `ki_doc_text`, `ki_doc_break`, `ki_doc_fail` | `src/convert.c` | Emission and failure entry points |
| `CatalogMap` | `examples/catalog_map.h` | Example mapper state: one state, one flag, one counter |
| `catalog_map_event` | `examples/catalog_map.c` | Example mapper for the catalog grammar |

## How to write a mapper

The callback signature is `int map(void *user, KiDoc *doc, const KiEvent *ev)`. `convert_streams` in `src/convert.c` calls it for every event and treats a nonzero return as failure. The pattern, following the catalog example:

1. Keep fixed-size state. `CatalogMap` is one enum state, one `value_started` flag and one item counter. It allocates no field, item or document buffer, stores no event pointers, and has a maximum schema depth of three. Sorting, joins and document-wide lookups need a different resource contract and do not fit this pattern.
2. Initialize with `ki_options_init`, then set `map` and `map_user`, and keep both the options and the state alive until the conversion returns.
3. Handle `KI_EV_START`, `KI_EV_TEXT` and `KI_EV_END`. There is no EOF event: validate required children when the parent closes (the catalog checks `item requires name then price` at item end) and emit summaries when the root closes.
4. Check every emission result and return failures immediately. Return `ki_doc_text` and `ki_doc_break` failures directly; replacing them with a profile failure would hide the real font, quota or I/O cause.
5. For a schema failure, build a bounded message that names the rule and the offset, then return `ki_doc_fail(doc, message)`. The catalog uses `"catalog: %s at byte %zu"`. `ki_doc_fail` records `KI_CAUSE_PROFILE`.

## Borrowed pointers

`KiEvent` and its `name`, `text` and attribute pointers are valid only for the duration of the current callback. Read attributes immediately: the catalog consumes the `id` attribute inside the `item` START callback and never keeps the pointer. If another schema needs metadata later, copy only bounded metadata into your own state; retaining an event pointer does not extend its lifetime.

## Fragments

A text event is a fragment, not a complete field, and not necessarily a complete UTF-8 character. Use `event->text_len`, in bytes. The catalog sends each fragment straight to `ki_doc_text(doc, event->text, event->text_len)`; `KiDoc` handles character boundaries and physical line and page wrapping. TEXT and CDATA use the same path, references are already decoded in ordinary text, and reference-looking text inside CDATA stays literal. A mapper that preserves fragment whitespace keeps a space between separate TEXT/CDATA events intact; field length is bounded by library quotas, not by a mapper buffer.

For each field, the first nonempty fragment emits the fixed label and `": "`; the END event calls `ki_doc_break`, and a field with no fragments emits the label and a bare colon first, so an empty value prints `Name:` or `Price:`.

## Byte offsets in diagnostics

`KiEvent.byte_offset` is the parser position after the event's bytes were consumed. An empty element's synthetic END uses the same position as its START. It is not a character index and not the beginning of the tag. `ki_doc_fail` does not append an offset for you: compose the message yourself, as the catalog does with `snprintf(message, sizeof message, "catalog: %s at byte %zu", rule, event->byte_offset)`.

## The catalog example

Grammar and output:

| Element | Attributes | Content |
| --- | --- | --- |
| `catalog` | none | Zero or more `item` elements |
| `item` | Optional nonblank `id` | Exactly one `name`, then exactly one `price` |
| `name`, `price` | none | Text, CDATA and references; no nested elements |

The mapper walks eight states: `BEFORE_CATALOG`, `IN_CATALOG`, `NEED_NAME`, `IN_NAME`, `NEED_PRICE`, `IN_PRICE`, `ITEM_DONE`, `AFTER_CATALOG` (`examples/catalog_map.c`). It prints `Catalog`, `Item <id or number>`, `Name: value`, `Price: value` and `Items: N`; missing IDs use the source item number starting at 1, duplicate IDs are allowed, empty catalogs and empty fields are valid, and prices are lexical text with no numeric parsing.

The rule strings the example produces, all prefixed `catalog: ` and suffixed ` at byte N`:

| Rule | Raised when |
| --- | --- |
| `namespaces are not supported` | A colon in an element or attribute name, or an `xmlns` attribute |
| `unknown attribute <attr> on <element>` | An attribute outside the allowed set |
| `item nesting` | `item` starts outside `catalog` |
| `catalog nesting` | `catalog` starts twice, or ends at the wrong state |
| `name order or nesting` / `price order or nesting` | `name` or `price` appears in the wrong position or order |
| `name nesting` / `price nesting` | A field end event in the wrong state |
| `item requires name then price` | `item` closes without one `name` then one `price` |
| `unknown element <name>` / `unknown end element` | Any other element name |
| `structural text` | Non-whitespace text outside `name` or `price` |
| `empty item id` | An `id` with no non-whitespace character |
| `item counter overflow` | The item counter reaches `ULLONG_MAX` |

The driver `examples/catalog_driver.c` runs `catalog [--max-pages N] INPUT OUTPUT [CHUNK_BYTES]`. Without a chunk argument it calls `ki_convert_file`. A positive chunk argument chooses a caller-owned `FILE` reader with `ki_convert_to_file`, capped at that many bytes per callback: `1` reads at most one byte per callback, `4096` is an ordinary size. A zero, negative, invalid or overlong chunk is a usage error with exit 2, as is `--max-pages 0`. The driver owns the input `FILE` and closes it; the library never closes or frees callback resources. On failure it prints `cause=<number>` plus the message to stderr and returns the library status. Leave `KiOptions.record_offset` and `record_limit` at zero: they belong to the built-in report profile, and a custom count belongs in the mapper state.

## Building against an installed library

```sh
prefix="$HOME/.local"
make install PREFIX="$prefix"
cc -std=c99 -Wall -Wextra -I"$prefix/include" \
  examples/catalog_driver.c examples/catalog_map.c \
  "$prefix/lib/libki_xml_pdf.a" -o catalog
```

Keep the mapper sources in your application and include only the installed `include/ki_xml_pdf.h`. For a shared library, replace the archive argument with `-L"$prefix/lib" -lki_xml_pdf` and make the directory findable at runtime. See [Packaging](packaging.md).

## Verification

`tests/catalog_verify.py` builds no binary; run it after `make example`:

```sh
python3 tests/catalog_verify.py
```

It checks the full oracle `examples/catalog.expected.txt`, empty fields and catalogs, duplicate IDs, significant spaces, literal CDATA and decimal and hex references. For a field larger than 4096 bytes it compares every character for file input, one-byte and ordinary callback reads. Negative cases assert the profile byte offsets, distinct XML, font and resource causes, an unchanged previous destination (same bytes and inode) and temporary-file cleanup. Each failing event has its own consumed-byte oracle computed from the fixture text.

## Integration points

- Precedence: `convert_streams` calls `opt->map` first, before both built-in profiles.
- Concurrency: use one mapper state per conversion; see [C API](../api/c-api.md) for the context rules.
- The library validates truncated or malformed XML before successful file publication, so a late schema error in a mapper still preserves the destination ([Atomic output](atomic-output.md)).

## Entry points for modification

- To adapt another schema, copy `examples/catalog_map.c` and `examples/catalog_map.h`, replace the state transitions and labels, define allowed attributes and child order, and add independent positive and negative fixtures.
- To extend what mappers can do, change `KiEvent` or `KiDoc` in `include/ki_xml_pdf.h` and the mapper call in `convert_streams`; a struct layout change requires a minor version bump before 1.0.

## Key source files

| File | Contents |
| --- | --- |
| `docs/mapper-guide.md` | Full guide: grammar, ownership, fragments, errors, verification |
| `examples/catalog_map.h` | Mapper state struct and callback declaration |
| `examples/catalog_map.c` | The mapper implementation and rule messages |
| `examples/catalog_driver.c` | Driver wiring options, callbacks and the two file APIs |
| `examples/catalog.xml`, `examples/catalog.expected.txt` | Fixture and oracle |
| `tests/catalog_verify.py` | Positive, negative and usage verification suite |
