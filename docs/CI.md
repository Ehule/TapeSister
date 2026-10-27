# Automated build coverage

The **PR build and regressions** workflow runs on every pull request's default
opened/reopened/synchronize events, without branch or path filters. Changes
limited to Mosaic, pattern headers, controller includes, tests, documentation,
or a newly added source file therefore receive the same baseline check. Draft
PRs also run it. GitHub can still require approval for fork contributors, refuse
a merge-conflicted PR workflow, or honor a commit-message skip instruction;
workflow coverage is not the same as an enforced repository merge rule.

Both Linux and Windows build the actual application in Release mode and run the
14 tests in `.github/pr-regressions.txt`. They cover the core/mixer, project
save/state/sample pages, tracker playback/edit, Mosaic scheduling and pattern
cards, pattern-player lookup, native Mosaic/embedded tracker controllers, source
routing and Matrix processing. The same list selects build targets and exact
CTest names (the two columns can differ for shared executables). The CMake
runner validates every name against CTest's JSON catalog before building;
empty, malformed, duplicate or unregistered selections fail. `--no-tests=error`
also rejects an empty run. The native workflow remains the broader
regression/packaging check.

The baseline builds only these targets and their dependencies with two compiler
jobs. It uses prebuilt system FFmpeg development packages, disables CDP8 and video
bundling, makes no release archive, and uploads
no package. This is a smaller check, not a promise about a fixed runner duration.
It does not replace bundled CDP8/FFmpeg, physical driver, or full release testing.

The existing **Native release bundles** path filters still select the more
expensive bundled Linux/Windows builds, broader selected regressions and archive
download/extraction checks. Manual dispatch and version tags remain available.
Both workflows cancel an older run of the same PR after another revision is
pushed; different PRs, manual runs and tagged releases have separate groups.

## Verified gaps in the former coverage

At main `e383b505073c2122e202f6af50113b14af9aee55`, only Native release bundles
and JACK audio existed, and both used PR path allowlists. A PR confined to any
of these existing files could skip both workflows:

- `src/ts_mosaic.c` or `src/main_sdl_mosaic.inc`;
- `include/tapesister/mosaic.h` or `include/tapesister/pattern_card.h`;
- `tests/test_mosaic.c` or `tests/test_mosaic_controller.c`.

`src/ts_pattern*` was already watched by the native workflow, so the audit's
pattern-card concern did not apply uniformly to every pattern file. The new
unfiltered baseline avoids expanding another fragile allowlist. The workflows
are independent; a source-only PR no longer relies on also touching CMake or a
watched routing file to acquire a build check.

## Local reproduction

With CMake 3.20+, SDL2, FFmpeg development libraries and ordinary platform build
dependencies installed:

```sh
cmake -S . -B build-pr -G Ninja -DCMAKE_BUILD_TYPE=Release -DTAPESISTER_BUNDLE_CDP8=OFF -DTAPESISTER_BUNDLE_VIDEO=OFF
cmake -P .github/pr-checks.cmake
```

Use fresh test fixtures for a repeat run. The embedded tracker test can leave
a modified generated project; reusing that folder can correctly trigger the
save-protection check. Its clean-fixture rerun passes. CI uses fresh checkouts.

Workflow trigger and cancellation semantics follow GitHub's documentation:
[triggering workflows](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)
and [workflow concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).
