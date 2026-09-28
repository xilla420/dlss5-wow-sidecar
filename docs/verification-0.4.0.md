# 0.4.0 verification

Verified on Windows on 2026-09-28.

- Release build: Visual Studio 2022, both NVIDIA SDKs present,
  `SIDECAR_REQUIRE_SDKS=ON`; no compiler warnings on the final functional build.
- `sidecar_tests.exe "[unit]"`: 211 cases, 6,342 assertions passed.
- `sidecar_tests.exe "[device]"`: 31 cases, 2,486 assertions passed.
- `sidecar_tests.exe "[deviceloss]"`: 1 case, 10 assertions passed, in its own process.
- `python -m pytest ci -q`: 32 passed.
- `python ci/check_translations.py`: all nine translated tables valid, no stale keys.
- Import checks: manager, runtime and test pattern have no forbidden imports.
- Frontend skill strict static audit: zero findings. This is static evidence;
  its web-oriented rules do not establish native accessibility compliance.

Native UI was exercised in an isolated `out/qa` installation. Observed the
generated welcome artwork, Easy and Advanced layouts, preset selection, dirty
feedback, save success, and the close-with-unsaved-changes dialog. Verified mode
switching leaves a pending preset uncommitted on disk, and Save writes matching
TOML and add-on intensity values. Keyboard focus crossed panels and Enter opened
the language selector; its overflow could be scrolled to all ten languages.

The user stopped Computer Use with physical Escape during the Japanese language
check. No further UI input was sent. The remaining theme/locale/short-window
matrix, UI save-failure injection, and a new live WoW visual comparison were not
completed. The final discard restoration code was compiled and reviewed after
that stop. New translations have not had independent native-speaker review.

GPU tests use the project's test pattern and are not proof of visual quality in
a live WoW session. Neural model/runtime behavior is unchanged by this release.
