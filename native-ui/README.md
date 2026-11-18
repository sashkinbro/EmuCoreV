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
Texture and shader decoding are not implemented here yet.

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
