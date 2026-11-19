# Standalone native Vita UI reconstruction

This directory contains the independent C++ implementation. Its core libraries
have no emulator, guest CPU, SDL, Android application or game runtime dependency. It is not yet a
replacement for SceShell or the full PAF framework, and is not connected to the
shipping application's UI.

The acceptance target is indistinguishable behavior and appearance, including
animation timing, fonts, materials, gestures, prompts, LiveArea, themes and
wallpaper changes. The latest presentation target is **60 FPS**, with UI work
fully suspended during games. Performance and equivalence remain unproven until
the renderer and paired scenario measurements exist.

## Application boundary

The application supplies library snapshots, changes to installed games, real
trophy data, localized text for every supported application language, preferences
and resource access. The standalone UI emits user intentions such as launching a
game, exiting or changing a setting. The host owns execution of these intentions
and existing game lifecycle contracts. Browsing this UI must not initialize or
call the emulation core. No game rendering settings belong to this module.

All new translated product text belongs in the existing main `strings.xml` files
for all supported locales. The host provides resolved text; this module must not
introduce a second partial translation catalogue.

Firmware binaries remain local development inputs. Ready images and fonts live
in the Git-ignored `native-ui/resources/` directory and are packaged as assets;
they are never extracted from firmware when the UI starts. The resource pack is
not uploaded to Git. Complete materials, layout, animation integration and the
host bridge are still pending. No approximate implementation is marked as equivalent.

The offline `tools/prepare_image_pack.py` uses our standalone C++ readers for
RCO/GIM and Pillow for embedded PNG/DDS images. It writes 935 PNG images from the
current 18 local references, verifies every PNG round trip, and records source,
pixel and output hashes in `images.json`. `tools/prepare_font_pack.py` copies
the supplied OpenType CFF/TrueType fonts unchanged into the same ignored directory.
Neither preparation tool is invoked at runtime or included in the application.
Android device status and lifecycle adapters use Kotlin. The status source
subscribes only while resumed; it does not poll or start the emulator.

## Implemented foundation: resource document reader

`emucorev_paf` currently reads version 0x110 RCOF/CXML/RCSF documents. It preserves
attribute words, float bits, table contents and element offsets; decodes typed
attributes and ID references; and validates table ranges, overlaps, links and
root reachability. Numeric/hash table positions are element indices; file and ID
positions are byte offsets. `paf_resource_inspect` exports the decoded tree as
JSON for comparison. Embedded file bytes can be accessed unchanged or decoded
using original compression metadata with a caller-supplied allocation limit.
Zlib streams require a valid checksum, exact decompressed size and no trailing
bytes. The optional `--extract NEW_DIRECTORY` writes decoded payloads using
element offsets and attribute ordinals, never resource-supplied filenames.
The observed linear GIM texture variants are decoded as described below. An
initial native bubble draw is implemented separately as described below; the
complete original draw pipeline is not implemented yet.

RCSF locale documents share the typed tree format. Their UTF-16 offsets are code
unit indices but their lengths are **bytes**; this was verified against actual
shell locales and corrects the initial synthetic-only assumption. Decoded text
is preserved, without normalization or substitution.

Validation on 2026-11-18:

- Android arm64 C++ build succeeds with Clang 21 / NDK 29.
- Standalone tests pass on RG556 and OnePlus CPH2747: all attribute types,
  float/NaN bits, typed table offsets, reference links, payload integrity/bounds
  and 439 malformed inputs.
  Synthetic type coverage is not proof of original runtime behavior.
- All 17 original shell RCO documents on RG556 parse with range/tree validation,
  including Top Menu (263 elements / 695 attributes) and LiveArea (221 / 659).
- Paired XML/RCO comparison passes for 21 local PAF samples: 575 elements and
  1,162 source attributes, comparing values and references, not just parse success.
- Two additional pairs remain unverified: `paf_sample_cxml_webview` contains a
  `box_bottom` ID disagreement; `paf_sample_feature_screen_orientation` contains
  different ID encoding. The strict comparison reports failure for these inputs.
  Do not silently exclude differences or adjust decoded values to match XML.
- On both RG556 and OnePlus, all 71 extracted Top Menu/LiveArea payloads match
  independent Python zlib output byte for byte (SHA-256), and all 40 nested locale
  documents parse. All 1,960 UTF-16 string values match independent raw decoding.
  This proves decoding, not translation completeness or rendered font parity.

