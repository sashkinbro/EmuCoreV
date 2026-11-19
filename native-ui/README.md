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

The lock-screen preview now connects the recovered widget configuration and
materials into one interactive development scene. The shell constructor
`8123b85a` creates a 960x512 PageTurn at centered position (0,-16), applies
fold ratios (.23,.85), Graphics margins (32,32), and corner extents (.08,.08).
Graphics assembly `8108047c` yields the 896x448 inset surface; `810ca2f8`
derives CornerSize (.96,.92,25,12.5). Native pose and model matrices remain
separate. The page samples the corresponding rectangle of the bundled canvas.
Date/hour/minute/separator/AM-PM positions, regular/bold fonts and sizes are
derived from the `power_manage_plugin` template f5228777 rather than the former
manually composed large labels. Ready assets remain in the ignored resource pack.

On Lenovo, the visible page curl, dragging, short-drag return, complete peel,
and minute changes were inspected in the running preview. Text-bearing page
textures update by revision; the pristine wallpaper is a separate underlying
image. Android gesture cancellation returns to rest; the source base threshold
is 280. The exit fold follows the recovered 500 ms sine curve instead of the
previous 300 ms quadratic placeholder. The optional empty-back renderer program
preserves the pre-existing rectangle constructor argument. Assemble and lint pass.

This scene remains incomplete: the offscreen preview flattens clip-space depth
while retaining model depth for lighting, and orders the curled face after the
overlapping surfaces. Original camera/depth and complete pass equivalence are
not established. Peel now enters the development Home composition; full startup,
exit alpha/Z tracks, original text shadows/rasterization,
gamepad navigation and remaining shell transitions still need integration.
The current visual result is **not** declared 1:1 or a replacement for the
shipping emulated mode. This work was checked only on Lenovo; Anbernic is
reserved for another agent at the user's request.

Home consumes a title-keyed catalog snapshot and packaged game icons instead of
opening the emulator. The current fixture contains two installed games copied
read-only from Lenovo. Bubble renderers are retained for the visible page only.
The 3-4-3 arrangement is provisional: exact shell grid transforms and paging
motion are not yet recovered. The default animated wave background is missing;
direct Home currently clears to black, while unlock still retains the lock
wallpaper underneath Home. Neither is the original default Home background.

The development LiveArea viewer consumes the game's actual template and listed
images, selects localized/default backgrounds, and composes its gate, frame
images and text natively. Only style A3 is supported currently; its placement
table comes from the existing LiveArea implementation, not a proven original
SceShell layout trace. Opening/closing animations, full template semantics,
gate labels and the production launch bridge remain pending. Resource fixtures
remain ignored; the preparation tool copies only template-referenced images.

Assemble and lint passed for this integration. On Lenovo, both fixture LiveAreas
were opened from Home and inspected. System Back now returns to Home without
finishing the NativeActivity; modern Android back callbacks and the legacy key
path queue the request for the native thread. The Android XML parser path was
also exercised on-device after removing unsupported desktop parser flags.

Cold preview startup now renders BusyIndicator while a single worker prepares
the ZIP, images, catalog and LiveArea snapshots. A separate 767-byte bundled
busy image is available before ZIP extraction. The main thread publishes the
ready scene only after preparation; native scene replacement happens on the
render thread, destroying the previous EGL window before creating its successor.
Cancellation serializes JNI publication with Activity destruction. Home bubble
GPU resources are destroyed before their EGL context, and LiveArea gate meshes
are cached per snapshot rather than allocated every frame.

Lenovo verification covered a fresh preview-data extraction, reaching Lock,
unlocking Home, opening LiveArea, Back, background/resume and repeated startup.
Assemble/lint passed. This verifies readiness integration, not original cold-start
duration, loading-circle placement or complete shell lifecycle equivalence.
Loading ignores the component preview's tap/A stop toggle; ready/failure requests
wake the native looper even if the busy component has become idle. The looper
pointer is synchronized with native-thread shutdown. Lenovo loading-input checks
reached the prepared shell without hanging; explicit component preview controls
remain separate. The former 80-pixel/10-pixel busy fixture has been replaced by
the topmenu template's 72-pixel extent, 16-pixel balls, cyan tint and texture
`754463d1`. Shell function `81297df8` opens template `b6e09720` and resolves child
`ae8fa25b`; these match the extracted RCO. This proves that Shell template path,
not the complete power-on loading sequence or its final screen placement.

