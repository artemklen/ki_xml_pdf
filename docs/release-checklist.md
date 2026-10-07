# Чеклист исходного архива 0.1.0

Владелец выбрал MIT и report-v1; оба реализованы. Исходники доступны в [GitHub](https://github.com/artemklen/ki_xml_pdf). Приёмка дистрибутива относится к конкретному commit и artifact, а не ко всем будущим изменениям.

| Ворота | Проверка |
| --- | --- |
| Лицензия и assets | `LICENSE` — MIT; DejaVu notice отдельно. Оба файла присутствуют в archive и installed data dir |
| Полезный профиль | CLI/API report-v1 совпадают с `examples/report-v1.expected.txt`; schema negatives, selection/fullness и late failure проверены |
| Внешнее использование | `make distcheck`, `tests/install_smoke.sh`: installed static/shared C/C++ clients запускаются из чужого cwd; `tests/prefix_switch.sh` проверяет смену PREFIX |
| Ошибки, квоты, отмена | `make test`: causes, code points, byte offsets, page/output/spool ceilings, fsync cancellation и сохранение previous PDF |
| Memory safety | Clean GCC/Clang `make test-san`; ASan/UBSan не используют RSS-assertions instrumented runtime. Нужны Poppler и Python, как и functional tests |
| CI | [Workflow ci](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml): GCC, Clang, ASan, ordinary resource gate, fuzz smoke и negative controls distcheck/proof |
| Большой вход | `python3 tests/large_proof.py 1073741824`: converter RSS≤64 МиБ, PDF≤10000 pages/256 МиБ, metadata spool≤64 МиБ, все выбранные значения/counters и reader под RLIMIT_AS1 ГиБ |

`make dist` создаёт archive текущего Git HEAD и SHA-256. Незакоммиченные изменения не входят в него. `make distcheck` распаковывает этот archive в own mktemp directory и выполняет installation/consumer/oracle checks; обязательная ошибка возвращает failure.

Job `source` запускается после успешных четырёх jobs. Artifact содержит `ki_xml_pdf-0.1.0.tar.gz`, `SHA256SUMS` и `SOURCE_COMMIT`. Эти файлы позволяют проверить и воспроизвести принятый source snapshot; загрузка CI artifact требует GitHub login. Ручной `workflow_dispatch` с `large=true` также проверяет 128 МиБ на GitHub runner. Замер около 1 ГиБ и команды описаны в [контракте core](contract.md) и [report-v1](report-v1.md).

Полная приёмка (KW-1023/KW-1025) сопоставляет результаты с точным source commit/archive checksum. Формальный version tag или GitHub Release не создаётся автоматически от успешного CI или закрытия задач.
