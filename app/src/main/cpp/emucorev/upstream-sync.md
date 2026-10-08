# Upstream integration audit — 2026-10-08

Fetched code references:

- Vita3K `master`: `9bd372fabf09c4c5386b18757170333cb97cc991`.
- Vita3K-Plus `all-enhancements`: `e2331f809a50f093ca1e2920e47e7dba67d08f3d`.
- Plus `master` is primarily repository documentation; code changes are reviewed from `all-enhancements`.

## Integrated in the first compatibility batch

| Source revision | Change |
| --- | --- |
| Vita3K `9bd372fab` | Reject content with an empty title ID before selecting an installation destination. |
| Vita3K `4b405e8b5` | Clear a pending suspend request when resuming a guest thread. |
| Vita3K `be7ae419b` | Clear the NGS voice pause flag when stopping a voice. |
| Vita3K `a366df69b` | Honor the OpenGL hashless texture cache setting. |
| Vita3K `83b80c8f5` | Find the containing uniform mapping and copy only its mapped portion. |
| Vita3K `0136d8504` | Bound each YUV422 camera plane write; adapted over the local packed-copy fallback. |
| Vita3K `7edda61b5` | Join the display queue host thread on GXM termination. |
| Vita3K `3cf046274` | Finish the save dialog with the expected error for display type zero. |
| Vita3K `e6ac4272e` | Validate save slot IDs. |
| Vita3K `a5aa96572` | Bound save dialog vector access and cap the requested slot list. |
| Vita3K `df9b18ee6` | Return an error for null kernel object names. |
| Vita3K `df2880875` | Correct the guest lightweight condition work area size to 16 bytes. |
| Vita3K `095371f83` | Handle undefined YAML keyboard bindings. |
| Plus `e2331f809` | Apply the partial audio decode offset only to the source buffer. |

Additional local fix: clear the installer metadata for every directory/archive content and reject failed directory SFO parsing. The upstream empty-ID guard alone cannot prevent a title with a missing `TITLE_ID` from inheriting the previous title's ID in a multi-content archive.

## Verification

- Android debug APK assembly succeeds.
- JVM regression suite: 176 tests, no failures/errors (unchanged suite reused by Gradle for this native-only batch).
- Real native installer instrumentation on Lenovo TB710FU: three passing tests. The old APK failed the empty-ID sentinel preservation test; after the upstream guard, the missing-ID multi-content test still failed; clearing metadata made both pass. The valid-package control passes throughout.
- The instrumentation uses a unique cache directory and does not access the user's game library.
- Previous cheats batch: 28 native interpreter/parser/guest page-table tests passed on the same tablet.

This batch does not constitute a full game compatibility test of rendering, camera, sound or save dialogs.

## AAC/SBR compatibility batch

Plus `997654b3f` supplies requested AAC output layout/rate handling, SBR sample sizing, converter reconfiguration and compressor gain smoothing. Additional local correction: convert oversized decoded frames into full temporary PCM before copying the guest's permitted sample count. Simply limiting `swr_convert` output capacity buffers surplus input forever, growing latency and returning audio from old frames.

Verification: five AAC conversion tests plus the 28 cheat tests pass on Lenovo (33 total). Baseline tests reproduced stereo/mono conversion and guest PCM overrun defects; the unmodified Plus patch failed the consecutive-frame backlog test with delay growing from 1024 to 4096 samples. SBR output rate/capacity and changing input channel layout are covered. APK assembly succeeds. These tests provide synthetic decoded frames to the real FFmpeg converter; they do not validate encoded AAC bitstreams or a particular game's sound.

## Cheat catalog and import corrections

The manager filters by the selected title ID and compares the pack revision to the installed base/update `APP_VER`. Known mismatches are hidden by default and cannot be downloaded; unknown metadata is explicitly unverified. A content fingerprint identifies the actual installed catalog variant without being invalidated by enabled/disabled toggles. Downloads verify the catalog's SHA-256 with the same LF normalization as its builder.

Native selection now prefers the canonical `<title>.psv`, then `<title>.txt`, then named variants in stable order. A similarly prefixed different title is not accepted. Per-game imports replace the canonical pack using a staged, validated copy and rename; invalid content and importing the active file itself preserve the current pack. Safe homebrew IDs remain supported.

Verification: 40 real native tests on Lenovo, including six selection/import regressions and the homebrew-ID regression; 187 JVM tests; APK assembly; three Compose card tests on Lenovo covering revision/region labels, replacement action and disabled mismatched downloads. The AndroidX Test helper activities need matching MAIN/LAUNCHER filters in the test APK. These checks do not certify each community cheat's in-game addresses.

The separate EmuCoreV-Cheat catalog now validates all 676 packs, counts 7,917 declarations with parseable code, and corrects four proven title-ID header copy errors without changing code addresses. Additional source mirrors supplied no verified new pack.

## Remaining audit work

- Review the official WaitQueue, callback-wait, UID-table and synchronization cancellation/deletion series together with the local Plus guest scheduler. They are coupled changes and cannot be copied as independent one-line fixes.
- Review the remainder of Plus graphics/audio/controller changes against existing manual ports, including mapped double-buffer lifetime and Mali alignment.
- Finish per-game cheat validation after games are available.
- Rebase/merge the verified main changes into `savestate-experimental`, then diagnose Save-only corruption separately from Load audio/graphics restoration. Verify long-running gameplay after each operation on the tablet.