Unlock now renders Home underneath the peeling page and applies the recovered
300 ms quadratic-out meta-alpha track from `811016e2` to every PageTurn surface,
alongside the existing 500 ms sine fold. A native shader state extension carries
transparency in the previously unused tail float; zero retains the prior opaque
shader result. The original 300 ms Z track/camera mapping remains unresolved.
Lenovo recorded the transition with Home showing through the fading page. The
focused native shader check passed surface submission, shadow pixels and 16
material draws; this is not a full original-frame equivalence comparison.

The wave reconstruction now includes the native `wave_pos.vp/fp` position pass
and the standard `effects_default_0512_01` sky parameter snapshot. Preset floats
are reconstructed from ARM/VFP writes before registration at `0x810b0fba`;
they are not sampled from a theme chooser thumbnail. The position pass consumes
a caller-owned height texture, four source vertices and explicit register blocks.
Both embedded GLES shaders compile on Lenovo's Adreno 750; the module builds,
but active simulation inputs, linked drawing and full background equivalence are
not established. This stage is not yet connected to Home. The recovered source
chain is `draw_pos` (`0x810a1a80`) through helper `0x810a25ba`, followed by
lighting texture `0x810a26d0` and color `0x810a451e`, called from `0x810a6688`.

The final surface component now embeds the named `wave.vp/wave.fp` pair, samples
caller-owned position/lighting textures and draws the supplied indexed wave mesh.
Its mesh uploads change only when source bytes change. Both the position and
surface renderer constructors compiled and linked their actual shaders in a
Lenovo EGL context without GL errors. No complete-wave draw or Home background
parity is claimed: simulation surfaces and the intermediate lighting pass are
still required before these components can produce the original animated scene.

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
in the Git-ignored `native-ui/resources/` directory and are packaged in one ZIP;
they are never extracted from firmware when the UI starts. The resource pack is
not uploaded to Git. Complete materials, layout, animation integration and the
host bridge are still pending. No approximate implementation is marked as equivalent.

The preview build creates a reproducible `native-ui.zip` plus its SHA-256 version
under its ignored build directory. Only those two assets enter the APK; the
unpacked source directory is not duplicated. `BundledResources` verifies the
archive and installs it once into a versioned app-private, non-backed-up directory.
It stages the complete ZIP before an atomic directory rename, serializes installs
with a process/file lock, rejects escaping paths and oversized packs, and retries
partial installs on the next launch. Changed content selects a new version and
removes obsolete ready packs after activation; unchanged content skips extraction.
Kotlin images/fonts and C++ preview geometry read the installed files directly.

Current pack: 1,033 files, 53,694,683 bytes unpacked, 30,799,432 bytes in ZIP.
Extraction needs additional private storage; it reduces the bundled archive size,
not the size of the decoded files. Both Lenovo and RG556 verified every installed
file against the ZIP, first/repeated startup, unchanged completion timestamps,
partial-install recovery and obsolete-directory cleanup. Preview assemble/lint
pass. This installer is connected only to the independent preview at present.

The offline `tools/prepare_image_pack.py` uses our standalone C++ readers for
RCO/GIM and Pillow for embedded PNG/DDS images. It writes 935 PNG images from the
current 18 local references, verifies every PNG round trip, and records source,
pixel and output hashes in `images.json`. `tools/prepare_font_pack.py` copies
the supplied OpenType CFF/TrueType fonts unchanged into the same ignored directory.
Neither preparation tool is invoked at runtime or included in the application.

`tools/prepare_effect_pack.py` additionally prepares the five standalone effect
DDS textures and the lock wallpaper under the ignored `resources/effects/`.
All 19 supplied mip levels are retained as separate PNGs, without resizing or
regenerated mipmaps. BGRA8 channels are reordered losslessly; the alpha-only
earth texture retains its original byte plane in a grayscale PNG, explicitly
marked `alpha8` for the renderer. The lock PNG is copied unchanged. The manifest
records source/pixel/output hashes. These assets are ready for renderer
integration; their presence does not establish effect or lock-screen parity.

```text
python native-ui/tools/prepare_effect_pack.py --effects LOCAL_EFFECTS_DIRECTORY --lock-image LOCAL_KEYLOCK.png --pack native-ui/resources
```

The two procedural PageTurn lookup images are also prepared offline by
`tools/prepare_page_turn_lookups.py`: the **128x8 exponential table (range 5)**
and **64x64 radial corner-distance table**. It executes the reviewed original
generators in an offline ARM VM. The original `sce_paf_expf` implementation runs
unchanged, with all **1,024 calls counted**; the radial generator makes no expf
calls. Only graphics allocation/upload/wrap, byte-vector allocation and deletion
are hosted. No host-libm approximation or hand-authored gradient substitutes for
the generated bytes. Both images round-trip through scalar8 PNGs exactly, and
their manifest retains source/pixel/PNG hashes, dimensions, range and wrap values.

