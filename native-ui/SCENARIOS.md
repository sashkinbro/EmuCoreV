# Native shell coverage

The target is indistinguishable appearance and behavior against the owned Vita
reference. This is an implementation inventory, not a claim of completion or an
exhaustive list of every behavior found in the original. Add newly identified
states here while reconstructing them. A screen being visible does not prove
its animations, timing, inputs or lifecycle are equivalent.

| Area | Required states and transitions | Current native state |
| --- | --- | --- |
| Cold start | Loading-circle animation; asynchronous resource readiness; lock screen; date/time; peel/unlock gesture and cancellation; transition to Home | BusyIndicator motion/start/stop reconstructed and checked against original functions; bundled texture drawn in separate preview; complete startup/readiness/lock controller pending |
| Resume | Preserve current page, selection, LiveArea and animation state; no repeated cold-start sequence after game/background return | One-bubble development surface preserves motion phase across window recreation; complete controller pending |
| Home | Every visible element, labels, fonts, bubble geometry/materials, idle movement, focus glow, hints and page indicators | One bubble and shader pair; recovered cone-motion basis; full composition/effects pending |
| Paging | Touch drag, linked movement of all elements, inertia, page settling, reversals, edge/overscroll response, indicators and background effects | Pending |
| Input | Touch and gamepad focus/navigation; confirm/back; directional repeat; button prompts; rear-panel behavior where applicable | Pending |
| Editing | Hold on a bubble, entry/exit effects, selection, moving/reordering, deletion, folders and cancellation paths matching the reference | Pending |
| LiveArea | Opening transition from bubble; intermediate game page, background, controls, prompts and reverse transition | Pending |
| Launch | Start action and complete launch animation; launch through the existing game path; real readiness/errors; no black frame treated as success | Host bridge pending; existing game launch path is separate |
| Game running | Stop native UI callbacks, clocks, radio observers and GPU submissions; release appropriate GPU resources | Preview pauses rendering; complete game/host integration pending |
| Return/close | Reference return animation; correct Home/LiveArea destination, selected game and page; cancellation and repeated launches | Pending |
| Status bar | Matching textures/layout/typography; real Android battery/charging, Wi-Fi, Bluetooth and local time; event-driven updates | Kotlin event source and C++ sprite composition visible on Lenovo/RG556; paired electric charging textures and original RNG ported; exact anchors, font rasterization, charging entry/exit opacity and timer-event ordering pending |
| Trophies | Original presentation with real trophy data, locks, progress, lists, details and navigation | Deferred until shell composition/host bridge |
| Library changes | Add/delete/update games; install/remove selected game; refresh icons/metadata; changes while UI is paused or a game is active | Existing emulated catalogue sync implemented; native host binding pending |
| Themes/settings | Original theme/wallpaper behavior; host settings tab; native UI-only resolution; game graphics remain separate | Pending |
| Locales | Every supported application language, including Ukrainian; main strings files; technical terms preserved as appropriate | Host text boundary defined; native screens pending |
| Display/device | Correct geometry on phone/tablet/handheld; corners/cutouts and safe spacing; no stretched UI; independent 60 FPS menu pacing | Aspect-preserving preview; short Lenovo 60 FPS sample; final adaptive layouts pending |
| Release/upgrade | No development preview, probes, loggers or dumpers in release; preserve installed games/settings/trophies during upgrades | Preview is opt-in/debug-only; prior 0.2.7→0.2.8 upgrade tested; new native integration pending |

Before replacing the emulated mode, compare complete sequences, including
interrupted/reversed gestures and failure/recovery paths. Current known shader
fidelity differences on Mali are recorded in README.md and remain unresolved.
