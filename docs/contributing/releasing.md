# Releasing

The hand-maintained [release workflow](../../.github/workflows/release.yml)
follows the draft-first model used by `usd-vrm-plugins`. It resolves the
Windows/Linux bundle cells from [openstrata.ci.yaml](../../openstrata.ci.yaml),
so runtime digests, runner images, Python and Linux host packages remain owned
by the CI matrix rather than copied into the release workflow.

The resolver bootstrap is pinned to `ost` 0.23.14, like
[stage-contract CI](../../.github/workflows/stage-contract-ci.yml). Update both
resolver bootstraps when changing the matrix's bootstrap version; each rejects
version drift.

## Prepare

1. Keep [VERSION](../../VERSION), [openstrata.toml](../../openstrata.toml) and
   the plugin/library/tool descriptor versions synchronized.
2. Finalize a dated version section in [CHANGELOG.md](../../CHANGELOG.md),
   leaving an empty `Unreleased` section for the next interval.
3. Add `docs/releases/v<version>.md` with scope, limitations, binary
   configurations, assets and verification procedure. It supplies the GitHub
   release notes; repository-relative links are expanded to the tagged tree.
4. Update only the owning [roadmap table](../roadmap/README.md#status-at-a-glance)
   for release mapping, and the documentation indexes for new pages.
5. Merge the preparation, then dispatch `release` on that commit. Manual
   dispatch builds and verifies all targets and uploads `release-dry-run`;
   it does not create a release or publish to a registry.

## Tag and publish

1. Require a successful dry run from the exact commit to tag. Inspect the
   packaged-product checks, archive membership and `SHA256SUMS`.
2. Push an annotated `v<version>` tag on that commit. Preflight requires the tag,
   versions, dated changelog section and release record to agree.
3. Wait for all release jobs. They rebuild from the tag, verify the root and
   standalone paths, check digest reproducibility, and test the extracted
   bundle and aggregate without source-tree discovery.
4. Inspect the draft's notes, source archive, per-target product/plugin/tool
   archives, manifest/SBOM sidecars, optional debug symbols and checksums.
5. Publish the draft and mark it latest when appropriate. Confirm the tag
   commit and downloadable assets after publication.

Only the final assembly job has `contents: write`; builds and dry-run jobs do
not publish packages. A failed target prevents draft creation. Do not replace
a published tag or silently overwrite published assets; correct a released
artifact in a new version.
