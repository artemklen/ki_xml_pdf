# Security policy

## Supported versions

Only the latest 0.y version is supported. The current version is 0.3.0. Older 0.y versions are not supported. Before 1.0, a new minor version may change the ABI.

| Version | Supported |
| --- | --- |
| 0.3.0 (latest 0.y) | yes |
| older 0.y | no |

## Reporting a vulnerability

Send security reports directly to Artem Klen at **artem.klen@icloud.com**. Include the affected version/commit, impact, reproduction steps, and the smallest shareable input. Use this private route for security details rather than opening a public issue with them. Do not send private source documents or secrets.

## What counts as a vulnerability

- Parsing untrusted XML or an untrusted TrueType font, when that input escapes the library's checks (memory safety, unexpected code execution, or an information leak).
- Breaking a documented resource limit: converter memory, the page quota, or the output-byte quota, on an input that asked for those limits.

An ordinary functional bug belongs in a public issue. See [CONTRIBUTING.md](CONTRIBUTING.md).
