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

## Guest API and graphics compatibility batch

Integrated Plus `987bfea47` (named SimpleEvent lookup, correct SimpleEvent deletion map, packed A1R5G5B5 Vulkan swizzles), `f23e32346` (48-byte color-surface guest ABI and clip accessors, descriptor-pool destruction), `3576a5763` (Android debug shader dump gating), and `105331d90` (saturating half-float packing). The half-float adaptation preserves NaN classification, and shader cache version 16 invalidates earlier generated programs. Normal shader cache writes remain enabled.

Integrated official `7ad9d0c33` (validate the requested semaphore cancellation count before mutation), and the relevant `29ffbcfe7` username, touch-sample count and optional-camera startup corrections. Adhoc completion now locks and checks the dialog type before changing a common dialog. Zero-count touch requests return without touching the supplied buffer.

Verification: 43 standalone native tests and 11 tests against the actual shared core pass on Lenovo. The latter cover bounded/NUL-terminated username copies, touch sample counts and canaries, canceled-contact cleanup, named event lookup/deletion, semaphore count validation, and color-surface clip geometry. An ABI-compatible baseline shared library fails the username canary and inactive-peek count regressions; the corrected library passes. Android's event loop also routes finger cancellation to the existing cleanup helper and releases overlay mouse input. APK assembly and 187 JVM tests pass. These API checks do not establish that a reported game's graphical issue is resolved.

## Non-mapped Vulkan vertex alignment

Adapted Plus `35cbde8d869214483558d47576fd6cb52a144e1b` for non-mapped vertex streams. The upload and pipeline paths use the same attribute-range predicate: record padding is applied only when all active attributes fit inside the original stride. Partial final records are copied with bounded reads and zero padding. Mapped streams keep their existing GPU-visible buffer path until readback/synchronization can be established.

Preflight simulates the complete draw's ring allocations, including alignment, wrap and overlapping ranges, before any allocation or stream mutation. This prevents expanded streams from exceeding the ring or overwriting another stream in the same draw. An oversized draw is skipped with a warning.

Verification: Android renderer/full APK builds, independent source review, and 49 standalone native tests on Lenovo pass. Six added cases cover byte preservation, partial records, mapped-path exclusion, crossing attributes, expanded capacity, and aggregate wrap/alignment. No affected game's rendering issue is claimed resolved by these synthetic checks.

## Kernel unified wait and callback batch

Adapted the following official Vita3K commits as one dependency block over local base `75d6df068b9dda8447da946614ed5d3dc14d5b74`:

| Source revision | Change |
| --- | --- |
| `e9058c06fd3d2ef722c1c83043e774df97a45b59` | Introduce WaitQueue and unify thread waits. |
| `ab71f829f0678323bcf3665cd8bea94197cb5832` | Deliver the exit status to threads waiting for thread end. |
| `8da5ce5eb5d2691a0b42ea17358b371eb20aeb02` | Represent thread exit separately from guest wait error results. |
| `ef105504e5ef610024f35477e3ad7f3b42b86cbe` | Match mutex and condition helpers to the other synchronization objects. |
| `07936ba3b39be90ace6e268760902b12c9c854a0` | Unify the waiting place and report its type and UID. |
| `722340b44c03b209afc320673837b335cf84f36d` | Service late callback notifications throughout callback-enabled waits, retaining self-notifications and handling callback deletion/exit. |

Local adaptations preserve the Plus guest scheduler gate/token, priority and affinity behavior, wake counter, mutex cache, diagnostic breadcrumbs/probes, and the existing requirement to reacquire a condition variable's mutex after a timeout. Thread wait and callback context helpers are separated into production translation units so host tests can use the real implementation without the guest JIT.

Additional local correction: a freeze that already accepted a waiting thread as quiescent must block later callback context preparation and restoration. The thread mutex serializes the freeze checks, notification consumption and context rewrite. World, VM and debugger suspension have independent resume conditions; guest execution also checks VM suspension before each quantum. Deletion bypasses a frozen gate without consuming a pending callback or restoring guest context while frozen.

