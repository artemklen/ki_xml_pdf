# Как сообщать об ошибках

Собственный код и примеры распространяются по [MIT](LICENSE). Уведомление о шрифте — `fonts/DejaVuSans.copyright`. [Публичный репозиторий](https://github.com/artemklen/ki_xml_pdf) принимает issues и pull requests; готовность конкретного commit проверяется в GitHub Actions.

Обычную ошибку можно сообщить через [GitHub Issues](https://github.com/artemklen/ki_xml_pdf/issues). Проблемы безопасности отправляйте Artem Klen на artem.klen@icloud.com. Нужны: версия из `xml2pdf --version`, commit или source archive checksum, команда, код выхода и короткий пример XML, который можно распространять. Не прикладывайте чужие документы.

Сборка и проверки описаны в `README.md`. Для изменения поведения приложите meaningful regression и результаты соответствующих checks. RSS измеряется на обычном бинарнике, отдельно от ASan/UBSan. Контракт [report-v1](docs/report-v1.md) и квоты должны сохраняться; изменения схемы и публичного API отражаются в документации и `CHANGELOG.md`.
