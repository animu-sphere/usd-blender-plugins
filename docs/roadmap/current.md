# Current milestones

Upcoming milestones, broken into incomplete tasks. A task leaves this page
when it lands; a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in and its status are in the
[status table](README.md#status-at-a-glance).

Current compatibility work is limited to Blender 5.x under the
[version policy](../design/BLEND_CONTRACT.md#9-version-support).
Existing older-version decoders, fixtures and regression tests remain, but
extending their coverage does not block the current milestones. Full
older-version compatibility is deferred to Phase 8 below.

## Phase 3 — materials and images

Treat Preview Surface as a compatibility realization, not a complete Blender
shader-graph translation. Keep the existing standard-node hierarchy and
supported texture/normal subset and preserve existing material bindings and
diagnostics; prioritize basic opacity and emission.
Unsupported graphs and socket conversions remain explicit, diagnosed
fallbacks rather than blockers for broader graph fidelity. A later MaterialX
path may address richer graphs without requiring changes to this Preview
Surface contract.

Reuse existing Blender-written fixtures and oracle evidence where they cover
the behavior. Prefer synthetic IR, targeted mutations or session-layer
variations for authoring and regression cases; add or extend a small
Blender-written fixture only when source storage or semantics need independent
evidence. This does not replace fixture-backed capability claims or renderer
and hosted acceptance with synthetic tests.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 3.1 | [External image and normal-map compatibility](../design/MATERIAL_POLICY.md#5-supported-node-subset) | confirm the documented supported Image Texture/UV Map/Normal Map paths and diagnosed fallbacks against existing Blender 5.x oracle and registered-plugin tests; retain the sRGB image-alpha warning rather than claiming exact equivalence | 🚧 |
| 3.2 | [Alpha and Emission mapping](../design/MATERIAL_POLICY.md#9-open-questions) | resolve MAT-O1/MAT-O4 for the maintained version scope; support constant opacity and emission plus supported Image Alpha → opacity, diagnose unsupported linked cases, and verify agreed opacity/threshold/emissive inputs with focused source and render evidence while preserving constants and bindings | 🚧 |
| 3.3 | [Color attributes](../design/STAGE_CONTRACT.md#18-open-questions) | resolve STAGE-O3, choose and fixture-test color domains/types and active-render `displayColor` mapping, and update the capability matrix only for proven scope | ⬜ |
| 3.4 | [Generic-renderer acceptance](../design/DESIGN_POLICY.md#14-phases) | verify base color, textures, alpha and normal maps in a generic renderer, every binding under `/Asset/mtl`, both build modes and hosted Windows/Linux gates; report approximations without claiming pixel equivalence | 🚧 |

3.1 has a bounded [native texture boundary](../design/MATERIAL_POLICY.md#72-native-external-texture-boundary)
and a [Blender 5.2.2 oracle](../../tests/fixtures/native-textures/README.md).
Color-to-Base-Color, Alpha-to-scalar, UV Map selection and strength-one OpenGL
tangent Normal Map networks have local native, authoring and registered-plugin
evidence. Color-to-scalar and Alpha-to-color coercions remain diagnosed constant
fallbacks, rather than guessed channel conversions; reproducing them is outside
the Phase 3 compatibility boundary. sRGB Image Color textures are retained
with a conservative alpha-behavior warning, not a claim of equivalence. The
Cycles oracle checks two translucent pixels and an opaque pixel, with and
without Alpha use; it does not establish USD renderer equivalence.
Generic-renderer and hosted acceptance remain separate work.

3.2 has a bounded [Blender 5.2.2 Cycles source-render
check](../../tests/fixtures/native-materials/README.md#oracle-and-checks) for
constant Alpha and Emission. The USD mapping, linked Alpha, render-method and
threshold decision, and renderer acceptance remain open.

3.4 has a local Windows Storm
[texture and normal-map visibility check](../guides/building.md#direct-texture-rendering-in-storm)
against the registered plugin, including constant-color and geometric-normal
negative controls and a session-only named-UV selection check. It does not
establish alpha or normal-map equivalence, tangent-frame correctness, other
renderer cases, or hosted gate results.

## Phase 8 — performance and robustness

After the Blender 5.x path, revisit full older-version compatibility alongside
the phase's robustness work. Select a bounded version range before adding
fixtures; existing regression evidence alone does not establish full support.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 8.1 | [Older-Blender compatibility](../design/BLEND_CONTRACT.md#9-version-support) | choose and document the older-version range and feature scope; add Blender-written oracle evidence for the chosen storage and normal modes, including older packed custom normals where in scope; verify native IR and registered-plugin behavior while preserving 5.x regressions; expand the capability matrix only for fixture-proven scope | ⬜ |