The results live in the ignored `resources/effects/lookups/` and are packaged in
the preview APK. Neither reference code, Unicorn nor this development tool enters
the application. No procedural generation or firmware access is needed at UI
startup. The pre-build resource check requires both ready images and their
manifest. Runtime material/image assembly remains pending.

```text
python native-ui/tools/prepare_page_turn_lookups.py --reference OWNED_DECRYPTED_PAF --pack native-ui/resources
```

The same ignored pack now includes `audio/`: 44 PCM16 WAV sounds from the current
18 RCO inputs (all sound payloads are in the common reference). Four sounds retain
separate left/right channels as stereo; mono sounds remain mono. The offline
`tools/prepare_sound_pack.py` uses `paf_export_sounds` plus a supplied vgmstream
CLI with HE-VAG support. It preserves the 48 kHz source rate, verifies sample
counts and stereo ordering, and applies no resampling, normalization or gain.
The ordinary PSX ADPCM decoder in the available FFmpeg build rejects these
HE-VAG frames, so it is not used as a conversion fallback.

`audio/sounds.json` records source/channel/PCM/WAV hashes, decoder metadata,
looping metadata and all original sound-event bindings. WAVs are compressed
inside the shared ZIP and read directly after its first-start installation. Neither vgmstream, its DLLs, VAG data nor
the firmware containers enter the APK. Runtime event routing/playback remains
to be connected as the native UI interactions are implemented.

```text
python native-ui/tools/prepare_sound_pack.py --adb ADB --serial DEVICE --exporter ANDROID_SOUND_EXPORTER --decoder VGMSTREAM_CLI --pack native-ui/resources OWNED_RESOURCES.rco [MORE_RESOURCES.rco ...]
```

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
anchors, original font rasterization, charging entry/exit opacity and complete radio
presentation remain pending. The Wi-Fi strength conversion uses Android's RSSI
scale rather than an emulated Vita radio service. The packaged debug APK contains
935 PNGs, 20 fonts and only `libpaf_preview.so`; no emulator library is loaded.

The charging overlay now selects the two original electric textures using the
recovered `sce_paf_rand` LCG (`state * 0x41c64e6d + 0x3039`, bit 16 selects the
image), with UI-owned state preserved across pause/resume and GL recreation.
The shell's charging callback selects those same images with `rand() & 1`; the
effect is disabled at full charge. Preview frame ticks currently drive this
selection. Original handler-timer event ordering and repeated 200 ms show-alpha
animation remain to be integrated; this is not a claim of complete charging
transition equivalence. The private RNG does not reproduce unrelated consumers
of the original shell's shared global random stream.

On Lenovo, real charging at 77% produced both electric texture frames. A temporary
BatteryService 100% override produced a stable full battery without that overlay.
RG556's temporary 50%/charging override also produced both frames. Both devices'
real BatteryService state was restored after the checks. These checks verify
host-state binding and frame selection, not original rendered-pixel parity.

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

### Recovered PageTurn controls and fold geometry

`page_turn.h` provides the original centered/Y-up fold layout, corner hit region,
drag target, normalized draw controls/back-opacity mask, and pointer/gamepad
release decisions. Fold fraction and aspect remain caller settings: no shell
screen dimensions or guessed lock-screen settings are baked into the component.
The drag calculation retains its original order and 0.7 overshoot factor.
Hit testing includes the excluded diagonal at the corner as well as the two
rectangle boundaries. Pointer and gamepad outward-release thresholds differ.

`page_turn_fold_shape()` additionally reconstructs the renderer's fold-plane
geometry: clamp the deepest point, build the normalized plane, project/mirror
the four corners, select signed-distance extremes and clip the fold line against
the page's four half-planes. The ARM implementation retains the original
reciprocal/root estimates and non-fused refinement steps. Its returned shape is
UI-owned data; it reads no guest memory and performs no allocation or I/O.

`page_turn_mesh()` constructs the original front and folded triangle fans in
reusable caller-owned storage. Front bands grow with the original 0.01 step;
the folded section uses 16 cubic-spaced bands. Half-plane clipping preserves
the original vertex order, including entry/exit intersections.

The offline oracle executes the reviewed original PageTurn functions, retaining
their math and branch decisions while supplying local pointer coordinates and
suppressing widget notifications, capture and drawing. The fold-shape function
and its projection/clipping helpers execute unchanged in the reference. The
original mesh builder additionally receives preallocated buffers and a dummy
graphics context; its tessellation and clipping execute unchanged. All
**422 control/shape/mesh records match bit for bit** on Lenovo and RG556,
including every vertex and front/folded fan range. The corpus covers four
page sizes, three fold configurations, hit-region boundaries, opacity, release
thresholds, outward-release options and additional deterministic input samples,
including fold saturation and large drags outside the page. The compared fields
include all projected/mirrored corners, signed distances, extrema, clipped-line
endpoints and the whole-line-outside flag.
It does not prove Android input mapping, gesture capture/lifecycle, rendering,
the shell's lock-screen configuration or spring/settling animation parity.

