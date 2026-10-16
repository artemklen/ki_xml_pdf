# OSR-11 XML subset evidence

Paths are normalized: `<evidence-root>` is the out-of-tree evidence directory and `<repo>` the working checkout.

## Retained corpus

`tests/corpus/xml/` holds the exact OSR-11 audit inputs, byte for byte and without a trailing newline; `SHA256SUMS` lists their hashes (`sha256sum -c SHA256SUMS`). `osr11-dtd-comment-bracket.xml` and `osr11-xml-stylesheet.xml` must convert; every other file must fail with an XML cause. The `xml_audited_corpus` test compares each file with the literal bytes in `tests/test_xml_subset.c`, and `tests/fuzz_smoke.py` runs each file through the CLI directly, after a closed report root and, where applicable, inside a record skipped by `--record-limit 1`.

## Original failure (accepted base 075455c)

`baseline-075455c-suite.txt`: the new `tests/test_xml_subset.c` cases compiled with the pristine `src/xml_parse.c`/`src/xml_parse.h` from `075455c` and the other current sources (`<evidence-root>/osr-11/base-parser`). All nine new tests fail and every existing test passes. Examples: a literal U+FFFE in text is accepted by the parser and reported as `font: U+FFFE` by conversion; `&#X41;` is accepted; `<?xml-stylesheet ...?>` is rejected as a reserved PI; `<!DOCTYPE root [ <!-- ] --> ]><root/>` is rejected; text split at the 1023-byte chunk accepts an illegal scalar; and a U+FFFE in a record skipped by report selection publishes a PDF.

## Fix (`src/xml_parse.c`)

- `feed_utf8` assembles each scalar and checks the XML `Char` production for every consumed byte, so all contexts, including the skipped DTD and content after the root, share one check. Text and CDATA chunks start a new event before a UTF-8 lead byte that does not fit, and a sequence left open at `<` or EOF fails.
- Character references accept only a lowercase `x` for hexadecimal.
- `parse_pi` reads the target as a bounded name followed by white space or `?>`. The target `xml` in any letter case is reserved; only the lowercase target at the document start opens `parse_xml_decl`, which streams ordered, unique, space-separated `version`/`encoding`/`standalone` pseudo-attributes. PI data is streamed and never reported.
- `skip_doctype` accepts one DOCTYPE before the root: white space, name, optional `SYSTEM`/`PUBLIC` literals (public ID characters checked), optional internal subset and `>`. `skip_internal_subset` accepts white space, parameter-entity references, comments, PIs and `ELEMENT`/`ATTLIST`/`ENTITY`/`NOTATION` declarations whose quoted values may contain `]` and `>`. Identifiers are never opened or fetched and declared entities are discarded, so their references stay unknown entities.
- Parser state stays fixed: a 4096-byte window, 64-byte names, 8 attributes, 256-byte attribute values, 1023-byte text events and 64 levels of nesting.

## Coverage (`tests/test_xml_subset.c`)

Every direct parser case runs at read caps 1, 2, 4095 and 4096 and requires the same verdict and event projection at each cap, with no text event ending inside a UTF-8 scalar.

| Test | Cases |
| --- | --- |
| `xml_char_contexts` | 28 illegal scalars/malformed UTF-8 sequences and 11 legal boundaries (U+0009 … U+10FFFF) in 20 contexts: text, attributes, CDATA, comments, PIs before/inside/after the root, DTD comment/entity/ATTLIST/PI, SYSTEM literal, names |
| `xml_references` | predefined entities and numeric references in text/attributes, CDATA literal spelling, 42 malformed or illegal references |
| `xml_audited_corpus` | exact OSR-11 bytes from `tests/corpus/xml` |
| `xml_pi_and_declaration` | valid `xml-*` PIs and declarations; reserved/misplaced/mixed-case `xml`, bad targets, unterminated PIs, duplicate/order/separator/value/truncation declaration errors |
| `xml_doctype_and_dtd` | supported DOCTYPE forms, delimiter-aware internal subsets, placement/duplication/keyword/literal/subset errors, entity and external-identifier sentinels |
| `xml_late_tails` | 15 malformed constructs after a 5000-byte tail beyond the root |
| `xml_chunk_boundaries` | 12 valid and 12 invalid pieces at offsets around the 1023-byte text chunk and the 4096-byte window |
| `xml_public_causes` | `ki_convert` causes at read sizes 1/4096 (XML vs FONT for a legal unsupported glyph), custom mapper, PI absence in `pdftotext` output |
| `xml_selected_tails` | report selection `record_limit=1` with poison in record 100 or after the root at read sizes 1/4095/4096: `KI_CAUSE_XML`, previous destination kept, no temporary file left |
