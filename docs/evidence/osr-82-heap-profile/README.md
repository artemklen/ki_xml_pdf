# OSR-82 heap profile and peak RSS

Library sources are unchanged. The binary was built from `de8ba0eeb39c9f35cb9dff2ecb576b689efd0fd4` with an ordinary `make xml2pdf libki_xml_pdf.so` (no ASan, no UBSan). This directory is the only addition.

| Item | Value |
| --- | --- |
| Revision measured | `de8ba0eeb39c9f35cb9dff2ecb576b689efd0fd4` |
| Compiler | gcc (Debian 14.2.0-19) 14.2.0 |
| CFLAGS | `-std=c99 -Wall -Wextra -Wpedantic -O2 -g -fPIC -D_POSIX_C_SOURCE=200809L` |
| Valgrind | valgrind-3.24.0, `--tool=massif --pages-as-heap=no` |
| RSS | `/usr/bin/time -f %M` (GNU time), maximum resident set in KiB |

## Pinned input

```sh
python3 tests/resource_recipe.py --profile report-v1 --target-bytes 16777216 --out r16-report.xml
```

The recipe finishes the last record, so the file is 16777245 bytes, not 16777216. A second generation compared equal (`cmp`). SHA-256:

```text
2cc851a4f8f1514241e9f3a45acc90eed99a6b9c5b97bae4793b4e9b90186d84  r16-report.xml
```

The records-v1 file from the same target is 16777233 bytes:

```text
76eb12d9bdc6bf3602ba4bd602f1da574855148e39bc504ea71fe7fc50de3446  r16-records.xml
```

The XML files are not committed. The recipe and these digests reproduce them. 246368 report records, 499983 records-v1 records.

## Command

```sh
./xml2pdf --font fonts/DejaVuSans.ttf --profile PROFILE --max-pages 0 --max-output-bytes 0 [extra] INPUT OUTPUT
```

Two ordinary runs of `report-v1` on `r16-report.xml` both exited 0 and wrote the same PDF:

```text
734eb23f37255da45de6b7e2733697cc2c83f9e3b4fc6d39b1648bb5374004d5
```

stderr on success is one line, `xml2pdf: pages=9855 objects=29573 offset_bytes=236592`.

## RSS, median of five

| Scenario | Five samples, KiB | Median KiB | Massif peak heap, bytes | Allocator extra, bytes |
| --- | --- | --- | --- | --- |
| report-v1, full 16 MiB | 2656 2728 2720 2832 2924 | 2728 | 1534120 | 240 |
| records-v1, full 16 MiB | 2640 2640 2832 2688 2792 | 2688 | 1534121 | 239 |
| report-v1, `--record-limit 1000` on the same 16 MiB file | 2728 2640 2704 2784 2784 | 2728 | 1534130 | 230 |

Useful heap plus allocator extra is 1534360 bytes in every scenario. For report-v1 the median RSS is 2728 × 1024 = 2793472 bytes, so the heap is 1534360 / 2793472 = 54.9% of RSS. The other 1259112 bytes (1230 KiB) are not heap: the `xml2pdf` text and data (71240 + 1048 bytes from `size -d`), mapped libc and libz, and stacks. Massif recorded `mem_stacks_B=0` at the peak snapshot.

Selection and profile do not move the peak. The page buffer is sized for the worst case of 50 lines, and the font is loaded before any record is emitted.

## Peak moment

The peak snapshot is the first page flush, after `pdf_begin_cfg` has returned. The new live allocation is `pdf_add_page` (`src/pdf_write.c:773`), called from `flush_page` (`src/layout.c:76`) and `commit_line` (`src/layout.c:92`). The font bytes and the deflate state created inside `pdf_begin_cfg` / `write_font_objects` are still allocated, so the peak is that later page, not the interior of `pdf_begin_cfg` alone.

`Layout lay` is a stack local in `convert_streams` (`src/convert.c:551`). `Layout.page[50][1024]` is 51200 bytes of that object. It is not a separate malloc in the heap tree.

## Top heap allocations at the report-v1 peak

The named blocks match in the other two peaks: 759720, 416256, four 65536-byte zlib blocks, 51250 and 16384. The below-threshold remainder is 28366 bytes in 14 places for report-v1, 28367 for records-v1 and 28376 for `--record-limit 1000`. That remainder is the whole useful-heap gap (1534120, 1534121, 1534130). records-v1 reaches `pdf_add_page` and `flush_page` through `map_event` (`src/convert.c:83`) and `convert_streams` at line 672. report-v1 and the selection path call `convert_streams` at line 670.

| Bytes | Where | What |
| --- | --- | --- |
| 759720 | `font_load` `src/font.c:342`, called from `pdf_begin_cfg` `src/pdf_write.c:702` | The whole `fonts/DejaVuSans.ttf` (file size 759720) |
| 416256 | `pdf_add_page` `src/pdf_write.c:773` | Worst-case page stream buffer: `256 + 50 * (8 * 1024 + 128)` |
| 262144 | four `deflateInit2_` blocks via `flate_ready` `src/pdf_write.c:116`, first used from `write_font_objects` `src/pdf_write.c:338` | zlib state, windowBits 15, memLevel 8. Still live after the font stream |
| 51250 | `flush_page` `src/layout.c:66` | Copy of the finished page (`50 * 1025`) |
| 16384 | `flate_ready` `src/pdf_write.c:109` | `zbuf` (`FLATE_CHUNK`) |
| 28366 | 14 sites under massif's 1% threshold | not separated |

759720 + 416256 + 262144 + 51250 + 16384 + 28366 = 1534120.

## Verdicts

* Шрифт: подтверждена. Вклад 759720 байт в пике всех трёх сценариев. Буфер живёт от `font_load` до освобождения writer.
* ToUnicode: опровергнута как часть пика. `write_tounicode` делает `malloc(262144)` (`src/pdf_write.c:203`) и `free` на каждом выходе, включая успех (`src/pdf_write.c:302`). В пиковом снимке этих байт нет.
* Буферы страницы: подтверждена. В пике 416256 байт (`pdf_add_page`) и 51250 байт (копия `flush_page`). Поле `Layout.page[50][1024]` — 51200 байт стекового `Layout`, не отдельный malloc пика.

The epic's guess that the peak is `pdf_begin_cfg` (TTF + zlib + ToUnicode) is only partly right. TTF and the zlib state are created there and are still live, ToUnicode is not, and the maximum is the first `pdf_add_page` on top of those survivors.

## Raw files

`report-v1.massif`, `records-v1.massif`, `report-v1-limit1000.massif`, and the `ms_print` text of each. `SHA256SUMS` covers those files. RSS samples are `*-rss.txt`.