```text
python native-ui/tests/generate_page_turn_reference.py OWNED_DECRYPTED_PAF LOCAL_PAGE_TURN_CORPUS.bin
paf_page_turn_test LOCAL_PAGE_TURN_CORPUS.bin
```

The folded top/bottom surfaces, round edges, shadow passes and
lighting parameters still need their native renderer and original-frame
comparison. These controls are not presented as a completed unlock screen.

`page_turn_empty_back_fragment_shader()` is the first native material for the
rounded empty back surface (reference program `0x812ed258`, which crashes the
Lenovo driver compiler in its generic translated form). It implements corner
sampling, background tint, shadow subtraction/highlight and opacity with
explicit half-register rounding. It accepts normalized native textures and
retains the original four-vec4 material layout. Guest raw-format casts and
guest framebuffer-resolution compensation are deliberately outside this
native shader's contract.

The standalone `paf_page_shader_test` draws 16 material cases into an RGBA8
target, using spatially varying coordinates and two nonuniform filtered test
textures. The native shader compiles, links and renders on Lenovo Adreno 750
and RG556 Mali-G57. On RG556, comparing the generic original translation to
this shader currently finds **10,510 differing channels out of 524,288, maximum
absolute difference 1/255**. This is an unresolved precision discrepancy, not
pixel parity; the shader is not yet connected to the preview lock screen.
The original translation is not executed on Lenovo because of the documented
vendor compiler crash. A native-only test explicitly reports that no reference
comparison took place. This test executable is never linked into an app.

Float-target diagnostic probes additionally exposed eliminated half-rounding
boundaries on Mali: the native pack/unpack expression can retain a full-float
result, while other packed-register expressions in the generic reference retain
different boundaries. For example, an alpha probe produced native `0.0931373`
versus reference `0.0931396`. Forcing every native boundary changes the observed
reference mismatch rather than resolving it. That experiment was not retained
as a material correction. Original hardware arithmetic and observed emulated
frames still need to be reconciled before declaring material parity.

`PageTurnRenderer` now owns the GLES vertex storage, front/folded programs and
four reusable UBOs. It uploads the recovered mesh and submits each counted fan
in original order, with no per-draw allocations or resource reads. Host-supplied
shaders/material blocks remain independent of the emulator. The vertex lookup
texture is assigned unit 4 separately from fragment units 0..3. Shader programs
are compiled only during renderer construction; short parameter blocks are
zero-padded to prevent stale state from a previous material.

The same standalone device test checks this submission path with diagnostic
red/blue materials over four recovered folds. On both Lenovo and RG556, all
32,768 pixel centers select the CPU half-plane's expected page side, with no
uncovered pixels. These diagnostic materials prove geometry submission only;
they are not used as substitutes for the original curl, lighting, edge or shadow
materials. The complete lock-screen renderer/controller remains unfinished.

`page_turn_top_vertex_shader()` now implements the top surface's semicircular
curl, reflected planar coordinates, displaced normal, model/projection transforms,
view-facing opacity, light response and UV transform. It uses the reviewed
28-vec4 material layout and retains the original operation order and explicit
fused operations. The C++ library supplies this GLSL directly; no original
program container, guest instructions or runtime translator is involved.

The `--top-vertex` device check captures position and both output varyings with
transform feedback, before rasterization. It uses all recovered mesh vertices
from four folds and three projection/scale configurations. On Lenovo, all
**16,308 float words match the original translation bit for bit**, with no
nonfinite outputs. On Mali-G57, the same strict comparison currently reports
**2,187 differing words, maximum absolute difference 0.0000193417**; it exits
nonzero rather than accepting a tolerance. Removing `precise` reduced Mali's
differences but also introduced differences on Lenovo, so that experiment is
not retained. This remains a shader precision issue to resolve, and these
measurements are not proof of complete rendered-screen parity.

`page_turn_bottom_vertex_shader()` and `page_turn_bottom_fragment_shader()`
implement the lower surface: its shallow curl, vertex texture lookup, RGB shadow
modulation, unchanged image alpha, UV and projection transforms. The vertex
parameters retain the original 12-vec4 layout. The lookup is supplied on texture
unit 4; the page image uses fragment unit 0. The fragment uses a high-precision
sampler followed by explicit half-register boundaries; lowering the sampler
itself introduced additional 8-bit output differences on Lenovo.