## Reconstructed animation functions

`evaluate_curve` implements the original linear, slower (square root), faster,
quadratic-in and quadratic-out functions, including input clamping and operation
order. These are independent C++ functions, not calls to guest code. They are not
the complete PAF timer or shell transitions. The API's enum values are local and
must not be treated as firmware function IDs.

The functions were reconstructed from the local reference's Thumb/VFP operations.
`tests/generate_curve_reference.py` verifies SHA-256 fingerprints of each code
region, executes the original instructions offline using Unicorn, and records
input/output words. This oracle is a research dependency only. The delivered C++
module has no Unicorn or guest-code dependency. It disables fast-math and fused
contraction so a compiler cannot replace the recovered arithmetic order.

On RG556, Lenovo TB710FU and OnePlus CPH2747, all **41,035** corpus comparisons
match bit for bit: 8,207 input words per function, including 4,097 evenly spaced
points, reproducible random bit patterns, signed zeros, subnormals, infinities,
and quiet/signaling NaNs. The reference oracle uses FPSCR=0 (round-to-nearest,
no flush-to-zero/default-NaN); the original shell's active floating-point settings
still require runtime capture. These results prove only the tested function
subset/settings, not complete animation or frame equivalence.

```text
python native-ui/tests/generate_curve_reference.py OWNED_DECRYPTED_LIBPAF.suprx LOCAL_CORPUS.bin
```

Build `paf_curve_test`, push it and the corpus to `/data/local/tmp`, and run
`paf_curve_test LOCAL_CORPUS.bin` on Android. Running without a corpus performs
only basic checks and explicitly reports that original-code comparison was not
performed. The reference binary and generated research corpus stay outside the
source tree. A different firmware code fingerprint requires independent review;
the generator refuses to execute unverified offsets.

## Scalar animation timer

`AnimationTimer` reconstructs scalar `InitTickUnit`, `InitTimer`, `ResetTimer`
and `DoTick` behavior for the five recovered curves. The clock has no threads,
guest memory, callbacks, system clock access or emulator dependency. Its owner
supplies tick intervals explicitly. Stopping calls costs no background work;
freezing the complete renderer and host callbacks is still a separate integration
requirement.

The original quantizes duration to tick units, applies the exact endpoint-bias
constant `0x3f7fffef`, and stores its reciprocal. Those operations and their
single-precision order are retained. `tick()` returns the original **unclamped
normalized time**; `snapshot().mapped` is the interpolated value. They differ on
overshoot and at finished/repeating states and must not be interchanged by the
future widget scheduler.

Logical state flags retain the original signed repeat-count behavior, first-tick
bit and observed raw repeat transitions. The `round` / `reverse` enum names are
reference labels, not a promise of conventional ping-pong behavior: direct
original calls can advance reverse time below zero. This implementation preserves
the observed comparisons; the surrounding original Animator/Widget scheduling
must still be recovered before deciding how shell transitions use these modes.
Snapshots requiring 2D/3D/custom-parameter curves or unknown states are rejected.

On RG556, Lenovo TB710FU and OnePlus CPH2747, all **2,340 traces / 149,760 frame
steps** match the original instructions bit for bit, including initial duration,
reciprocal, current time, flags, mapped value and every returned normalized time.
The corpus covers five curves; three time units; thirteen duration cases
(including zero, negative, huge, infinity and NaN); four observed repeat modes;
counts -1/0/2; changing intervals, resets, held states and forced finished states.
Oracle FPSCR=0 remains explicit. This proves the tested scalar timing behavior,
not all PAF curves, scheduling, full animations or rendered frame equivalence.

```text
python native-ui/tests/generate_timer_reference.py OWNED_DECRYPTED_LIBPAF.suprx LOCAL_TIMER_CORPUS.bin
```

Build `paf_timer_test`, upload it and the corpus to the device's research storage,
then run `paf_timer_test LOCAL_TIMER_CORPUS.bin`. It requires a supplied corpus;
an absent or empty reference cannot produce a pass. The generator executes the
original functions offline with Unicorn and refuses a different firmware hash
until that profile has been reviewed. No original instructions enter the C++
runtime or source tree.

## Original GIM texture decoding

