# Contributing

Use the [current roadmap](docs/roadmap/current.md) to choose work and the
[capability matrix](docs/reference/CAPABILITY_MATRIX.md) to distinguish
implemented behavior from plans. Discuss changes to scope or public contracts
in an issue before implementing them. Keep pull requests focused on one task.

## Development and validation

Follow [Building and testing](docs/guides/building.md) for prerequisites and
verified commands. Windows is currently verified; Linux CI remains pending.

- Reader changes: build and run the reader CTests, including the include and
  generated-link boundary gates. The plugin tests do not run reader CTests.
- Plugin or stage changes: build the bundle, run `ost plugin doctor`, the
  L0-L5 tests and the stage-contract tests described in the build guide.
- Fixture changes: run the corresponding generator checks. Blender's
  `--check` validates scene contents without rewriting the committed fixture;
  byte-identical regeneration remains unresolved.
- Documentation changes: check relative links and heading anchors, and follow
  the [documentation guidelines](docs/contributing/documentation.md).

Add a regression test for a behavior change or bug fix. Use the root
`.clang-format` for touched C++ code, and avoid unrelated formatting changes.
Report the commands run, their results and any unverified platforms in the
pull request; do not describe a skipped check as passing.

## Contracts and dependencies

The [workspace contract](docs/architecture/WORKSPACE.md) owns component
identities and dependency directions. Structural changes go there first, in
their own pull request. The [design policy](docs/design/DESIGN_POLICY.md) and
its focused contracts define intended behavior, not current capabilities.

Keep `blendFile` and the planned `blendScene` independent of OpenUSD. Never
link Blender libraries, copy Blender source code or distribute a Blender
binary. The native reader is independently implemented; Blender is a separate
process, as required by the [backend policy](docs/design/BACKEND_POLICY.md).
Review new dependency licenses and update
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) when vendoring code.

Fixtures must come from this repository's generators or have explicit
redistribution permission. Do not submit confidential assets. Record the
provenance and permission for contributed corpus files; see the
[documentation guidelines](docs/contributing/documentation.md).

## Pull requests

Describe the problem, the chosen behavior and the verification evidence.
Update the owning documentation with changes to public boundaries,
capabilities or delivery status, and add notable changes to
[CHANGELOG.md](CHANGELOG.md). Remove completed tasks from the current roadmap.
Do not present unimplemented behavior as supported.

Contributions are submitted under [Apache-2.0](LICENSE), unless explicitly
stated otherwise, as specified by section 5 of that license. Preserve existing
third-party license notices.

Follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report vulnerabilities
privately using [SECURITY.md](SECURITY.md), not in a public issue or pull request.