The bottom vertex transform-feedback check compares **10,872 float words bit
for bit on both Lenovo and RG556**, with no nonfinite results, across the same
four folds and three projection/scale configurations. A deliberately nonuniform
64-texel test lookup exercises filtering and binding; it is not presented as the
original exponential lookup asset. The 16 bottom fragment cases vary shadow
strength across filtered nonuniform image samples. On Lenovo all **524,288
RGBA8 channels match** the original translation. On Mali, **12,855 channels still
differ by at most 1/255**, with alpha matching exactly; the strict comparison
returns nonzero. Complete material integration and original lookup preparation
remain pending.

`page_turn_top_fragment_shader()` now reconstructs the upper material's tint,
back opacity, lighting addition and fused final blend, preserving the original
nine-vec4 fragment layout and half-register write boundaries. The 16 filtered
texture/light/opacity cases match all **524,288 RGBA8 channels on Lenovo**. On
Mali the strict comparison still reports **1,343 differing channels, maximum
1/255**. This known discrepancy is not accepted as material parity.

`page_turn_quad_vertex_shader()` and `page_turn_shadow_fragment_shader()` cover
the rectangle and outer shadow passes. The rectangle consumes position/UV pairs
and the original five-vec4 transform layout. Its **10,872 transform-feedback
words match bit for bit on Lenovo and RG556**. The shadow retains packed half
border/level parameters in its first vec4 and ordinary RGBA in the second;
callers must preserve these bit patterns. It reconstructs absolute-coordinate
border mapping, clipping and the sampled shadow/color blend. All **524,288
RGBA8 channels match the original shadow translation on both devices** for the
16 varied-level/border cases.

The bottom vertex comparisons also support `--lookup-r8 LOCAL_128x8_BYTES`.
Using the bytes decoded from the prepared exponential PNG (with its manifest
hash checked), and range/reciprocal 5/0.2, the flat and rounded bottom stages
match **10,872 and 16,308 words respectively on both Lenovo and RG556**. This
checks sampling the actual prepared table in addition to the deliberately
nonuniform synthetic lookup; it does not complete the material controller.

The GLES renderer now accepts an optional third shader pair and submits four
explicit position/UV vertices through `draw_quad()`. Construction compiles all
configured pairs; draw calls only update existing buffers. Material UBO members
have separate vertex/fragment instance names: testing actual pairs exposed a
link conflict that compiling each stage with a diagnostic counterpart missed.
The device check now links all three real pairs together, submits an actual
shadow rectangle, and verifies all **8,192 output pixels** against its expected
constant material on Lenovo and RG556. These are component checks; rounded-edge
passes, complete material parameters/lookup assets and lock-screen integration
are still pending.

All 13 reviewed PageTurn GPU programs now have native source implementations:
six vertex stages and seven fragment stages, linked into the seven original
stage pairings by the device component check. Flat and rounded vertex variants
share compile-time source bodies; the variants are assembled as constant arrays,
with no runtime string allocation or original-program translation. Rounded
variants retain their additional signed corner coordinates and the original
fused-operation differences. The rounded materials implement corner sampling,
UV refraction, edge highlights and coverage alpha.

Current additional component comparisons (each fragment set contains 524,288
RGBA8 channels unless stated otherwise):

| Component | Lenovo | RG556 |
| --- | --- | --- |
| Rounded bottom vertex, 16,308 words | Bit-exact | Bit-exact |
| Rounded bottom fragment | 4,495 differing channels, max 1/255 | 7,375 differing channels, max 1/255 |
| Rounded top vertex, 21,744 words | Bit-exact | 2,148 differing words, max 0.0000193417 |
| Rounded top fragment | Exact RGBA8 | 3,393 differing channels, max 1/255 |
| Empty-back vertex, 16,308 words | Bit-exact | Bit-exact |
| Pure copy, 32,768 channels | Exact RGBA8 | Exact RGBA8 |

The empty-back vertex preserves the original exponential shadow falloff and
the explicit NaN-to-one exponential behavior. Pure copy preserves the image's
half-register output boundaries. The new pairs also undergo joint compilation,
in addition to separate transform-feedback and fragment comparisons. Tests of
the shared flat top/bottom bodies still pass their existing exact checks on
Lenovo/RG556 respectively. Remaining precision discrepancies are reported as
failures by the strict comparator. Having all stage sources does not complete
the screen: material assembly, original lookup images, pass ordering, the full
gesture controller and original-frame comparison remain required.

