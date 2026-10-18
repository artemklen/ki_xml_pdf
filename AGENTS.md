# AGENTS.md — ki_xml_pdf

- Repo: `github.com/artemklen/ki_xml_pdf`, default `main`; для эпика OPS-625 ветка `ops-625`.
- Локальный pre-send gate: [CONTRIBUTING.md](CONTRIBUTING.md), раздел «Local pre-send gate (OPS-625)». Выполнять на exact SHA и записывать команды/результаты/окружение.
- CI (`.github/workflows/ci.yml`) — compile + source artifacts only: `make xml2pdf example libki_xml_pdf.so` и `make dist`/SHA-256. Tests, sanitizers, resources и distcheck в CI не запускаются.
- Tag release workflow (`release.yml`) сохраняется; при удалении jobs не оставлять `needs` на них.
- Недоступная Linux/glibc/Poppler/CMake среда — blocker в Linear, не silent skip и не возврат тестов в CI.
- Тесты и assertions не удалять и не ослаблять; новых тестов ради галочки не добавлять.
- Push/PR после локальной проверки; merge — только после независимого exact-SHA review.
