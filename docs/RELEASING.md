# Publishing a macOS release

The **Build and Release** workflow builds an Apple Silicon app on `macos-15`,
runs CTest, bundles Python and Qt, checks native dependencies and signatures,
checks app startup with Qt software rendering, and verifies the DMG and its
SHA-256 checksum. Software rendering lets the startup check run without a GPU
or desktop session; it does not validate the native renderer. The app uses
ad-hoc signatures and is not notarized by Apple. No signing secrets or Apple
Developer account are required.

The DMG includes the project's `LICENSE` (GPL-3.0-only) and `COPYRIGHT` at its
root and inside `Atom Studio.app/Contents/Resources/licenses`. Release notes link
to the exact source revision and build instructions. Third-party components
retain their own license terms; including the project license does not replace
their notices or source requirements.

## Try the workflow before publishing

Once the workflow is on the default branch, open **Actions → Build and Release →
Run workflow** and select the branch to test. A manual run builds and uploads a
`macos-release` artifact containing the DMG, checksum, and proposed release notes.
It does **not** create a GitHub Release, even when run against a tag. Actions
artifacts are retained for 14 days; published release downloads do not use that
retention period.

Download the artifact, review the test summary (including any skipped tests),
and test the installed app on a Mac. Hosted runners may skip graphics tests;
check rendering, structure import/export, and the Python shell locally before
tagging a release.

## Publish

1. Set `project(atom-studio VERSION ...)` in `CMakeLists.txt` to the intended
   `MAJOR.MINOR.PATCH` version and update `vcpkg.json` to match. The workflow
   requires the tag to match the CMake version.
2. Commit and push the release changes to `main`, and check that macOS Build &
   Tests and CodeQL succeed.
3. Create and push an annotated tag on that commit. For version `0.1.1`:

   ```bash
   git tag -a v0.1.1 -m "Atom Studio 0.1.1"
   git push origin v0.1.1
   ```

4. Watch **Actions → Build and Release**. After all build, test, and packaging
   steps succeed, a separate job publishes the tag's GitHub Release with:
   - `Atom Studio-0.1.1-macOS-arm64.dmg`
   - `Atom Studio-0.1.1-macOS-arm64.dmg.sha256`

The publication job uses GitHub's automatic `GITHUB_TOKEN` with `contents: write`;
the build job has read-only repository access. The tag must already exist on
GitHub. The workflow supports stable `vMAJOR.MINOR.PATCH` tags, not prerelease tags.

The release notes state the exact minimum macOS version, installation steps,
checksum command, and CTest results. The minimum currently follows the build
runner's macOS version because bundled Homebrew dependencies are validated
against that version. These downloads do not promise support for older macOS
versions or Intel Macs.

## Failed runs and retries

Build or packaging failures prevent publication. Download
`macos-release-diagnostics` from the run for configuration, build, CTest, and
Qt deployment logs.

A failed upload may leave an unpublished draft release. Delete only that failed
draft in GitHub's Releases UI before rerunning the failed publication job.
Published releases are never overwritten by this workflow: use a new version
and tag for changes. Avoid moving or reusing published tags.
