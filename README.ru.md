# ki_xml_pdf

[English README](README.md)

Библиотека на C и CLI для преобразования больших XML в PDF с ограниченной памятью, версия 0.2.0. Профиль [report-v1](docs/report-v1.md) выводит последовательный отчёт с полями и явной выборкой записей. Другие XML-схемы подключаются через callback mapping; [руководство адаптера](docs/mapper-guide.md) показывает собственный пример каталога. Диагностический `records-v1` печатает имена элементов, атрибуты и текст; он используется по умолчанию.

Исходники и собственные примеры распространяются по [MIT](LICENSE). Шрифт DejaVu имеет [отдельное уведомление](fonts/DejaVuSans.copyright). Репозиторий: [artemklen/ki_xml_pdf](https://github.com/artemklen/ki_xml_pdf); результаты проверок: [GitHub Actions](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml).

Исторический замер на ревизии `7c62fe4`: на собственной синтетике около 1 ГиБ с выборкой первых 1000 записей RSS конвертера составила 2372 КиБ, PDF — 41 страницу / 1 009 241 байт. Без выборки сохраняются все записи; размер PDF и время зависят от содержимого. Ревизия, команды, бюджеты и границы этих измерений описаны в [контрактах](docs/contract.md). Текущий baseline на `075455c` — в разделе [«Ресурсный baseline 075455c»](#ресурсный-baseline-075455c).

## Сборка и запуск

Поддерживаемый стенд: Linux 64-bit с glibc, GCC или Clang, GNU Make. Сборка использует GNU/POSIX API и GNU-compatible linker. Работа на других ОС не проверена.

```sh
make
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
```

Выборка большого входа и ограничения результата:

```sh
./xml2pdf --profile report-v1 --record-offset 0 --record-limit 1000 \
  --max-pages 10000 --max-output-bytes 268435456 \
  --max-spool-bytes 67108864 input.xml report.pdf
```

Даже при выборке весь XML дочитывается и валидируется. Без `--record-limit` данные не пропускаются. Файловый API сохраняет прежний PDF при ошибке.

В версии 0.2.0 `-` задаёт stdin или stdout. Например:

```sh
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - report.pdf
./xml2pdf --profile report-v1 examples/report-v1.xml - > report-stdout.pdf
```

Сообщения конвертации идут в stderr. При поздней ошибке stdout может содержать неполный PDF: проверяйте код завершения. `-h` и `--help` возвращают 0; `--` завершает разбор параметров для путей с дефисом. [Английское руководство](README.md) содержит полный набор pipeline-примеров и инструкции CMake/pkg-config для установленной библиотеки.

## Установка и API

```sh
make install PREFIX="$HOME/.local"
cc -I"$HOME/.local/include" examples/report.c \
  "$HOME/.local/lib/libki_xml_pdf.a" -o report-example
./report-example examples/report-v1.xml report.pdf
```

Для shared library линковка использует `-L<prefix>/lib -lki_xml_pdf`; каталог должен быть доступен runtime linker. Устанавливаются публичный заголовок, `.a`, `.so`, font и notices. `PREFIX` задаёт runtime-путь шрифта; `DESTDIR` только стадирует файлы. При переносе данных можно явно задать `KiOptions.font_path` или `KI_XML_PDF_FONT`.

```c
#include "ki_xml_pdf.h"
#include <stdio.h>

int main(void) {
    KiOptions options;
    ki_options_init(&options);
    options.profile = KI_PROFILE_REPORT;
    options.record_limit = 1000;
    options.max_pages = 10000;

    KiResult result;
    int status = ki_convert_file("input.xml", "report.pdf", &options, &result);
    if (status != 0) {
        fprintf(stderr, "%d: %s\n", result.cause, result.message);
    }
    return status;
}
```

`ki_convert_to_file` читает через read callback и атомарно публикует файловый результат, игнорируя write callback. Ресурсы callbacks остаются во владении вызывающего кода. `ki_convert` принимает read/write callbacks; короткое положительное чтение не означает EOF. Уже принятые output callback байты не откатываются при ошибке. Независимые conversion calls могут работать одновременно; библиотека не меняет signal handlers приложения. Примеры: [C](examples/report.c), [C++](examples/consume.cpp); полный публичный контракт — [ki_xml_pdf.h](include/ki_xml_pdf.h).

## Проверки

Для проверок нужны compiler, `python3`, `/usr/bin/time`, `pdfinfo` и `pdftotext` (Poppler). Примеры и install checks также используют C++ compiler. В Ubuntu/Debian: `gcc g++ clang make python3 time poppler-utils`. Эти зависимости нужны и sanitizer jobs. Если reader отсутствует, проверка завершается ошибкой.

```sh
make check
```

`make check` удаляет прежние `xml2pdf` и `test_stream`, заново собирает их из исходников, прогоняет группы `tests/test_stream` и `tests/verify.sh`. Готовые бинарники для этого не нужны.

Отдельно:

```sh
make clean
make test
./tests/verify.sh
make test-san
make distcheck
python3 tests/large_proof.py --self-test
python3 tests/large_proof.py 1073741824
```

`tests/verify.sh` пишет отчёты в свой каталог (`mktemp -d`, либо `SCRATCH`, если он задан). Крупные файлы этого прогона — один XML около 16 МиБ и его PDF; после замера RSS они удаляются. В отчёте остаётся `mem.txt`. RSS в нём — это maximum resident set size процесса `./xml2pdf`, не генератора XML и не разбора PDF.

Ресурсные серии повторяются так (scratch — каталог на диске, не tmpfs, с запасом в несколько ГиБ: один полный PDF `records-v1` 64 МиБ занимает около 713 МБ; каталог evidence — новый):

```sh
python3 tests/resource_baseline_test.py
python3 tests/resource_baseline.py matrix --binary ./xml2pdf --font fonts/DejaVuSans.ttf \
  --source-dir . --evidence-dir "$HOME/ki-evidence/run1" --scratch "$HOME/ki-scratch"
python3 tests/resource_controls.py --binary ./xml2pdf --font fonts/DejaVuSans.ttf \
  --evidence-dir "$HOME/ki-evidence/controls1" --scratch "$HOME/ki-scratch"
python3 tests/resource_baseline.py compare docs/evidence/osr-23-baseline-075455c/baseline \
  "$HOME/ki-evidence/run1"
```

## Ресурсный baseline 075455c

[Baseline OSR-23](docs/evidence/osr-23-baseline-075455c/README.md) снят на неизменённых исходниках 0.2.0 ревизии `075455c`, на одном Linux-стенде со шрифтом DejaVu Sans из поставки. Серий две, и они не подменяют друг друга:

- **Полный вывод**, все записи в PDF: `records-v1` и `report-v1` на 1, 16 и 64 МиБ. Пиковая RSS конвертера — 2340–2524 КиБ. Вход 64 МиБ дал 115 552 страницы / 713 436 448 байт (`records-v1`) и 39 033 страницы / 347 124 499 байт (`report-v1`).
- **Выборка** `report-v1 --record-limit 1000` из входов 16, 256 и 1024 МиБ: весь XML читается и валидируется, в PDF попадают 1000 записей. Пиковая RSS конвертера — 2340–2536 КиБ, каждый раз 41 страница и около 1,1 МБ PDF.

Независимый потоковый oracle сверил каждую выведенную запись с рецептом fixture. Там же лежат сырые CSV/JSON, отдельные замеры `/usr/bin/time` генератора, конвертера и reader, хеши и отрицательные контроли. Размер PDF, файла смещений и время растут вместе с выводимым содержимым; RSS включает загруженный шрифт и зависит от него. Полный вывод для входа 1 ГиБ не измерялся. Строки выборки не описывают полный вывод; сравнения с другими конвертерами нет.

Запуск конвертера:

```sh
./xml2pdf input.xml output.pdf
```

## Пределы парсера

Состояние парсера не растёт вместе с документом. Вход читается окном 4096 байт. Одновременно открыто не больше 64 элементов. Имя — до 63 байт, атрибутов не больше 8, значение атрибута — до 255 байт. Текст отдаётся кусками до 1023 байт.

Разбираются разметка, комментарии, инструкции обработки, объявление XML, один DOCTYPE и секции CDATA внутри элемента, пять предопределённых сущностей и числовые ссылки `&#...;` / `&#x...;` (строчная `x`). Внешние DTD и сущности не загружаются, сущности из внутреннего подмножества не раскрываются. Неизвестная сущность, CDATA вне элемента, `<!FOO>`, `]]>` в тексте, неверное объявление, управляющий байт, NUL и битый UTF-8 отклоняются. Буквальные CR/CRLF становятся LF.

Каждый прочитанный байт должен быть корректным UTF-8 и кодировать XML `Char`, в том числе в комментариях, инструкциях обработки, пропускаемой DTD, записях вне выборки и после корня: буквальный U+FFFE или U+FFFF — ошибка XML, а не шрифта. Объявление XML допустимо только в начале (после необязательного BOM UTF-8); псевдоатрибуты `version="1.0"`, необязательные `encoding="UTF-8"` и `standalone` идут в этом порядке, по одному разу и через пробел. Цель инструкции обработки — имя до 63 байт, за ним пробел или `?>`; цель `xml` в любом регистре зарезервирована, а `xml-stylesheet` и подобные — обычные инструкции, которые не выводятся. DOCTYPE стоит до корня; его внутреннее подмножество пропускается с учётом комментариев, инструкций и кавычек, поэтому `]` или `>` внутри них его не завершают. Пробел между словами сохраняется; отступ с переводом строки на пустой строке — нет, если нет `xml:space="preserve"` и это не CDATA. Это по-прежнему не весь XML 1.0.

## Подтверждённые ограничения

Смещения объектов PDF пишутся во временный файл, а не в растущий массив. На синтетике около 16 МиБ (десятки тысяч страниц) RSS конвертера остаётся около 2–3 МиБ: в неё входит шрифт, а не список страниц. Это не обещание, что любой вход уложится в ту же цифру. Потолок контракта — 64 МиБ RSS на стенде обзора.

Текст набирается DejaVu Sans (файл `fonts/DejaVuSans.ttf`, лицензия рядом). Кириллица, ё/Ё и тире U+2014 читаются `pdftotext`. Символ вне репертуара контракта — ошибка, а не пропуск. Строка переносится по ширине глифа внутри полей 40 пт, а не по 90 байтам.

Результат пишется во временный файл в том же каталоге и заменяет путь назначения через `rename` только после успешного разбора, сборки PDF, `fflush` и `fsync`. Один и тот же inode — тот же путь, жёсткая ссылка или символическая ссылка на исходник — отклоняется до изменения файлов. Символическая ссылка на другой файл при успехе заменяется новым файлом, а не записью сквозь ссылку. При ошибке прежний файл назначения не меняется, временный файл удаляется. После `SIGKILL` незавершённый временный файл может остаться рядом, но это не путь назначения.

Контракт ограничивает RAM конвертера на заявленном стенде. Вёрстка — последовательные текстовые блоки; таблицы, глобальные сортировки и XSL-FO не поддерживаются. Неизвестный XML требует отдельного адаптера. Параметры, typed errors, XML/font limits и quotas описаны в [контракте core](docs/contract.md) и [report-v1](docs/report-v1.md). Куда сообщать об ошибках, написано в [CONTRIBUTING.md](CONTRIBUTING.md).

## Исходный дистрибутив

`make distcheck` создаёт source archive из текущего Git HEAD, распаковывает и устанавливает его, запускает внешние C/C++ clients и проверяет report-v1 по oracle. `make dist` создаёт `ki_xml_pdf-0.2.0.tar.gz` и печатает SHA-256.

После успешных GCC, Clang, sanitizer и resource jobs [CI](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) сохраняет тот же source archive, `SHA256SUMS` и `SOURCE_COMMIT` как artifact. Для его загрузки требуется GitHub login. Формальный GitHub Release и version tag создаются отдельно от проверки исходников. [Чеклист приёмки](docs/release-checklist.md).
