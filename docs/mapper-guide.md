# Map your XML through the public API

The catalog example is a small, project-owned XML schema. It demonstrates how
an application can supply its own mapper without changing the library parser,
layout or PDF writer. It supports the grammar below; other XML schemas need
their own adapters.

## Build and run

From the source tree:

```sh
make example
./examples/catalog examples/catalog.xml catalog.pdf
./examples/catalog examples/catalog.xml catalog-one-byte.pdf 1
./examples/catalog examples/catalog.xml catalog-callback.pdf 4096
```

The command is `catalog [--max-pages N] INPUT OUTPUT [CHUNK_BYTES]`.
Without a chunk argument it calls `ki_convert_file`. A positive chunk
argument chooses a caller-owned FILE reader and `ki_convert_to_file`;
`1` reads at most one byte per callback, while `4096` is an ordinary
read size. An explicitly supplied zero, negative or invalid chunk is a usage
error. `--max-pages` accepts a positive page limit before the paths.

To compile against an installed library, keep the mapper sources in your
application and include only the installed public header:

```sh
catalog_prefix="$HOME/.local"
make install PREFIX="$catalog_prefix"
cc -std=c99 -Wall -Wextra -I"$catalog_prefix/include" \
  examples/catalog_driver.c examples/catalog_map.c \
  "$catalog_prefix/lib/libki_xml_pdf.a" -lz -o catalog
./catalog examples/catalog.xml catalog.pdf
```

For a shared library, replace the archive and `-lz` arguments with
`-L"$catalog_prefix/lib" -lki_xml_pdf` and make that directory available
to the runtime linker. The installed font must remain at the configured prefix.
A relocated installation can set `KiOptions.font_path` explicitly.

## Catalog grammar

```xml
<catalog>
  <item id="pen-1">
    <name>Blue<![CDATA[ & white]]> pen &#x2014; caf&#233;</name>
    <price>12&#46;50</price>
  </item>
  <item>
    <name><![CDATA[Notebook]]></name>
    <price/>
  </item>
</catalog>
```

| Element | Attributes | Content |
| --- | --- | --- |
| `catalog` | none | zero or more `item` elements |
| `item` | optional nonblank `id` | exactly one `name`, then exactly one `price` |
| `name`, `price` | none | text, CDATA and supported character references; no nested elements |

The document has one root, uses UTF-8 and has no namespaces. Unknown attributes,
unknown elements, invalid order/nesting, blank IDs, missing fields and nonblank
structural text are profile errors. Empty catalogs and empty fields are allowed.
Whitespace between structural elements is ignored. Comments and processing
instructions are handled by the library and do not become catalog content.

An ID is consumed during its START event. Missing IDs use the source item number,
starting at 1. Duplicate IDs are allowed; the example does not retain a set of IDs.
Attribute values are subject to the parser's 255 UTF-8 byte limit. Field values
have no mapper buffer limit: their length is bounded by the library quotas.

The example prints `Catalog`, `Item <id or number>`, `Name: value`,
`Price: value` and the final `Items: N`. Empty values print `Name:` or
`Price:`. Prices are lexical text, without numeric parsing, currency
conversion or arithmetic. The independent full example oracle is
[../examples/catalog.expected.txt](../examples/catalog.expected.txt).

## Ownership and fragments

[../examples/catalog_map.h](../examples/catalog_map.h) defines the entire mapper
state: one integer state, one value-started flag and one item counter.
[../examples/catalog_map.c](../examples/catalog_map.c) moves through the fixed
catalog/item/name/price states. Its maximum schema depth is three. It allocates
no field, item or document buffer and stores no event pointers.

`KiEvent` and its name, text and attribute pointers are valid only during the
current mapper callback. Read attributes immediately. If another schema requires
metadata later, copy only bounded metadata into your own state; retaining the
event pointer does not extend its lifetime.

A text event is a fragment, not a complete field or necessarily a complete UTF-8
character. Use `event->text_len` in bytes. The mapper sends each fragment
directly to `ki_doc_text(doc, event->text, event->text_len)`; `KiDoc`
handles character boundaries and physical line/page wrapping: words break at
spaces and tabs, and a word split across calls is still one word. TEXT and CDATA
use the same emission path. References are already decoded in ordinary text;
reference-looking text inside CDATA stays literal. The mapper preserves fragment
whitespace, including a space between separate TEXT/CDATA events. The library
normalizes source CR/CRLF and applies its documented layout rules.

