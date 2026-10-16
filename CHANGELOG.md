# Changelog

Версии до 1.0 не обещают стабильный ABI. Совместимость ломается вместе с номером 0.y.

## Unreleased

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
