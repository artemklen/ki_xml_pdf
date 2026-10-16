# report-v1: XML profile contract

The owner's decision of 7 October 2026 was **MIT + report-v1** for the first release. This document defines the profile contract. Select the built-in mapper with `--profile report-v1` or `KiOptions.profile = KI_PROFILE_REPORT`.

The project owns this format for sequential reports from large XML inputs. The permitted original example is [report-v1.xml](../examples/report-v1.xml); the expected text for its complete small report is [report-v1.expected.txt](../examples/report-v1.expected.txt). The example was created for this project, contains no customer data, and is covered by MIT. Other schemas need a public mapping API adapter; see the [mapper guide](mapper-guide.md).

## Grammar

```xml
<?xml version="1.0" encoding="UTF-8"?>
<report version="1" title="Сводка">
  <record id="1">
    <field name="Название">Запись 1</field>
    <field name="Статус">Готово</field>
  </record>
  <record id="2">
    <field name="Название"><![CDATA[Запись 2]]></field>
    <field name="Примечание"></field>
  </record>
</report>
```

| Element | Attributes | Content |
| --- | --- | --- |
| `report` | Required `version="1"` and nonempty `title` | Zero or more `record` elements |
| `record` | Optional `id` | One or more `field` elements |
| `field` | Required nonempty `name`; optional `xml:space` | Text, CDATA, and supported character references; no nested elements |

Input is UTF-8 with one root `report` and no namespaces. Prefixes and `xmlns` declarations are rejected, except for the predefined `xml:space` attribute on `field`. Unknown elements/attributes, another version, invalid nesting, missing required attributes, an empty `record`, and non-whitespace structural text cause a profile error. Its message names the rule and input byte offset; an unknown-attribute error also names the attribute and element. A character outside the font repertoire causes a font error with its `U+` code point rather than an XML error.

Whitespace indentation is allowed between structural elements. Comments and processing instructions are not rendered. Declaration and entity support follows the [core contract](contract.md); external resources are never loaded. The English [README](../README.md#xml-font-and-resource-limits) lists the parser and font limits.

Nonempty `title` and `name` contain at least one non-whitespace character; their significant spaces are preserved. A supplied `id` must also be nonempty. Without `id`, the original record number is used, starting at 1. IDs need not be unique: retaining a growing set of every ID is outside the streaming contract. Duplicate field names are allowed, and source order is preserved.

`title`, `id`, and `name` follow the parser's decoded-attribute limit of 255 UTF-8 bytes, not 255 characters. Overflow is an error. Field text is delivered in streaming fragments; a complete field, record, or document is never buffered. Output quotas, rather than a fixed field buffer, limit the value length.

## Rendering and whitespace

The report title is printed once. Each selected record is a sequential block headed by the literal Russian label `Запись ` followed by its ID or original record number. Fields appear as `name: value`; an empty value appears as `name:`. XML element names and the `version` attribute do not become report content. The Russian labels are part of this existing profile and remain unchanged.

All spaces within a field value are significant: leading/trailing spaces must not be trimmed, and a space must not be lost at a TEXT/CDATA boundary. Both `xml:space="default"` and `xml:space="preserve"` are allowed and preserve the full field value because this profile does not remove indentation inside a field. Other `xml:space` values are errors. Core XML normalization applies to literal CR/CRLF and character references; changing the read-fragment size does not change the result.

Physical line and page breaks follow the layout rules: Letter paper, 40 pt margins, DejaVu Sans at 10 pt, and 14 pt line spacing. Completeness is checked by logical text and value order; significant-whitespace tests must not normalize away a loss of spaces. Extra blank lines between blocks are allowed. Characters that the selected font/core cannot render cause an explicit error instead of silently dropping selected data.

The report ends with the exact labels `Всего записей: N`, `Выбрано записей: S`, and `Пропущено записей: N-S`. The application receives the same counters through `KiResult.report_total`, `KiResult.report_selected`, and `KiResult.report_skipped`. Counter overflow is a resource error. An empty `report` renders its title and three zero counters.

## Explicit selection from a large input

`record_offset` is the nonnegative number of initial records to skip (0 starts at the beginning). `record_limit` is the nonnegative maximum number of records to select after that offset (0 selects all remaining records). Selection follows source order, not IDs. An offset beyond the end selects no records.

Without explicit selection, all records are rendered. Long values are not silently skipped, and conversion does not stop at an arbitrary page count. Even after reaching the selection limit, the entire XML is read and validated, including the grammar of skipped records. A late failure must not publish a successful file result. State depends on current depth, fragment, and page rather than the number of records or field length.

Select the profile explicitly in the CLI with `--profile report-v1`, and selection with `--record-offset`/`--record-limit`; the corresponding fields are in `KiOptions`. `records-v1` remains the default profile. The API example is [examples/report.c](../examples/report.c).

`ki_convert_file` and `ki_convert_to_file` preserve the previous destination on a failed conversion. `ki_convert` and CLI stdout can leave already accepted PDF bytes on failure. Short positive reads are not EOF, and choosing stdout does not relax validation of skipped records.

## Acceptance evidence for KW-1022

1. The complete small example matches its expected text. Cover an empty report, missing ID, empty/repeated fields, significant whitespace, and TEXT/CDATA/reference fragments.
2. Negative schema cases produce diagnosed errors: unknown attributes/elements/version, namespaces, missing title/name, empty records, nested field content, invalid `xml:space`, and a late failure after the selected portion. XML/core, font, quota, and mapping errors must not be reported as success.
3. Cover full conversion without selection, offset/limit, offset beyond EOF, and selection from the middle/end. Validate total/selected/skipped counters and values against an independent input oracle.
4. On the project's synthetic 1 GiB XML, selecting the first 1000 records: peak converter RSS ≤64 MiB, PDF ≤10,000 pages and ≤256 MiB, and metadata spool ≤64 MiB. The generator explicitly uses short selected records and large skipped data; these budgets are not promised for arbitrary sets of 1000 records of any length.
5. First/middle/last pages are read by an independent PDF reader under a separate 1 GiB address-space limit. Reader RSS and RLIMIT_AS are distinct measurements. Evidence records the commit, commands, XML/PDF/spool sizes, pages, converter RSS, and reader result.
6. Errors and cancellation preserve an existing file destination; callbacks have an explicit partial-output contract. External C/C++ clients select the profile without changing the parser/writer, and integration checks exercise both CLI and API.

## Historical resource measurement

`python3 tests/large_proof.py 1073741824` on the first public revision [`7c62fe4`](https://github.com/artemklen/ki_xml_pdf/commit/7c62fe4af72b097885d76a7f9f897492f5c4c2d5), using an ordinary build and measuring only the `xml2pdf` process, produced:

| Measure | Value |
| --- | --- |
| XML input | 1,073,741,815 bytes |
| Total records | 19,522,650 |
| Selected records | 1000 |
| Converter peak RSS | 2372 KiB |
| PDF pages | 41 |
| PDF output | 1,009,241 bytes |
| Metadata spool | 720 bytes |
| Conversion time | 7.61 seconds |

The full selected text matched the independent oracle. `pdftotext` read pages 1/21/41 under the separate `ulimit -v 1048576` limit. These measurements belong to that source revision and synthetic workload; the time is not an SLA. No comparison with an unknown former converter or Apache FOP is claimed. The same-machine baseline of the unmodified revision `075455c`, with separate full-output and fixed-1000 selection series, is in [the OSR-23 evidence](evidence/osr-23-baseline-075455c/README.md); it does not replace this historical table. [CI and source artifacts](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) are tied to a specific commit; see the [distribution checklist](release-checklist.md).
