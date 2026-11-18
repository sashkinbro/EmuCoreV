# Standalone native Vita UI reconstruction

This directory contains the independent C++ implementation. It has no emulator,
guest CPU, SDL, Android application or game runtime dependency. It is not yet a
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

Firmware binaries and extracted resources are local research inputs and are not
distributed in this directory. Resource/theme packaging, font selection, shader
reconstruction, layout, animation and the host bridge are still pending. No
approximate implementation is marked as equivalent.

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
The observed linear GIM texture variants are decoded as described below. Shader
reconstruction and the original draw pipeline are not implemented yet.

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

## Build and validation

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