For each field, the first nonempty fragment emits a fixed English label and
`": "`. The END event calls `ki_doc_break`; a field with no fragments
emits the label and colon first. Check every emission result and return failure
immediately. This preserves the library's font, resource and I/O failure cause.

The driver initializes options with `ki_options_init`, sets `map` and
`map_user`, and keeps both options and state alive until conversion returns:

```c
CatalogMap mapper;
catalog_map_init(&mapper);
KiOptions options;
ki_options_init(&options);
options.map = catalog_map_event;
options.map_user = &mapper;
KiResult result;
int status = ki_convert_file("catalog.xml", "catalog.pdf", &options, &result);
```

Use a separate mapper state for each concurrent conversion. The mapper receives
START, TEXT and END events, with no EOF event. Validate required children when
the item closes and finish the summary when the root closes. The library checks
truncated or malformed XML before successful file publication.

The catalog prints all items. Leave `KiOptions.record_offset` and
`record_limit` at zero: those options belong to the built-in report profile.
`KiResult.report_*` also belongs to that profile; the custom item count lives
in `CatalogMap.items`.

## Errors and file publication

For a schema failure, return `ki_doc_fail(doc, rule)` with a message naming the
rule. This records `KI_CAUSE_PROFILE` and appends the current event position as
`at line L, column C, byte B`, so the mapper does not format an offset itself.
The position is the parser position **after** consuming the event, including
an empty element's synthetic END; it is not the beginning of the tag. The byte
equals `event->byte_offset`; line and column (1-based, Unicode scalars, CRLF as
one line break) are only available in the message. The result keeps the whole
position within 160 bytes and shortens a long rule at a UTF-8 boundary. A mapper
that still adds its own offset gets both.

`ki_doc_fail` is for mapper rules. Return an existing `ki_doc_text` or
`ki_doc_break` failure directly; replacing it with a profile failure would
hide the actual font, quota or I/O error. Malformed XML has `KI_CAUSE_XML`;
an unsupported glyph has `KI_CAUSE_FONT`; a page quota has
`KI_CAUSE_RESOURCE`.

Both file APIs publish by rename only after the entire input is validated and
the PDF is flushed, synced and closed. A late schema/XML error or quota failure
preserves the previous destination and removes the temporary output.

The callback reader returns `1..maximum` for data, `0` only at EOF and a
negative value on error. A positive short read is not EOF. The driver owns and
closes its input FILE; the library neither closes nor frees callback resources.
`ki_convert_to_file` ignores `write_bytes`. If you adapt the example to
`ki_convert` with a write callback, accepted output bytes remain on failure:
that API does not provide file rollback.

The driver returns the library status and prints `cause=<number>` plus the
message to stderr on failure. Usage errors return 2; a failed input open returns
3. Successful runs print item/page/output-byte counts.

## Verify and adapt

```sh
python3 tests/catalog_verify.py
python3 tests/catalog_verify.py --binary /absolute/path/to/catalog \
  --fixture /absolute/path/to/catalog.xml \
  --oracle /absolute/path/to/catalog.expected.txt \
  --pdftotext /absolute/path/to/pdftotext
```

Build the executable first. The suite also runs from another working directory.
It checks the fixed English oracle, empty fields/catalogs, duplicate IDs,
significant spaces, literal CDATA and decimal/hex references. It compares every
character of a field larger than 4096 bytes for file, one-byte and ordinary
callback reads; line/page separators are removed only for that known unbroken
payload. Negative cases check profile byte offsets, distinct XML/font/resource
causes, unchanged OLD destination bytes/inode and temporary-file cleanup.

To adapt another small schema, replace the state transitions and labels, define
the allowed attributes and child order, and add independent positive/negative
fixtures. Keep callbacks incremental and retain only fixed current state.
Sorting all items, joins and document-wide lookups require a different resource
contract and are outside this example.

