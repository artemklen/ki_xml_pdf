# Background

ki_xml_pdf's shape comes from a handful of recorded decisions, not from convention. This section holds the reasoning behind the own streaming pipeline, the bounded-memory rule and the release scope, and the danger zones that follow from them.

## Pages

| Page | Contents |
| --- | --- |
| [Design decisions](design-decisions.md) | Own pipeline instead of Apache FOP, offsets spooled to disk, fixed object numbering, report-v1, MIT, Linux/glibc only, atomic publication, selection that still validates, no font fallback |
| [Pitfalls](pitfalls.md) | Eleven ways to get bitten: partial stdout, PREFIX mismatch, `KiOptions.size`, borrowed event pointers, short reads, process-global test hooks, fragment whitespace, `xml:space` differences, quota zero values, the header version, `make dist` and HEAD |

## Where the decisions live

Three decisions are recorded in Russian in the repository: `docs/contract.md` (KW-1004, the pipeline contract and measurements), `docs/profile-decision.md` (KW-1021, the report-v1 profile) and `docs/license-decision.md` (KW-1024, MIT). [Design decisions](design-decisions.md) summarizes them in English and points at the code that implements each one. Task identifiers such as KW-1004 and OSR-7 come from the owner's tracker and also appear in commit messages.

## Where to go next

- For what is unsafe input and how it is handled, see [Security](../security.md).
- For project history and measurement provenance, see [Lore](../lore.md).
- For what could be removed or simplified next, see [Cleanup opportunities](../cleanup-opportunities.md).