Verification: the Android arm64 native build passes with NDK 29/C++23. Host tests cover wait cleanup/results, priority/FIFO order, late and self-notifying callbacks, non-CB isolation, callback deletion/exit, thread-end exit status, scheduler token release, overlapping freezes in both resume orders, debugger independence, and callback preparation/restoration during freeze and deletion. All six behavioral mutation checks fail as expected. Independent review found no additional confirmed wake, lifetime or join-lock regression. CPU register operations, guest JIT execution and the unrelated registry boundary are substituted in these host tests. After main integration, 13 tests against the real Android shared core pass on Lenovo, including blocked semaphore handover/cancellation and thread-end exit-status delivery using actual ThreadState and synchronization functions. These device tests do not execute guest JIT instructions; in-game validation remains pending.

The UID table/classes were not part of this initial block. The deletion/cancellation follow-up below retains independently integrated SimpleEvent/semaphore fixes.

## Archive installer and repair integrity

VPK/ZIP installation now validates normalized entry paths and case-insensitive aliases before metadata discovery, reads metadata by exact ZIP index, and writes recognized SFO/theme metadata at canonical paths. Exact nested metadata declares a separate content boundary even when that child is invalid. One harmless explicit relative-root directory remains supported; marker-like backup assets remain ordinary files.

Extraction and patch merging prepare a unique sibling stage before replacing the destination with backup/rename rollback. Failed extraction preserves the installed title. NoNpDrm's derived temporary directory cleanup is restricted to the owned archive stage. Title, theme and DLC identities must be safe filename components.

Android archive repair now checks the original CRC and uncompressed size before accepting a repaired file, rejects ambiguous paths, uses independent temporary outputs and removes partial output after failure. BZip2 data is identified as requiring repair for the native installer and can be converted to supported deflate without changing its payload. Successful multi-content installation reports the actual count.

Verification: all 18 real native installer instrumentation fixtures pass on Lenovo, including seven cases that failed against the previous installed APK. All 205 JVM tests and full debug/test APK assembly pass. Independent source review found no remaining actionable issue in this delta. Rename fault injection, interruption recovery, large ZIP64 archives and real NoNpDrm cryptographic content remain outside this verification.

## Remaining audit work

- Review the remaining UID-table and synchronization cancellation/deletion series against the integrated WaitQueue block and local guest scheduler.
- Review the remainder of Plus graphics/audio/controller changes against existing manual ports, including mapped double-buffer lifetime and Mali alignment.
- Finish per-game cheat validation after games are available.
- Merge `savestate-experimental` into `main` while retaining the verified fixes, then diagnose Save-only corruption separately from Load audio/graphics restoration. Verify long-running gameplay after each operation on the tablet.
## Synchronization lifetime and cancellation follow-up

Adapted official `681f695bbc752df38fbb4053473f352de6c8643f`, `7f7027cbee1cad750ee4651531f0576bd1218969`, and `960b776327ad8d79cfcf56c6c2cce87dae507453` while retaining the local per-type object maps. Deletion extracts the object under the registry lock, then marks it deleted and wakes its waiters under its own lock. Operations and information queries recheck liveness after acquiring that lock. Timer waits honor explicit cancellation/deletion results; message-pipe cancellation wakes both queues and clears buffered data. Event, timer, mutex, RW-lock, and message-pipe cancellation exports now forward to implemented helpers.

Condition waits retain untimed mutex reacquisition after a timeout and return distinct condition/mutex deletion and cancellation errors. A deleted LW mutex cannot write its former work area. Diagnostic readers acquire the same object locks; condition waiting passes its retained thread to mutex helpers to avoid an object-to-registry lock inversion.

