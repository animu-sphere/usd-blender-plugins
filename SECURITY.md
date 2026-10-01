# Security policy

## Supported development line

Security fixes currently target the default branch, `main`. No maintained
release series is declared yet. This is an early implementation, not a
security certification; platform verification is recorded in the
[build guide](docs/guides/building.md).

## Reporting a vulnerability

Do not publish vulnerability details, proof-of-concept files or private assets
in issues or pull requests before coordinated disclosure.

Use the repository's GitHub **Security > Advisories > Report a vulnerability**
option when available. If private vulnerability reporting is unavailable,
contact a maintainer through a private contact method listed on their GitHub
profile. If no private contact is listed, open an issue only to request a
private reporting channel, without describing the vulnerability or attaching
files. This document does not imply that GitHub private reporting is enabled.

Include the affected commit or version, platform and toolchain, reproduction
steps, observed impact, and a minimal sample you are authorized to share.
Identify whether the problem is in the native reader, the OpenUSD plugin or
a dependency. Remove unrelated content and credentials from samples.

Maintainers will assess the report and coordinate a fix and disclosure with
the reporter. No response-time guarantee is currently offered.

## Untrusted files

Treat `.blend` files as untrusted binary input. Report crashes, out-of-bounds
access, excessive resource use, and violations of decompression budgets as
potential vulnerabilities. The current reader validates headers only;
successful opening does not validate the rest of a file or certify it safe.

The native file format does not execute embedded scripts or link Blender.
When using Blender as a fixture oracle, retain `--factory-startup` and
`--disable-autoexec` as shown in the build guide. These flags are not a
sandbox; use process isolation and resource limits appropriate to the input.
The planned host-backend requirements are defined in the
[backend policy](docs/design/BACKEND_POLICY.md), not implemented guarantees.