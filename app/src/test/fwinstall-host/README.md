# Firmware installer host tests (B030-B035, B037)

Host build of the EmuCoreV firmware installer (`app/src/main/cpp/emucorev/src/firmware_installer.cpp`) together with the
unchanged vendored `packages/pup.cpp` (the reference) and the patched `packages/sce_utils.cpp` / `exfat.cpp`.
Nothing here is part of the Android build. The real 3.74 PUPs are local-only and never committed.

    python run_fwinstall_host_tests.py --core <CORE_PSP2UPDAT.PUP> --fonts <PSP2UPDAT.PUP> [--reference <tree>] [--measure]

Needs a static OpenSSL 3.x (`libcrypto.a`; see `tools/oracles/install/README.md` for how it is built with llvm-mingw) and
the llvm-mingw toolchain on Windows. `--reference` is a tree produced by the vendored `install_pup` *before* the
change (CORE then FONTS); without it the vendored `install_pup` linked into the test binary builds the reference.

| test | proves |
|---|---|
| `install_matches_reference` | both real PUPs install, marker + key files present, progress monotonic to 100, whole tree (1723 files, `system-packages/*` with CR normalised: the vendored writer is text mode) byte-identical to the vendored installer; no `.fw_stage`/`.fw_old`/`PUP_DEC` left (B030, B031, B034) |
| `reinstall_same_process` | a second install in the same process gives the identical tree (the vendored `make_filename` kept a function-static counter, B033) |
| `not_a_pup` | zeros, empty file, broken magic -> `NOT_PUP`; missing file -> `IO_ERROR`; nothing written (B033) |
| `truncated` | cut at 60 %, at size-1, inside the table, inside the header -> `TRUNCATED`, empty root (B033) |
| `bad_records` | offset inside the header -> `CORRUPT`; offset/length past EOF or overflowing 2^64 -> `TRUNCATED`; absurd record count, SCE header length out of range, damaged encrypted metadata, damaged deflate stream -> `CORRUPT`; never a crash, never a partial tree (B033) |
| `failure_keeps_old_tree` | with firmware installed: exception from the progress callback, cancel during decrypt / extract / before the swap, an I/O error in the middle of the extraction, a failure *after* the partitions were swapped (marker cannot be written -> full rollback), injected low free space, stale staging dirs -> old tree byte-identical afterwards, nothing left behind (B030, B032, B037) |
| `recover_interrupted` | a crash between "old partition moved away" and "new one moved in" is rolled back, a finished-but-not-cleaned swap is just cleaned |

Not covered by an automated host test: a real ENOSPC from the file system (no way to create a size-limited volume
without admin rights on the host). The mapping `ENOSPC -> NO_SPACE` is in `fail_io()` and the `std::system_error` /
`boost::system::system_error` handlers; the preflight path (`available_bytes` seam) and the generic write-error path
(`IO_ERROR`, test above) are covered.

## Findings on the 3.74 packages (measured here)

* Segment count (B035): every partition package of both PUPs has exactly **one** AES-128-CTR segment (idx 3). The
  vendored `decrypt_segments` truncating its output per segment therefore never hurt these packages; the new installer
  appends all segments in order, so a multi-segment package would also work.
* The fonts PUP's `sa0` packages are deflated (`comp=1`); the base PUP's are not.
* The largest single record is 8.4 MB (vs0 parts), so the vendored installer's whole-record buffers never reached the
  "4-5x package" the review feared (below).

## Peak numbers (`--measure`, Windows host, empty Vita root, sampled every 10 ms)

| package | installer | peak RSS | peak size of the Vita root |
|---|---|---|---|
| base 133.8 MB | vendored `install_pup` | 31.9 MB | 375 MB (2.8x) |
| base 133.8 MB | staged installer | 8.7 MB | 231 MB (1.7x) |
| fonts 56.8 MB | vendored `install_pup` | 37.5 MB | 249 MB (4.4x) |
| fonts 56.8 MB | staged installer | 8.8 MB | 192 MB (3.4x) |

Replacing an existing partition keeps the old tree until the swap, which adds its size (about +113 MB base, +95 MB fonts).
The native preflight therefore asks for `2 * sum(partition) + largest + 32 MB` (estimate: decrypted bytes, doubled for
deflated segments): 363 MB for the base package, 344 MB for the fonts package.
