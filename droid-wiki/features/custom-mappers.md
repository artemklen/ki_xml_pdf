# Custom mappers
Active contributors: Artem

A caller-supplied `KiOptions.map` callback replaces both built-in profiles. It validates an application schema and emits incremental text through `KiDoc`, while the library retains XML parsing, fonts, wrapping, PDF generation and publication.

## Minimal integration

```c
CatalogMap mapper;
catalog_map_init(&mapper);
KiOptions opt;
KiResult result;
ki_options_init(&opt);
opt.map = catalog_map_event;
opt.map_user = &mapper;
int status = ki_convert_file("catalog.xml", "catalog.pdf", &opt, &result);
```

This uses `examples/catalog_map.h` and `examples/catalog_map.c`. The callback signature is `int map(void *user, KiDoc *doc, const KiEvent *ev)`. Return zero for success, nonzero to stop. `src/convert.c` always dispatches custom map before a built-in profile; the ordinary options/profile fields must still pass validation.

## Mapper rules

1. Keep current state bounded. The example holds one state, one value-started flag and one counter, not field/item/document buffers.
2. Handle START, TEXT and END; there is no mapper EOF event. Validate required children at parent closure and emit summaries at root closure.
3. Use `text_len` in bytes, not `strlen` assumptions for arbitrary fragments. Send TEXT and CDATA through the same emission path and preserve significant spaces.
4. Check every `ki_doc_text` / `ki_doc_break` return and propagate failures immediately. Replacing them with `ki_doc_fail` hides font/resource/I/O causes.
5. Use `ki_doc_fail(doc, "schema: rule")` for schema errors. The library appends the current consumed `at line L, column C, byte B`; do not append a second offset yourself.

`KiEvent` and its name/text/attribute pointers are borrowed only during that callback (`include/ki_xml_pdf.h`). Consume attributes immediately or copy only bounded metadata; never save an event pointer.

Text is incremental, not a whole field. The current parser emits scalar-complete chunks, and `ki_doc_text` also accepts caller emission split within UTF-8 sequences. The pending word survives separate calls and TEXT/CDATA boundaries; no application word buffer is needed. See [Layout and fonts](../systems/layout-and-fonts.md).

## Catalog example

| Element | Attributes | Content |
| --- | --- | --- |
| `catalog` | None | Zero or more items |
| `item` | Optional nonblank `id` | Exactly one `name`, then one `price` |
| `name`, `price` | None | Text/CDATA/references, no nested elements |

`examples/catalog_map.c` has eight states: before catalog, in catalog, need/in name, need/in price, item done, after catalog. It rejects namespaces, unknown attributes/elements, invalid child order and structural text.

Output is `Catalog`, `Item <id or original number>`, `Name: value`, `Price: value`, then `Items: N`. Empty fields use a bare colon, duplicate IDs are allowed, empty catalogs are valid, and prices are lexical strings rather than parsed amounts. Attribute values retain the parser's 255-byte bound; field values stream without a mapper buffer limit.

The example is two C implementation files (mapper and driver), one private header and one dedicated Python verifier, `tests/catalog_verify.py`. This is an example-level 1:2 test-to-implementation-file ratio, not a coverage metric. Both files include only the public library interface.

## Run and install

```sh
make example
./examples/catalog examples/catalog.xml catalog.pdf
./examples/catalog examples/catalog.xml one-byte.pdf 1
python3 tests/catalog_verify.py
```

The driver accepts `catalog [--max-pages N] INPUT OUTPUT [CHUNK_BYTES]`. No chunk uses file input; a positive chunk uses a caller-owned FILE reader through `ki_convert_to_file`. Zero/negative/invalid explicit chunks are usage errors.

Against an installation, compile mapper sources in the application:

```sh
prefix="$HOME/.local"
cc -std=c99 -Wall -Wextra -I"$prefix/include" \
  examples/catalog_driver.c examples/catalog_map.c \
  "$prefix/lib/libki_xml_pdf.a" -lz -o catalog
```

Use the same prefix at build/install, or set an explicit font. Installed CMake/pkg-config targets manage zlib; see [Packaging](packaging.md).

## Selection, errors and ownership

Report offset/limit and `KiResult.report_*` belong to the built-in report mapper. The custom catalog renders all items and keeps its count in `CatalogMap.items`; it does not inherit report selection.

`ki_doc_fail` produces `KI_CAUSE_PROFILE`. Its position is **after** the event, not the beginning of the tag; empty-element END matches START. Long rule text is shortened at a UTF-8 boundary while retaining the whole position suffix.

Use separate mapper state for concurrent conversions. Callback resources remain caller-owned. The file APIs preserve old destinations on late schema/XML/quota errors; callback output cannot be rolled back.

## Change entry points and checks

Adapt state transitions/labels in `examples/catalog_map.c`, define bounded state in `examples/catalog_map.h`, and wire I/O in `examples/catalog_driver.c`. Add independent positive/negative fixtures for your schema. Sorting, joins and document-wide lookup sets need a different resource contract.

`tests/catalog_verify.py` checks full expected output, duplicate/empty/significant-whitespace cases, long fields with file/one-byte/ordinary reads, consumed positions, distinct causes and preserved destination bytes/inode with temporary-file cleanup.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Map/event/document and ownership contract |
| `src/convert.c` | Mapper precedence, emission calls and positioned failures |
| `examples/catalog_map.c`, `examples/catalog_map.h` | Fixed-state schema adapter |
| `examples/catalog_driver.c` | File/callback driver |
| `examples/catalog.xml`, `examples/catalog.expected.txt` | Original fixture and independent oracle |
| `tests/catalog_verify.py` | Public-interface positive/negative suite |
| `docs/mapper-guide.md` | Full integration and adaptation guide |

Related: [C API](../api/c-api.md), [Streaming I/O](streaming-io.md), [Atomic output](atomic-output.md).
