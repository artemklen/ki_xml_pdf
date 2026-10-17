# Contributing
Active contributors: Artem

Contributions should preserve the bounded-state conversion contract and come with reproducible evidence. `CONTRIBUTING.md` is the project policy; this section maps that policy to the implementation and checks at version 0.3.0.

## Choose the right reporting route

| Topic | Route | Include |
| --- | --- | --- |
| Ordinary functional bug | Public GitHub issue | Version, commit or archive SHA-256, OS/architecture, compiler, static/shared linkage, minimal command/API call, status and diagnostic |
| Memory regression | Memory-regression issue template | Ordinary converter RSS, input size, quotas, measurement command and revision |
| Security issue | Private email to artem.klen@icloud.com | Affected revision, impact, reproduction and smallest shareable input |
| Conduct concern | Private email to artem.klen@icloud.com | Relevant details under the Contributor Covenant 2.1 policy |

Use synthetic inputs whenever real documents contain customer or private data.
Do not upload secrets or documents you lack permission to share.
Label partial callback/stdout PDFs with their failure status, not as successful output.
The current security policy supports only the latest 0.y release, currently 0.3.0.

## Before implementation

1. Read [Architecture](../overview/architecture.md) and the owning [system](../systems/index.md).
2. Reproduce the observable failure with a small input.
3. Read the public contract in `include/ki_xml_pdf.h`, plus the affected profile or mapper guide.
4. Agree intentional schema/API changes with the maintainer; do not silently broaden supported platforms or formats.

The supported platform is **64-bit Linux with glibc**.
Ports require a prior maintainer decision, not merely a successful local build.
New UTF-16 input, tables, font addons and internal subsetting are not existing product features.

## Review checklist

- Parser, mapper and page state stay bounded independently of document length.
- Selection still validates skipped records and the complete input.
- File failures preserve the previous destination and remove handled temporaries.
- Callback/stdout failures retain their explicit partial-output contract.
- Caller callbacks and resources remain caller-owned; the library installs no signal handlers.
- Significant text survives short reads, TEXT/CDATA/reference boundaries and wrapping.
- Changes to geometry consider glyph ink, not just advance widths.
- Public ABI changes consider struct sizes, ELF exports and installed C/C++ consumers.
- Version 0.3 preserves the 0.2 `KiOptions`, `KiResult` and `KiEvent` layouts; additions live in `KiConfig` and `KI_0_3`.

## Evidence for a pull request

Add a regression that fails before the fix and checks an observable result.
Use an independent PDF reader and, for visual bounds, a raster oracle.
Record check commands, results, source revision and any skipped check with its reason.

Use [Testing](testing.md) to choose functional, sanitizer, resource and packaging checks.
Measure RSS on an ordinary converter separately from sanitizers, generators and readers.
Do not turn historical measurements or synthetic success into adoption or comparative-performance claims.

Update `CHANGELOG.md` and the relevant contract for intentional behavior changes.
Keep the header-derived CLI, CMake, pkg-config and archive versions consistent.
A merged PR and green CI do not create a release tag; see [Distribution and releases](../deployment.md).

## Key source files

| File | Role |
| --- | --- |
| `CONTRIBUTING.md` | Contribution and evidence policy |
| `SECURITY.md` | Supported versions and private reporting |
| `CODE_OF_CONDUCT.md` | Contributor Covenant 2.1 and enforcement contact |
| `.github/ISSUE_TEMPLATE/bug_report.yml` | Functional-bug intake |
| `.github/ISSUE_TEMPLATE/memory_regression.yml` | Resource-regression intake |
| `.github/pull_request_template.md` | Review evidence checklist |

Related: [Development workflow](development-workflow.md), [Debugging](debugging.md), [Patterns and conventions](patterns-and-conventions.md).
