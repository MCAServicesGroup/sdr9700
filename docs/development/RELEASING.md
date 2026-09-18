# Releasing SDR9700

Every GitHub release must include substantive, maintainer-readable release
notes. GitHub-generated notes may be used as source material, but a changelog
link by itself is not an acceptable release description.

## Version naming

SDR9700 versions use `YY.M.R`, where `YY` is the final two digits of the
calendar year, `M` is the numeric month without a leading zero, and `R` is the
release sequence for that month. For example, the first September 2026 release
is `26.9.1`.

Stable releases use the numeric version directly, for example `26.9.1`.
Prereleases append a hyphenated prerelease identifier, starting with
`-beta.1` and incrementing the final number for each subsequent beta, for
example `26.9.1-beta.1` and `26.9.1-beta.2`.

Keep the CMake project version numeric because CMake's `project(VERSION)`
field does not accept prerelease suffixes. For a beta of `26.9.1`, set the
project version to `26.9.1` and `SDR9700_DISPLAY_VERSION` to the complete
prerelease version such as `26.9.1-beta.1`.

Do not include a leading `v` in `SDR9700_DISPLAY_VERSION`. The application
adds that prefix when it builds the title bar, which must read
`SDR9700 v<version>` (for example, `SDR9700 v26.9.1-beta.1`).

Git tags always add a leading `v`. A beta release therefore uses a tag such
as `v26.9.1-beta.1`, the title `SDR9700 v26.9.1-beta.1`, and must be marked as a
GitHub prerelease. Create it with:

```bash
gh release create v26.9.1-beta.1 --target main --prerelease \
  --title "SDR9700 v26.9.1-beta.1" --notes-file <file>
```

## Release checklist

1. Set the numeric CMake project version and `SDR9700_DISPLAY_VERSION` in
   `CMakeLists.txt`. Include the prerelease suffix only in the display version.
2. Run a clean Release build with `make release`.
3. Run the complete test suite with
   `ctest --test-dir src/build --output-on-failure`.
4. Write release notes that summarize user-visible highlights, improvements,
   and fixes since the previous release. End with the full changelog comparison
   link. Generated notes may be edited into the authored notes, but must not be
   published without maintainer review.
5. Commit and push the version change.
6. Publish the release as `SDR9700 v<version>` with tag `v<version>`, target the
   verified `main` commit, and supply the authored notes with
   `gh release create --notes-file <file>`. Add `--prerelease` for beta builds.
7. Read the published release back with `gh release view` and verify that it is
   not a draft, its stable/prerelease state is correct, its title and tag match
   the display version, and its body contains the reviewed notes.
8. Confirm that the macOS release workflow started and will attach the signed,
   notarized Apple Silicon DMG to the release.
