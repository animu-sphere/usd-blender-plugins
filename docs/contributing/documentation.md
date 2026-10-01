# Documentation guidelines

Documentation is part of the implementation contract. A change is incomplete if
it changes a public boundary, the authored stage, implemented architecture, or
delivery status without updating the page that owns it.

## Category ownership

| Category | Put this here | Not this |
| --- | --- | --- |
| `architecture/` | Component identities, dependency edges, layout, build modes, external dependencies — the binding structural contract, with what exists marked as such. | Rationale; unimplemented features described as present. |
| `design/` | Intended contracts, their rationale, and open questions. | Claims that something is implemented. |
| `reference/` | Facts about the current tree: capabilities, diagnostics. | Plans, except in a column that is clearly labelled as intended. |
| `roadmap/` | Phase status and release mapping; incomplete, ordered tasks and their completion criteria. | Completed task detail; rationale. |
| `guides/` | How to accomplish a task, with commands that have been run. | Commands nobody has run. |
| `releases/` | One immutable record per released version. | Work in progress. |
| `reports/` | Dated evidence from real runs; append-only. | Current-state claims. |
| `contributing/` | How to maintain this repository. | End-user tasks. |

`guides/`, `releases/` and `reports/` are created when they have real content —
the first build guide with Phase 0, the first release record with the first
tag, the first report with the first dated run.

## Status rules

- A design document carries `proposed`, `accepted`, `superseded` or
  `rejected`. A section that has been implemented and fixture-tested is
  binding; changing it afterwards is a contract change
  ([STAGE_CONTRACT.md §2](../design/STAGE_CONTRACT.md#2-contract-version)).
- Phase status appears only in the roadmap status table; task status appears
  only in the current task tables. Use ✅ done, 🚧 in progress, ⬜ not started,
  or ⛔ blocked. Do not repeat those states in summaries or phase headings.
- The capability matrix uses its own vocabulary — supported, approximated,
  preserved, unsupported, unverified, and `—` for nothing implemented — and
  never says "supported" without a fixture.
- A release record, and a dated report, is not rewritten. A later finding gets
  a new report and a one-line forward-note at the top of the old one.

## Stable numbering

Design and architecture documents number their sections, and a number never
changes meaning, so other documents can cite "stage contract §6". A revision
adds subsections or appends sections; it does not renumber. Open questions are
identified by prefix and number (`STAGE-O1`, `BLEND-O2`, `MAT-O1`, `NAME-O1`,
`BACK-O1`, `DEP-O1`, …), never reused.

## One source of truth per fact

- Phase status and which release carries a Phase: the
  [roadmap status table](../roadmap/README.md#status-at-a-glance), nowhere else.
- Incomplete tasks and their status:
  [current.md](../roadmap/current.md), nowhere else.
- What a Phase contains: [DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases).
- Structure and dependency edges:
  [WORKSPACE.md](../architecture/WORKSPACE.md), changed first and alone.
- What is implemented:
  [CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md).
- Which open questions exist, and what blocks on them: the owning document;
  the [roadmap](../roadmap/README.md#open-decisions) only schedules them.

Link to the owner instead of restating it. When a summary disagrees with the
implementation, the implementation wins and the summary is a bug.

README files, design and architecture documents, contributing instructions,
and guides must not carry live progress summaries or CI pass/pending status.
Link to the roadmap for delivery status and to the capability matrix for
support claims. Design acceptance status is a separate decision, not phase
progress. Guides describe procedures; dated verification evidence belongs in
reports, or is explicitly scoped to a dated run rather than phrased as current
status. Changelog entries record delivered changes, not outstanding work.

## Language and form

- Repository documents are in English.
- Relative links for everything in the repository; code spans for commands,
  paths, targets, schema and attribute names, and diagnostic codes.
- Keep each category's index (`docs/README.md`, `roadmap/README.md`) in sync
  with its files.
- Never commit machine-local paths; write `$HOME` or `%USERPROFILE%`.
- Never commit a `.blend`, texture or render from elsewhere, or a screenshot of
  one, unless its license explicitly allows redistribution. Fixtures are
  written by this repository's own scripts.

## Change checklist

1. Planned behavior is not presented as implemented.
2. Every new page appears in its category index.
3. Relative links and heading anchors resolve.
4. Implementation changes update `architecture/` and `reference/`.
5. Completed work leaves `roadmap/`.
6. A departure from a design document is recorded in that document, not only
   in the code or the pull request.
7. Live phase, task and capability status is updated only in its owning table;
  other pages link there instead of repeating it. Do not replace an obsolete
  status banner with a newer duplicate.