```text
paf_page_shader_test
paf_page_shader_test LOCAL_ORIGINAL_EMPTY_BACK_FRAGMENT.frag
paf_page_shader_test --top-vertex LOCAL_ORIGINAL_TOP_VERTEX.vert
paf_page_shader_test --bottom-vertex LOCAL_ORIGINAL_BOTTOM_VERTEX.vert
paf_page_shader_test --bottom-fragment LOCAL_ORIGINAL_BOTTOM_FRAGMENT.frag
paf_page_shader_test --top-fragment LOCAL_ORIGINAL_TOP_FRAGMENT.frag
paf_page_shader_test --quad-vertex LOCAL_ORIGINAL_QUAD_VERTEX.vert
paf_page_shader_test --shadow-fragment LOCAL_ORIGINAL_SHADOW_FRAGMENT.frag
paf_page_shader_test --bottom-round-vertex LOCAL_ORIGINAL_BOTTOM_ROUND_VERTEX.vert
paf_page_shader_test --bottom-round-fragment LOCAL_ORIGINAL_BOTTOM_ROUND_FRAGMENT.frag
paf_page_shader_test --top-round-vertex LOCAL_ORIGINAL_TOP_ROUND_VERTEX.vert
paf_page_shader_test --top-round-fragment LOCAL_ORIGINAL_TOP_ROUND_FRAGMENT.frag
paf_page_shader_test --empty-back-vertex LOCAL_ORIGINAL_EMPTY_BACK_VERTEX.vert
paf_page_shader_test --copy-fragment LOCAL_ORIGINAL_COPY_FRAGMENT.frag
paf_page_shader_test --bottom-vertex LOCAL_ORIGINAL_BOTTOM_VERTEX.vert --lookup-r8 LOCAL_EXP_MINUS.r8
```

### Native shape-material assembly

`page_turn_shape_material()` now assembles the common `Deepest`, `Grep`,
`Params`, `FoldPlane`, `Aspect` and normal-scale uniforms from the native fold
shape and style. `Deepest` contains the signed distance extrema and far corner;
`Grep` retains the input fold point. The builder preserves the original small-fold
curl fade and progress-dependent Fresnel multiplier, including negative progress
and the original upper-only clamp. Default shape style values retain the original
constructor's float bit patterns.

`page_turn_normalized_aspect()` retains the original reciprocal-estimate,
refinement and multiplication sequence. Both dimensions are multiplied by the
inverse height: the resulting Y is not forcibly replaced with `1.0`. The original
1234x567 case produces `0.9999998807907104`; preserving this matters to the plane
and material parameters.

The offline corpus executes the original renderer's parameter-assembly entry,
retaining fold-plane, quaternion/matrix, lighting and material-adaptation math.
Graphics/surface submission, uniform setters and mesh submission are hosted;
mesh arithmetic is checked separately. Captured setter arguments are compared
to the native builder for **80 records / 1,680 words, bit-exact on Lenovo and
RG556**. Four page sizes, two controlled styles and ten fold points cover near
rest, saturation, negative progress and out-of-page inputs. The comparison also
checks the resulting two Fresnel coefficients after the multiplier is applied.
This proves the shared shape-material block; pose/light assembly is checked
separately below. Complete texture assembly, actual shell configuration and
rendered-frame parity remain unproven. The preview
lock-screen integration remains pending.

```text
python native-ui/tests/generate_page_material_reference.py OWNED_DECRYPTED_PAF LOCAL_MATERIAL_CORPUS.bin
paf_page_material_test LOCAL_MATERIAL_CORPUS.bin
```

### Native page pose and point light

`page_turn_material_pack()` assembles the complete 16-float original material
block from caller-supplied color and nine coefficients. Constructor defaults
retain their exact float bits; the two Fresnel coefficients follow the recovered
fold-progress multiplier. The top-vertex builder now includes this block instead
of leaving it zero. An independent capture of the original renderer's setter
matches **48 records / 768 words bit for bit on Lenovo and RG556**, covering four
sizes, two fold styles, two material sets and three fold states. This establishes
parameter assembly, not the unfinished lock-screen camera/composition or pixels.

```text
python native-ui/tests/generate_page_material_pack_reference.py OWNED_DECRYPTED_PAF LOCAL_PACK_CORPUS.bin
paf_page_material_pack_test LOCAL_PACK_CORPUS.bin
```

`page_turn_transforms()` assembles column-major model/MVP matrices from the
physical page size, scale, center position, quaternion and view/projection.
It retains the original quaternion normalization, zero-quaternion identity,
origin placement, height-normalized axes and non-fused matrix operation order.
**60 original CPU-renderer records / 1,920 matrix words match bit for bit on
Lenovo and RG556**, across four sizes, five poses and three camera configurations.