Verification: eleven shared test cases pass against production synchronization/wait code on the host and against the real shared core on Lenovo; the full Android native debug build succeeds. The prechange implementation failed blocked semaphore/event-flag deletion tests by retaining the objects and timing out. Coverage includes timer cancellation/deletion, both message-pipe queues, invalid mutex cancellation, current event-flag bits on deletion, LW work-area canaries, special condition errors, delayed timeout reacquisition, and releasing the registry lock before waiting for an object lock. Independent source review found no confirmed blocker. The combined device suite passes all 25 cases, including the later shutdown race case. UID storage/query migration and thread-end timeout handling remain separate work.

## Android shutdown responsiveness

A repeated launch request for an already running game could block Android's main thread in SDLThread.join(10000), exceeding the input-dispatch timeout. Shutdown now has one operation shared by relaunch and Activity destruction: the main thread claims it and detaches input, a worker sends quit and waits, and completion returns to the main looper. Native cleanup is skipped if guest execution remains alive. A second SDL Activity cannot initialize over an unfinished shutdown. Rebirth requests are deduplicated, and touch/key/keyboard input is disabled during that operation.

Verification: four production SDL/Robolectric tests cover UI responsiveness while the native thread is blocked, destruction/repeated-request ordering, timeout behavior, native-caller marshalling and rejected overlapping initialization. All 209 JVM tests pass. On Lenovo, the original Persona relaunch sequence reproduced an ANR; the corrected APK completed SDL main approximately 0.36 seconds after pause and launched a new process back to the game's network prompt. This confirms the tested restart path, without claiming long gameplay or save-state correctness. HID-device teardown still follows the existing synchronous path.

## PKG validation and transactional installation

PKG headers and extension headers must both have valid signatures and complete reads. Metadata, encrypted tables, entry names and file data are checked against the declared package/data bounds before allocation or offset addition. Filename components and canonical entry paths are validated. FILE and OpenSSL handles are released on every return. Encryption offsets are required to be 16-byte aligned, matching the checks in [pkg2zip](https://github.com/mmozeiko/pkg2zip/blob/master/pkg2zip.c).

Extraction and PFS decryption happen in owned sibling staging directories; the previous destination is replaced only after success, with backup/rename rollback. Patches merge into a staged copy of the installed app. Themes explicitly require successful PFS mount and file decryption while omitting only the keystone check; neither byte-progress heuristics nor undecrypted fallback can report success. License lookup also validates the full content ID before constructing a path.

Verification: the malformed main-signature fixture deleted the installed sentinel on the previous APK and preserves it with this implementation. All ten PKG and eighteen VPK real-installer tests pass on Lenovo. Synthetic encrypted-table/name/payload fixtures cover out-of-range and unaligned offsets, truncation, unsafe IDs, traversal and missing-license failure. The latter additionally proves progress reached the post-extraction PFS phase and staging cleanup completed. Full debug/test APK builds pass. Independent source review found no remaining blocker. Successful retail/PFS/theme installation and rename fault injection still need representative inputs and are not established by these failure-path fixtures.

## Late guest thread creation during shutdown

An Android shutdown trace showed `process_exit` waiting for the thread registry to empty while `Movie Audio Out` remained parked in the ordinary dormant-thread wait. A guest CreateThread already initializing outside the registry lock can publish its child after shutdown's one-time deletion sweep. The creating guest remains registered until that HLE call returns, so its eventual removal provides the notification needed to sweep the late child. `process_exit` now repeats deletion requests in its locked condition predicate before checking that the registry is empty. This local correction does not add persistent shutdown state or change freeze/scheduler handling.

Verification: a deterministic registry-boundary test using the exact production shutdown body fails on the old sweep (the child never receives deletion) and passes after the correction. The same test compiles and links against the real shared core using real ThreadState objects without JIT initialization. The Android shared library and shared-core test executable build successfully. No device execution is claimed. The correction covers creation by registered guest threads; unrelated native creation after shutdown has already completed remains subject to host session sequencing.