`decode_gim` independently decodes the observed little-endian PSP GIM variants:
linear RGBA8888, INDEX8 and INDEX4 with RGBA8888 palettes. It handles per-row
padding, odd indexed widths, palette-before-image order and multiple pictures.
Output preserves the stored RGBA bytes without resizing, quantization, alpha or
color-space conversion. The interpretation of alpha and color space in the
original shader/blend pipeline still requires original draw-state evidence.

Unsupported formats, swizzled order, mip levels and multiple frames fail
explicitly. They must be reconstructed before use; the decoder never substitutes
an approximate path. Output allocation is bounded across all pictures. Headers,
block boundaries, plane offsets and palette indices are checked before access.

The implementation follows inspected original resource bytes. The publicly
documented binary fields can also be inspected in the
[GIM format reference](https://github.com/jeffangelion/gimconv_v#GIM-image-structure).
No implementation code or assets from that project are included.

On RG556, Lenovo TB710FU and OnePlus CPH2747, the native decoder reproduces the
independent Python decoder's output for all **841** local original shell GIM
files: **2,984,296 RGBA pixels**. These cover 804 INDEX8, one INDEX4 and 36 RGBA8888
images. The original ARM PAF decoder/rendered pixel comparison remains pending;
agreement between two independent decoders does not prove full render parity.

All three devices also pass synthetic exact-pixel checks for each supported
format, row padding, odd widths, palette order and multiple pictures, plus 329
malformed/over-limit rejection cases. Run `paf_gim_test` on the target device.

```text
python native-ui/tests/verify_gim_pixels.py --adb PATH_TO_ADB --serial DEVICE --executable PATH_TO_ANDROID_GIM_DECODER --inputs OWNED_GIM_DIRECTORY --output NEW_LOCAL_OUTPUT_DIRECTORY
```

The verifier uploads inputs to a unique device research directory, runs the
standalone decoder and compares every RGBA byte with independent Python output.
An optional `--png-map` checks paired original source PNGs using Pillow. No such
PNG pair is currently claimed verified: the four sampled RCO source PNGs remain
PNG payloads, so they cannot serve as GIM-to-PNG conversion reference pairs.
The tool `paf_gim_decode INPUT.gim NEW_DIRECTORY` writes numbered raw RGBA files
and prints their dimensions; it is not included in the application release.

## Shader reconstruction research and device gate

The owned local reference contains 97 embedded GXP programs in the first load
segment of `libpaf.suprx` and 16 in `shell.self`. Their binary lengths, reflected
parameter records/names and primary instruction ranges have been checked.
Parameters expose lighting, environment maps, wave materials and page-fold
controls, but their presence does not prove which programs a particular screen
uses. Active program pairs, mesh data, uniform values, texture bindings and blend
state still require original draw capture.

```text
python native-ui/tests/extract_shader_references.py --output NEW_RESEARCH_DIRECTORY OWNED_DECRYPTED_LIBPAF.suprx OWNED_DECRYPTED_SHELL.self
```

This inventories and extracts supplied decrypted data; it does not decrypt
firmware, run guest code, translate shaders or scan unreviewed load segments.
Original GXP programs and generated inventories stay in ignored research storage.

An offline research helper linked only to existing shader translation archives
generated GLSL ES 3.20 and USSE disassembly for all 113 programs, with default RGBA
texture/surface hints. This helper is separate from the native UI and application;
the translation archives are not dependencies of `emucorev_paf`. Generated shader
sources are not committed. Runtime texture format hints and draw pairing remain
unverified. This is a shader-analysis reference, not an approved shipping shader
set or a completed renderer.

The Android-only `paf_shader_compile_test` creates an isolated EGL pbuffer and
compiles externally supplied `.vert` / `.frag` files on the actual GLES driver.
It requires an exact expected file count and fails for missing inputs or compile
errors. It prints each input before compilation, so a driver crash can be isolated
without starting or modifying the emulator app. Run it through ADB:

```text
paf_shader_compile_test LOCAL_GLSL_DIRECTORY EXPECTED_SHADER_COUNT
```

Measured on 2026-11-18 for the offline reference described above:

- The forced interlock translation fails to compile seven programs on RG556;
  its Mali driver does not support that extension. The exposed coherent
  `GL_EXT_shader_framebuffer_fetch` path compiles all 113 programs on RG556.
- The same framebuffer-fetch variant compiles all 113 programs on OnePlus
  CPH2747 (Adreno 840).
- Lenovo TB710FU (Adreno 750, driver V@0762.37) compiles 109 programs in isolated
  processes. Its driver compiler crashes for fragment programs at reference PAF
  addresses `0x812e4f4c`, `0x812ed258`, `0x812fe500`, `0x812ff950`. Backtraces enter
  the vendor `libllvm-qgl.so` from `glCompileShader`. These four cases are unresolved
  and prevent declaring this GLES reference universally usable. Do not hide them
  or skip their effects. A final renderer needs a verified compatible shader path.

Compilation alone proves neither stage linking nor original arithmetic, blending,
input binding, image equivalence or performance. These gates remain pending.

### Independent Vulkan pipeline compilation

The same 113 original programs have also been translated offline to Vulkan
SPIR-V with direct framebuffer input attachments and default RGBA hints. All 113
pass the NDK's `spirv-val --target-env vulkan1.1`. The Android-only diagnostic
`paf_shader_pipeline_test` links only the system Vulkan loader. It creates its
own Vulkan instance/device and offscreen render-pass description; no emulator,
application, guest runtime or shader translation library is linked into it.

Unlike creating shader modules alone, this test calls
`vkCreateGraphicsPipelines` for every original stage. For a fragment program it
supplies a generated vertex stage with matching reflected inputs; for an original
vertex program it supplies a fragment stage that consumes its reflected outputs.
These diagnostic companions are explicitly not original UI stage pairs and must
never be used to claim a reconstructed screen. Entry-point names come from the
SPIR-V instructions, rather than assuming `main` (`main_vs`/`main_fs` occur in the
reference). Reflected descriptor layouts are created for each stage.

On RG556, Lenovo TB710FU and OnePlus CPH2747, **113/113 diagnostic graphics
pipelines create successfully**, including the four PAF fragment programs that
crash Lenovo's GLES compiler. This establishes a Vulkan compilation path for the
reviewed programs. It does not repair the GLES compiler or establish a final
renderer, original stage pairing, descriptor contents, ordered framebuffer
feedback, correct pixels or 60 FPS. No draws or queue submissions occur in this
gate. All reported device features are enabled for this diagnostic; production
feature selection remains pending.

```text
paf_shader_pipeline_test LOCAL_SPIRV_REFERENCE_DIRECTORY EXPECTED_PROGRAM_COUNT
```

Each reference subdirectory contains `shader.spv` and `shader.meta`, generated
by offline SPIRV-Cross reflection. The line-based metadata uses:

```text
STAGE <Vulkan stage bit: 1 vertex / 16 fragment>
BIND <set> <binding> <Vulkan descriptor type> <array count>
IN <location> <components> <bit width> <SPIRV-Cross base type> <array dimensions>
OUT <location> <components> <bit width> <SPIRV-Cross base type> <array dimensions>
INPUT_ATTACHMENT <index>
```

The reviewed interface consists of scalar-array-free float32 vec4 locations
(SPIRV-Cross base type 13), input attachment index 0 and descriptor sets 0–3.
Unreviewed interface variants are rejected explicitly. Original programs,
reflection files and translated shaders remain ignored local research inputs.
Neither diagnostic shader test is part of the application build or release.

## Original bubble draw capture

The original `BubbleObj` draw path has been traced through its renderer queue to
the `bubble_opaque.vp` / `bubble_opaque.fp` programs. The original shaders' program
bytes match the extracted reference programs by SHA-256, rather than relying on
their names alone. On the smartphone and RG556 the captured draw uses a 608-vertex
stream (32-byte records), 1,220 uint16 indices and the same shader pair. Vertex and
index buffers match byte for byte between these captures. Viewport and vertex/
fragment uniform buffers are recorded as original bit patterns.

The opt-in capture implementation lives in the emulated reference's SceGxm debug
path, not in this independent module. Both its implementation and call site are
guarded by `__ANDROID__ && !NDEBUG`; it refuses sessions other than the private
native-library sentinel and the two reviewed BubbleObj vertex program GUIDs.
It performs no writes to guest state. A numeric token in the Android property
`debug.emucorev.shell.mesh` requests one capture per process/token; `0` disables
capture. Reusing a token does not repeat or overwrite the output. Capture files
are confined to the application's cache path, with fixed generated filenames and
bounded allocation/I/O (2 MiB per buffer, 8 MiB total). Failures consume the token
to prevent continuous per-frame I/O. Change the token for an explicit retry.

The debug reader first tries canonical guest memory. For separately mapped GPU
pages it can resolve individual pages through the guest page table and use
kernel-mediated reads while holding the external-mapping transition read lock.
Unreadable texture data is explicitly marked `texture_unavailable` while retaining
the descriptor. It is not replaced with invented pixels. The completed reference
captures still have an unavailable 64x64 environment-detail texture at fragment
slot 1. GPU-cached texture contents and freshness need additional capture before
these files can reproduce the full draw. The 128x128 indexed color texture at slot
0 and its palette were captured; different installed titles can supply different
icons, so cross-device icon bytes are not assumed identical.

Debug/release builds, unit tests and lint passed for this capture addition. The
APK's debug `libECV.so` contains its property, manifest and failure markers; the
release library contains none of those markers. Smartphone and RG556 capture
integrity checks pass; seven deliberately corrupted host captures are rejected.
The independent UI still does not initialize or link the emulation core.

```text
python native-ui/tests/inspect_mesh_reference.py LOCAL_CAPTURE_DIRECTORY --original-programs LOCAL_EXTRACTED_GXP_DIRECTORY --output LOCAL_REPORT.json
```

This checks completed manifests, exact buffer sizes, index/attribute bounds,
finite float vertices, uniform/texture files and shader identity. It explicitly
reports missing texture sources and leaves `pixelReplayVerified` false. It does
not establish original GPU readback, full frame capture, correct resource binding
or rendered equivalence. Original buffers and reports remain ignored artifacts.

## Native bubble renderer: first actual draw

`emucorev_paf_gles` is an independent C++ GLES renderer. Its host supplies shaders,
32-byte profile vertices, 16-bit strip indices, RGBA icon/environment images and
the reviewed material parameter blocks. It links GLES only: no firmware loader,
guest execution, emulator runtime or shader translator is linked into this module.
Parameter blocks use small std140 uniform buffers: unlike vertex storage
buffers, these also link successfully on the RG556's Mali-G57. The reference
preview adapts only the reviewed read-only vec4 arrays from std430 storage to
std140 uniform blocks; their array byte layouts are identical and shader math
is unchanged.
GPU buffers, images and the program are created once; a draw updates parameter
buffers and submits the strip without allocating or loading resources. All
calls, including destruction, require the owning current context and thread.
The renderer owns its draw state; a host sharing the context must rebind its own
state afterwards. The host owns the framebuffer, viewport, presentation, pacing
and suspension while games run.

`paf_bubble_preview` renders one 960x544 frame to a top-down RGBA file using local
external reference inputs. It converts the captured indexed icon through its
actual palette. Shaders, captures and images are not packaged or committed.

```text
paf_bubble_preview CAPTURE_DIR VERTEX_GLSL FRAGMENT_GLSL ENVIRONMENT_64_RGBA OUTPUT_RGBA
```

Actual draws and readback succeeded on the OnePlus CPH2747 (Adreno 840) and
Lenovo TB710FU (Adreno 750). Direct presentation uses native GLES Y and the
corresponding winding, avoiding the reference backend's second presentation
flip. Both devices were run with the captured steady-state geometry/uniforms
and an explicitly unresolved zero environment input. That input demonstrates
rendering only and is **not** an equivalent material. Reflection generation,
filter/address state equivalence, blending equivalence, paired pixel comparison,
selection effects and full-screen composition remain unverified. The command
does not display an interactive Android screen. No 60 FPS or performance
claim follows from a single offscreen draw.

### Reconstructed opaque-bubble shader pair

`opaque_bubble_vertex_shader()` and `opaque_bubble_fragment_shader()` provide
our compact semantic implementations. They contain no translated register
arrays or instruction dispatch. The vertex stage reconstructs profile-based
position/normal transforms, the original cubic angular approximation, light and
view vectors, diffuse/specular factors, background-height blend, cubic facing
blend, icon coordinates and projection. The fragment stage reconstructs the
icon, reflection, back-color and specular composition, retaining individual
half-rounding boundaries and explicit fused operations.

The standalone preview accepts `--native-vertex` in place of the vertex file and
`--native-material` in place of the fragment file. Neither stage requires an
external shader translator or original shader binary at runtime.

Actual 960x544 output of the complete own shader pair matches the translated
reference pair byte for byte on Adreno 840 (OnePlus) and Adreno 750 (Lenovo), for
one steady-state mesh/transform fixture, a synthetic gradient environment and
three fragment parameter sets (captured, mixed RGB and translucent alpha).
On Mali-G57 the program renders successfully, but **does not yet match**: the
same three comparisons differ in 3863, 3654 and 1440 channels, respectively.
This remains an explicit fidelity defect; the test fails rather than accepting
a tolerance. The three fixtures do not establish general shader or original
firmware pixel equivalence, and reflection-source generation is still pending.

```text
python native-ui/tests/compare_bubble_material.py --adb ADB --serial DEVICE --executable PREVIEW --capture CAPTURE_DIR --vertex-shader REFERENCE_VERTEX --fragment-shader REFERENCE_FRAGMENT --output LOCAL_REPORT.json
```

This compares real framebuffer bytes, rejects an empty bubble, leaves all
reference inputs external and removes its temporary device directory. Its
`--fragment-only` option keeps the reference vertex stage for fault isolation.

### Isolated Android development surface

The optional `preview` module displays the own shader pair in an actual Android
window under the separate package `com.sbro.emucorev.nativeuipreview`. It does not
replace or update EmuCoreV. Its APK contains only `libpaf_preview.so` and no
external reference assets, emulator library, firmware or game runtime. Normal
builds do not include this module, and its release variant is disabled.

```text
gradlew -PnativeUiPreview=true :native-ui-preview:assembleDebug
```

Local debug reference inputs are prepared in the ignored resource pack before
building. The debug APK packages these as `preview/bubble` assets; a fresh
installation no longer needs private-file injection, installed firmware or the
emulator. The mesh, initial pose and icon sample still come from the local
capture, rather than a complete reconstructed widget/geometry generator. The
current surface is one bubble on a flat clear color, with recovered cone-motion
geometry applied relative to the supplied pose. It is not the reconstructed
menu/background or complete widget animation system.

```text
python native-ui/tools/prepare_image_pack.py --adb ADB --serial DEVICE --exporter ANDROID_EXPORTER --output native-ui/resources OWNED_RESOURCES.rco [MORE_RESOURCES.rco ...]
python native-ui/tools/prepare_font_pack.py LOCAL_PVF_DIRECTORY native-ui/resources
python native-ui/tools/prepare_preview_fixture.py LOCAL_BUBBLE_CAPTURE native-ui/resources
```

Image preparation expects a new output directory. Font and preview preparation
then add their files to that pack. A missing pack fails the opt-in preview build
with an explicit preparation message. The fixture helper defaults to a zero
reflection texture, explicitly a placeholder until that source is reconstructed.

`SpriteRenderer` composes straight-alpha RGBA images in C++, uploading only
changed images and retaining them for the GL context lifetime. The Kotlin adapter
decodes 13 packaged indicator textures and rasterizes changed clock text with the
packaged Rodin bold font. Minute/time-zone/format changes, battery, Wi-Fi and
Bluetooth events feed the preview without per-frame JNI calls or polling.
The visible status panel was launched on Lenovo and RG556; Lenovo's clock matched
device time and its battery selection used the real 78% reading. Exact panel
anchors, original font rasterization, charging animation and complete radio
presentation remain pending. The Wi-Fi strength conversion uses Android's RSSI
scale rather than an emulated Vita radio service. The packaged debug APK contains
935 PNGs, 20 fonts and only `libpaf_preview.so`; no emulator library is loaded.

Rendering uses a 960x544 offscreen target and aspect-preserving presentation.
Window recreation releases and rebuilds all context-owned resources. Native
choreographer callbacks target 60 frames per second only while resumed and
focused. Pausing clears timing history, preserves the phase and stops drawing;
there is no background polling timer or catch-up after returning. Actual Lenovo
launch and Home/resume succeeded with both EmuCoreV processes stopped. The
initial static surface accumulated zero additional idle process CPU ticks.
Complete lifecycle, sustained animation pacing and final adaptive layouts still
require validation with the full scene.

The current animated Lenovo sample contains 126 presented frames at 60.00 FPS,
with a 16.666667 ms median interval and 16.667343 ms maximum interval. After
Home/cleanup settled, the paused process accumulated zero additional user or
system CPU ticks during the observation. This is evidence for this one-bubble
surface, not a full-menu load or thermal benchmark.

### Recovered shell cone-motion basis

`bubble_wobble_basis()` and `advance_bubble_phase()` reconstruct the reviewed
shell function at `0x810cf332`: phase advances by half the product of the two
framework deltas and wraps at positive pi; the cone amplitude is 0.15 times its
setting. Three axes are built by normalization and cross products. The ARM port
retains the original estimate/refinement sequence, including AArch32's separate
rounding in reciprocal and square-root steps. Simply substituting the similarly
named AArch64 fused intrinsics changed reference output and was corrected.

All 132 numeric cases (33 phase positions, four strengths) match the original
function bit for bit on OnePlus, Lenovo and RG556. Their trigonometric samples
are explicit inputs: this does **not** verify platform libm against the original
cos/sin implementation. The preview uses platform libm and a provisional
relative transform; exact per-widget integration, phase initialization,
framework-clock mapping and all other motion/effect controllers remain pending.

```text
python native-ui/tests/generate_bubble_motion_reference.py OWNED_DECRYPTED_SHELL LOCAL_CORPUS.bin
paf_bubble_motion_test LOCAL_CORPUS.bin
```

The generator executes the reviewed original function offline and supplies two
explicit math-call results, leaving its vector helpers unchanged. The original
binary's whole-file SHA256 is checked before execution. No original instructions
or test interpreter are linked into the UI.

## Build and validation

### Recovered busy indicator

`BusyIndicator` reconstructs the PAF component's head and eight retained trail
objects, including the 0.3-second entry, 0.5-second exit, interrupted start/stop,
phase wrapping, emission spacing, size relaxation and packed-byte alpha decay.
The trail positions are retained particles, not a uniformly rotated static ring.
The owner advances active time explicitly; this class owns no clocks or resources.

The offline oracle executes the reviewed original update/start/stop functions.
Its minimal object host records position/size setters, leaves the original
packed-color setter executable, and suppresses widget notifications. All 32
traces (four dimensions/ball sizes, 30/60/90/120 Hz, uninterrupted and interrupted
sequences) pass **18,560 operations bit for bit** on Lenovo and RG556, comparing
state, phase, elapsed time, emission history and all nine objects' positions,
sizes and alpha. Both implementations use double host-libm trig samples rounded
to float here; this does not establish original PAF libm, widget scheduling,
complete startup timing or rendered-pixel parity.

```text
python native-ui/tests/generate_busy_reference.py OWNED_DECRYPTED_PAF LOCAL_BUSY_CORPUS.bin
paf_busy_test LOCAL_BUSY_CORPUS.bin
adb shell am start -n com.sbro.emucorev.nativeuipreview/.PreviewActivity --es scene busy
```

Start the scene in a fresh preview activity/process. This debug-only component
surface uses the packaged `_common_texture_busy` image (hash `ba195a37`), with an
80-pixel test extent. Tap or gamepad A interrupts/stops/restarts it. The completed
idle state stops frame callbacks; backgrounding preserves state and suspends
time. This scene does not load the bubble geometry/material or subscribe to
unused status services. It is a component preview, not the completed cold-start
controller: lock-screen, readiness transitions and original screen placement
are still pending. Its input toggle is not part of the shipping UI.

Build independently using CMake 3.22+ and a C++20 compiler. For Android, supply
the NDK toolchain, `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-29` and
`ANDROID_STL=c++_static`. Build outputs belong outside this source directory.

On a host build, run CTest. On Android, push `paf_resource_test` to
`/data/local/tmp`, grant executable permission and run it through ADB. The test
does not start or change the emulator application.

For external paired resource samples:

```text
python native-ui/tests/compare_resource_samples.py --adb PATH_TO_ADB --serial DEVICE --executable PATH_TO_ANDROID_INSPECTOR --samples PATH_TO_SAMPLES
```

The comparison exits nonzero for any disagreement. Original source/binary pairs
must be verified before they become reference fixtures. Research tools and tests
are separate from the app build and must not be added to release packaging.

For embedded payload and locale decoding:

```text
python native-ui/tests/verify_resource_payloads.py --adb PATH_TO_ADB --serial DEVICE --executable PATH_TO_ANDROID_INSPECTOR OWNED_RESOURCE.rco [MORE_RESOURCES.rco ...]
```

This creates isolated research outputs in a fresh `/data/local/tmp` directory
and checks extracted hashes and UTF-16 text against independently decoded input.