`page_turn_light_parameters()` builds the position/attenuation and
ambient/diffuse/fade uniform blocks. The Y-up spherical conversion reproduces
the original PAF sine/cosine polynomials, range reduction and quadrant handling;
it does not substitute platform libm. The zero-attenuation fallback is retained.
Constructor defaults preserve their original float bits. **105 original
CPU-renderer records / 3,360 light words match bit for bit on both devices**,
including default angles/distance, positive/negative/wrapped angles, signed zero,
polynomial/quadrant boundaries, zero distance and varied attenuation/colors/fade.
The shared material corpus remains byte-identical and still passes all 80 cases.

All these builders use caller-owned values with no allocation, I/O, guest execution
or graphics calls. Their offline reference runner hosts graphics submission only;
the original pose and lighting math executes unchanged. These results establish
CPU parameter equivalence for the recorded cases; full material binding, pass
ordering, shell layout and screen transitions still need integration.

```text
python native-ui/tests/generate_page_transform_reference.py OWNED_DECRYPTED_PAF LOCAL_POSE_CORPUS.bin
paf_page_transform_test LOCAL_POSE_CORPUS.bin
python native-ui/tests/generate_page_light_reference.py OWNED_DECRYPTED_PAF LOCAL_LIGHT_CORPUS.bin
paf_page_light_test LOCAL_LIGHT_CORPUS.bin
```

### Recovered PageTurn GPU program interfaces

The offline GXP inventory retains each program's reflected parameter names,
component counts, float/half type and packed uniform-slot offset. The reviewed
PageTurn programs are now identified by role. Addresses are local reference
labels, not runtime constants; the delivered C++/GLSL keeps no firmware address.

| Role | Uniform block in slot order (float-slot offset) |
| --- | --- |
| Top surface vertex (flat and rounded) | MVP 0, ModelMatrix 16, MaterialPack 32, UVTransform 48, SqueezeZ 52, Deepest 56, Grep 60, NormalScaleZ 62, Params 64, FoldPlane 68, Aspect 72, PointLightPack2 76, PointLightPack1 92, Eye 108 |
| Bottom surface vertex (flat and rounded) | MVP 0, Shadow 16, UVTransform 20, SqueezeZ 24, ExpMinusRange 28, Deepest 30, Grep 34, Params 36, FoldPlane 40, Aspect 44 |
| Empty folded-back vertex | MVP 0, Shadow 16, UVTransform 20, SqueezeZ 24, FoldPlane 28, Aspect 32 |
| Rectangle (quad) vertex | MVP (half) 0, UVOffset (half2) 8, UVScale (half2) 10 |
| Top fragment (flat) | BackSideOpacity 0, MaterialPack 2, PointLightPack2 18 |
| Top fragment (rounded) | CornerSize 0, BackSideOpacity 4, MaterialPack 6, PointLightPack2 22 |
| Bottom fragment (flat) | No own material block; samples the page texture through the shared draw state |
| Bottom fragment (rounded) | CornerSize 0, CornerRefract 4 |
| Empty-back fragment | CornerSize 0, EmptyShadow 4, EmptyColor 5, EmptyShadowColor 9, EmptyBackgroundUVFactor 13 |
| Shadow fragment | Border (half4) 0, Level (half) 2, Color 4 |
| Copy fragment | Texture only; its program is not uniquely separated from the other texture-only programs yet |

These offsets agree with the native material array layouts already exercised by
the device shader comparisons (28/12/9/5-vec4 and the small fragment blocks), so
each native field now has a recovered original name. They do **not** complete the
renderer: the `Shadow`, `UVTransform`, `SqueezeZ`, `CornerSize`, `CornerRefract`,
`ExpMinusRange` and lighting-pack values still need a native material controller,
and pass ordering, texture identity and blend states require the full draw
protocol capture described below.

The offline PageTurnRenderer constructor was also reviewed. It allocates the
renderer bundle used by the traces, creates the program objects at the bundle
offsets seen in the draw protocol, and registers every material, style and light
default. Its defaults are the original bit patterns: diffuse/specular/fresnel/
refraction coefficients, both point lights, top/bottom scale and curl, shadow and
shadow level, normal scale, squeeze, empty-back colour and occluder shadow
constants, plus the `page_turn/textures/page_corner.dds` corner texture. The
same review recovered the registered material parameter order for every pass
(e.g. top vertex: MVP, ModelMatrix, Grep, FoldPlane, Params, Deepest, Aspect,
NormalScaleZ, Eye, MaterialPack, PointLightPack2, PointLightPack1, UVTransform,
SqueezeZ). That order resolves the observed 17/20-call setter sequences to
uniform semantics; the earlier verified builders already supply the matrix,
shape, Fresnel and light values. Value assembly for the remaining slots and the
shell's own page size/pose configuration are still pending.

