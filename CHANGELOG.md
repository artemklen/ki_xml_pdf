# Changelog

Версии до 1.0 не обещают стабильный ABI. Совместимость ломается вместе с номером 0.y.

## Unreleased

- Compress page contents and the embedded TrueType font with zlib as `/FlateDecode` streams. Each compressed stream is deflated through a fixed 16 KiB window straight into the counted output; its `/Length` is an indirect object written after the stream, so callbacks and pipes still receive forward-only output and no compressed stream is held whole. The font stream keeps `/Length1` equal to the original font size. A page now uses three objects (contents, contents length, page). Every compressed byte counts against output quotas and the xref ceiling. A zlib allocation failure is `KI_CAUSE_RESOURCE`; any other compressor error is `KI_CAUSE_IO` "pdf compression failed", and file destinations are preserved. zlib is a new build and link dependency: Make links it into the CLI, shared library and examples, `pkg-config --static` adds `Libs.private: -lz`, the CMake package calls `find_dependency(ZLIB)` and static targets link `ZLIB::ZLIB`. The public header, `KiOptions`/`KiResult`/`KiEvent` and exports are unchanged.
- Count every emitted PDF byte in one checked place and cap the total at 9,999,999,999 bytes even when `max_output_bytes` is 0, so no classic xref offset or `startxref` needs an eleventh digit. Crossing the ceiling fails with `KI_CAUSE_RESOURCE` before the bytes reach the destination. `output_bytes` now also includes bytes flushed while a failed conversion closes its stream. An output quota equal to the PDF size succeeds and one byte less fails, including during xref/trailer finalization.
- Name the embedded font in `BaseFont`/`FontName` by its own PostScript name (name ID 6) instead of a fixed `DejaVuSans`, escaping PDF delimiters, whitespace and `#` as `#XX`. A font without a usable 1–63 character printable-ASCII name gets a deterministic `KiFont-XXXXXXXX` name from a hash of its bytes; a `name` table whose header or records lie outside the table, or a truncated `maxp`, is rejected with `KI_CAUSE_FONT`.
- Treat a glyph with zero advance as present: combining marks such as U+0301, U+0306 and U+0483 in DejaVu Sans now render with width 0 and extract through ToUnicode/ActualText, instead of failing as missing. A GID of 0 or at/past `maxp` numGlyphs is still a font error. Each GID maps back to a single code point. No shaping, composition or new repertoire.
- A line longer than 1023 UTF-8 bytes that cannot wrap (a long run of zero-advance marks) now fails with exit 4 and `KI_CAUSE_RESOURCE` instead of an XML error. Page streams are sized for 50 such full lines.

- Reject TTF files whose cmap subtable header, encoding records, table directory or table spans are truncated or out of range with `KI_CAUSE_FONT`, without reading past the font data. All range checks avoid offset-plus-length overflow. The `hmtx` table must hold all `numberOfHMetrics` entries. The 8 MiB font size cap is unchanged.
- Validate every consumed scalar as an XML `Char` and well-formed UTF-8 in text, attributes, CDATA, comments, PIs, the skipped DTD, skipped records and content after the root. A literal U+FFFE/U+FFFF is now `KI_CAUSE_XML` instead of a font error or success, and text events never end inside a UTF-8 sequence. Accept `xml-stylesheet` and other non-`xml` PI targets; reject empty or non-name PI targets and `&#X...;`. Require ordered, unique, space-separated XML declaration pseudo-attributes, a single DOCTYPE with white space after the keyword, and skip its internal subset with comment/PI/quote awareness. External DTDs and entities are still never loaded or expanded; parser limits are unchanged.

## 0.2.0

- Initialize `KiResult` before callback option validation, so reused results never retain earlier metrics on a usage error.
- Add CMake out-of-tree builds, CTest, installed `ki_xml_pdf::ki_xml_pdf`/static/shared targets and pkg-config discovery for Make and CMake installations.
- Accept `-` for CLI stdin/stdout, `-h` for help and `--` for paths beginning with a hyphen. Stream failures return a failure status; stdout may contain a partial PDF.
- Add `ki_convert_to_file` for callback input with the existing atomic file publication, quotas and cancellation contract. Preserve `KiOptions` layout and all existing exports.
- Provide a bounded public catalog mapper with English output, one-byte callback reads, independent text oracles and grammar diagnostics.
- Add English onboarding, contribution/security guidance and report-v1 documentation; retain Russian onboarding. Existing report-v1 labels and selection/whitespace rules are unchanged.
- Derive CLI/build/package/source archive versions from the public header, archive the exact Git HEAD and retain source commit/checksum provenance after the complete CI gates. A version tag or GitHub Release remains a separate action.

## 0.1.0

- Потоковый профиль records-v1, шрифт DejaVu, атомарная публикация PDF.
- Публичный вызов `ki_convert_file` с квотами, отменой и сменным mapping.
- Лицензия исходников — MIT (`LICENSE`). Профиль `report-v1`: `--profile report-v1`, выборка `--record-offset`/`--record-limit`. `KiOptions` инициализируется `ki_options_init`; несовпадение `size` — ошибка вызова.
- Символ вне репертуара шрифта — `KI_CAUSE_FONT` и `U+` код и в `records-v1`, и в `report-v1`. Ошибка профиля называет правило и смещение в байтах (`KiEvent.byte_offset`, включая конец самозакрывающегося элемента). Отмена во время `fsync` не публикует PDF. Смена `PREFIX` пересобирает путь шрифта.
- GitHub CI явно устанавливает Poppler/Python для sanitizer tests и сохраняет проверенный source archive с SHA-256 и commit. Добавлены инструкции установки и публичного API; измерения привязаны к публичной Git-ревизии.