An offline trace runs the original renderer entry in a hosted graphics
environment and records the ordered protocol: surface activation and texture
binds, per-pass matrix/shape/light setter blocks, program binds, render-state
changes and draw submissions. Supplying the draw-info structures makes the
renderer submit its real draws: a flat page produces 74 material blocks, four
program binds and two surface draws; a folded page produces 111 blocks, six
program binds and five draws (three surfaces). The renderer prepares every
surface first and then binds and submits them in order, so each recorded
vertex/fragment program object can be attributed to one material set. The
material setter API is fixed-slot (17 vertex / 20 fragment calls per surface),
which means the next reconstruction pass must map those slots to the reflected
uniform offsets above. This is a research generator only. The standalone preview
scenes and existing device gates are unaffected.

```text
python native-ui/tests/trace_page_draw_protocol.py OWNED_DECRYPTED_LIBPAF OUTPUT_TRACE.json [--fold]
```

The tracer validates the reviewed reference SHA-256 and refuses an unknown
image. It supplies only the controlled renderer input state; the original
renderer computes the recorded protocol itself. With `--fold` the fixture's
non-zero fold point selects the rounded-edge material set. Its output does not
establish the shell's real lock-screen configuration, texture identity or
rendered-frame parity, and no trace enters the application.

The traced setter blocks are cross-checked against the already bit-exact
material, pose and light oracles for the same fixture. Five fixtures across three page sizes and both material paths match all 17
traced vertex slots: MVP, ModelMatrix, shape.Grep,
shape.FoldPlane, shape.Params, shape.Deepest, shape.Aspect,
shape.NormalScaleZ, the two MaterialPack Fresnel cells and both point-light
packs. This proves the recovered setter-slot -> uniform mapping and the
compatibility of the tracer fixture with the earlier corpora; it does not add
renderer behaviour.

```text
python native-ui/tests/verify_page_material_slots.py OWNED_DECRYPTED_LIBPAF
```

The reviewed material registration order and the renderer-constructor defaults
(exact IEEE-754 bit patterns for the material pack, both point lights and every
style/empty-back constant, plus the corner texture location) are recorded in
`tests/reference/page_turn_material_order.json` for the native material
controller. It is a local research reference; no firmware data is packaged.

```text
python native-ui/tests/run_offline_checks.py OWNED_DECRYPTED_LIBPAF
```

This harness runs the reference consistency check and, when a decrypted
reference is supplied, the five-fixture draw-trace slot cross-check. Without a
reference it reports the device-free check only; it never touches a device.

The recovered uniform offsets live in the standalone `page_turn_material`
module: `paf_page_material_layout_test` checks that the top (14 uniforms,
112 floats), bottom (10/48) and empty-back (6/36) blocks have no overlapping
ranges, that their counted prefix ends at the block size (including the
one-float `NormalScaleZ` padding) and that undeclared uniforms report empty
ranges. It passed on Anbernic RG556; it reads no firmware and starts no guest.

```text
paf_page_material_layout_test
```

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

### Recovered PlaneObj div / 9-slice layout

`emucorev::paf::plane_div` reconstructs `PlaneObj::UpdateLayout` (`0x810848e8`),
`FUN_81083892` (`scale_mode=0`), `PlaneObj::UpdateDivVerticesPosition` (`0x81084b1a`),
`PlaneObj::UpdateDivVerticesTexcoord` (`0x810852c4`), `Surface` div presets
(`0x8108d0d8`), RCO texture margin mapping (`0x81057856`, where `margin_v` stores
bottom at `+0x8e` and top at `+0x92`), and the ARMv7 `vrecpe.f32` + 2x `vrecps.f32`
reciprocal refinement. All 10 div modes (`0`–`9`), border overflow clamping,
`<= 1.0` collapse guard, `3x5`/`5x3` stretch gap/offset behavior, and index tables
from `DAT_8129aaac`/`DAT_8129ab1c` are implemented in `emucorev_paf`.

```text
python native-ui/tests/generate_plane_div_reference.py
paf_plane_div_test artifacts/plane-div-corpus.bin
```

All 15 Unicorn cases pass bit-for-bit (`layout_pos`, `layout_size`, and all vertex
`(x, y, u, v)` words) on Lenovo TB710FU (`HA28QVYS`). The LiveArea gate chrome
(`0xca6a6db1`, `74x82`, `div_mode=3x3`, `div_ratio=0.487800002`, `margin_h=5`,
`margin_v=9` around the `292x170` RCO panel) renders with `build_plane_div_screen_quads`
and `SpriteRenderer::draw_patch`.

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
