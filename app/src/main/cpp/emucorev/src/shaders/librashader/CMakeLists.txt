# librashader вЂ” RetroArch (.slangp) shader chains for Vita Vulkan presentation.
#
# librashader is a RUST project, so it can't be vendored the way the C/C++ 3rdparty deps
# are: this repo keeps no git submodules and no checked-in binaries. Instead we fetch a
# PINNED commit at configure time and cargo-build its C-ABI library for the ABI being
# built. Nothing lands in git, and the pin means the build stays reproducible.
#
# Its C-ABI crate is a LEAF crate (crate-type = cdylib/staticlib, no rlib), so it cannot
# be pulled in as a normal cargo dependency from a small wrapper вЂ” it has to be built
# from its own source tree via --manifest-path. That's why we fetch the whole repo.
#
# RUST IS OPTIONAL. If cargo isn't installed we simply don't create the target and leave
# EMUCOREV_HAVE_LIBRASHADER off, so every existing build (and any contributor without a
# Rust toolchain) keeps working exactly as before вЂ” the shader chain compiles out via
# #ifdef rather than breaking the build.
#
# This target builds only librashader's Vulkan runtime (`runtime-vulkan`). The Vita
# presentation path is Vulkan-backed; it does not claim support for OpenGL or GLES.

set(LIBRASHADER_PIN "87e8a97b50516d997defeaa168173dcd185d4022"
    CACHE STRING "Pinned librashader commit. Bump deliberately вЂ” never track a branch.")

# ---- Rust toolchain (optional) ---------------------------------------------------
find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{HOME}/.cargo/bin" "$ENV{CARGO_HOME}/bin")
if(NOT CARGO_EXECUTABLE)
	message(STATUS "librashader: cargo not found -> RetroArch shader support DISABLED. "
	               "Install Rust (https://rustup.rs) to enable it.")
	set(EMUCOREV_HAVE_LIBRASHADER OFF PARENT_SCOPE)
	return()
endif()

# ---- Android ABI -> Rust target ---------------------------------------------------
if(ANDROID_ABI STREQUAL "arm64-v8a")
	set(_lrs_rust_target "aarch64-linux-android")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
	set(_lrs_rust_target "armv7-linux-androideabi")
elseif(ANDROID_ABI STREQUAL "x86_64")
	set(_lrs_rust_target "x86_64-linux-android")
elseif(ANDROID_ABI STREQUAL "x86")
	set(_lrs_rust_target "i686-linux-android")
else()
	message(STATUS "librashader: unmapped ANDROID_ABI '${ANDROID_ABI}' -> DISABLED.")
	set(EMUCOREV_HAVE_LIBRASHADER OFF PARENT_SCOPE)
	return()
endif()

# ---- Host Rust toolchain (Windows fallback) ---------------------------------------
# cargo compiles every crate's build script for the HOST, even when the cdylib itself is
# cross-built for Android. The rustup default on Windows is *-pc-windows-msvc, whose
# linker is link.exe from Visual Studio's "Desktop development with C++" workload. When
# that workload is missing (or simply not on PATH for the Gradle daemon) EVERY build
# script dies on that one missing binary, and ninja reports it as a dozen unrelated
# "could not compile <crate> (build script) due to 1 previous error" lines that hide the
# real cause.
#
# rustup's GNU toolchain needs no Visual Studio at all: it links against the rust-mingw
# import libraries and rust-lld that rustup itself installs. So when the default
# toolchain cannot link a trivial host binary, fall back to an installed GNU toolchain
# that also has the std for the Android target being built. Force a specific toolchain
# with -DEMUCOREV_RUST_TOOLCHAIN=<rustup toolchain name>.
set(EMUCOREV_RUST_TOOLCHAIN "" CACHE STRING
	"rustup toolchain for librashader's host build scripts. Empty = auto-detect.")
set(_lrs_rustup_env "")
set(_lrs_cargo_path "")
set(_lrs_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/host-linker-probe")
set(_lrs_probe_error "")

if(EMUCOREV_RUST_TOOLCHAIN)
	set(_lrs_rustup_env "RUSTUP_TOOLCHAIN=${EMUCOREV_RUST_TOOLCHAIN}")
	message(STATUS "librashader: host Rust toolchain pinned to ${EMUCOREV_RUST_TOOLCHAIN}")
elseif(CMAKE_HOST_WIN32)
	get_filename_component(_lrs_cargo_bin_dir "${CARGO_EXECUTABLE}" DIRECTORY)
	find_program(RUSTC_EXECUTABLE rustc HINTS "${_lrs_cargo_bin_dir}" "$ENV{USERPROFILE}/.cargo/bin")
	find_program(RUSTUP_EXECUTABLE rustup HINTS "${_lrs_cargo_bin_dir}" "$ENV{USERPROFILE}/.cargo/bin")

	if(NOT RUSTC_EXECUTABLE)
		message(FATAL_ERROR "librashader: cargo was found at ${CARGO_EXECUTABLE} but rustc was "
			"not. Repair the Rust installation (https://rustup.rs) or take cargo off PATH.")
	endif()

	file(MAKE_DIRECTORY "${_lrs_probe_dir}")
	file(WRITE "${_lrs_probe_dir}/probe.rs" "fn main() {}\n")
	execute_process(
		COMMAND "${RUSTC_EXECUTABLE}" --crate-name librashader_host_probe --emit=link
			--out-dir "${_lrs_probe_dir}" "${_lrs_probe_dir}/probe.rs"
		RESULT_VARIABLE _lrs_probe_status
		OUTPUT_QUIET
		ERROR_VARIABLE _lrs_probe_error
	)

	if(NOT "${_lrs_probe_status}" STREQUAL "0")
		execute_process(
			COMMAND "${RUSTC_EXECUTABLE}" -vV
			OUTPUT_VARIABLE _lrs_rustc_version
			ERROR_QUIET
		)
		string(REGEX MATCH "host: ([A-Za-z0-9_.-]+)" _lrs_host_match "${_lrs_rustc_version}")
		set(_lrs_host_triple "${CMAKE_MATCH_1}")

		# msvc is the only Windows host flavour whose linker lives outside rustup.
		set(_lrs_fallback_triples "")
		foreach(_lrs_suffix gnu gnullvm)
			string(REPLACE "-msvc" "-${_lrs_suffix}" _lrs_candidate "${_lrs_host_triple}")
			if(NOT _lrs_candidate STREQUAL _lrs_host_triple)
				list(APPEND _lrs_fallback_triples "${_lrs_candidate}")
			endif()
		endforeach()

		set(_lrs_gnu_toolchain "")
		if(RUSTUP_EXECUTABLE)
			execute_process(
				COMMAND "${RUSTUP_EXECUTABLE}" toolchain list
				OUTPUT_VARIABLE _lrs_rustup_toolchains
				ERROR_QUIET
			)
			foreach(_lrs_candidate IN LISTS _lrs_fallback_triples)
				string(REGEX MATCH "([A-Za-z0-9._-]+-${_lrs_candidate})([^A-Za-z0-9._-]|$)"
					_lrs_toolchain_match "${_lrs_rustup_toolchains}")
				if(_lrs_toolchain_match)
					set(_lrs_toolchain "${CMAKE_MATCH_1}")
					execute_process(
						COMMAND "${RUSTUP_EXECUTABLE}" target list --toolchain "${_lrs_toolchain}" --installed
						OUTPUT_VARIABLE _lrs_toolchain_targets
						ERROR_QUIET
					)
					string(REGEX MATCH "(^|\n)${_lrs_rust_target}\r?(\n|$)" _lrs_toolchain_target_match
						"${_lrs_toolchain_targets}")
					if(_lrs_toolchain_target_match)
						set(_lrs_gnu_toolchain "${_lrs_toolchain}")
						break()
					endif()
				endif()
			endforeach()
		endif()

		if(_lrs_gnu_toolchain)
			# Only trust the fallback once it has proven it can link a host binary itself.
			execute_process(
				COMMAND "${CMAKE_COMMAND}" -E env "RUSTUP_TOOLCHAIN=${_lrs_gnu_toolchain}"
					"${RUSTC_EXECUTABLE}" --crate-name librashader_host_probe --emit=link
					--out-dir "${_lrs_probe_dir}" "${_lrs_probe_dir}/probe.rs"
				RESULT_VARIABLE _lrs_fallback_status
				OUTPUT_QUIET
				ERROR_VARIABLE _lrs_fallback_error
			)
			if("${_lrs_fallback_status}" STREQUAL "0")
				set(_lrs_rustup_env "RUSTUP_TOOLCHAIN=${_lrs_gnu_toolchain}")
				message(STATUS "librashader: '${_lrs_host_triple}' cannot link host build scripts "
					"(no Visual Studio C++ tools) -> using ${_lrs_gnu_toolchain}")
			else()
				set(_lrs_probe_error "${_lrs_probe_error}\n${_lrs_fallback_error}")
			endif()
		endif()

		if(NOT _lrs_rustup_env)
			set(_lrs_fix_hint "  * install the Visual Studio \"Desktop development with C++\" workload, or\n")
			foreach(_lrs_candidate IN LISTS _lrs_fallback_triples)
				string(APPEND _lrs_fix_hint
					"  * run: rustup toolchain install ${_lrs_candidate}"
					" && rustup target add ${_lrs_rust_target} --toolchain ${_lrs_candidate}\n")
			endforeach()
			string(APPEND _lrs_fix_hint
				"  * or build without RetroArch shaders by taking cargo off PATH.\n")
			message(FATAL_ERROR
				"librashader: cargo is installed but the Rust host toolchain cannot link native "
				"build scripts.\n${_lrs_probe_error}\ncargo needs that linker for build scripts. "
				"Fix it by any of:\n${_lrs_fix_hint}"
				"Also possible: -DEMUCOREV_RUST_TOOLCHAIN=<rustup toolchain> to force a toolchain.")
		endif()
	endif()
endif()

# ---- NDK toolchain paths for cargo's linker/cc ------------------------------------
# cargo drives its own linker, so hand it the same NDK clang CMake is using.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	set(_lrs_host_tag "darwin-x86_64")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	set(_lrs_host_tag "linux-x86_64")
else()
	set(_lrs_host_tag "windows-x86_64")
endif()
set(_lrs_ndk "${CMAKE_ANDROID_NDK}")
if(NOT _lrs_ndk)
	set(_lrs_ndk "$ENV{ANDROID_NDK_HOME}")
endif()
set(_lrs_tc "${_lrs_ndk}/toolchains/llvm/prebuilt/${_lrs_host_tag}")

# API level: ANDROID_PLATFORM looks like "android-26"; cargo's clang wrapper wants "26".
set(_lrs_api "${ANDROID_PLATFORM}")
string(REGEX REPLACE "^android-" "" _lrs_api "${_lrs_api}")
if(NOT _lrs_api)
	set(_lrs_api 26)
endif()

set(_lrs_cc "${_lrs_tc}/bin/${_lrs_rust_target}${_lrs_api}-clang")
# armv7 is the one target whose clang wrapper prefix != its rust triple.
if(_lrs_rust_target STREQUAL "armv7-linux-androideabi")
	set(_lrs_cc "${_lrs_tc}/bin/armv7a-linux-androideabi${_lrs_api}-clang")
endif()

set(_lrs_cxx "${_lrs_cc}++")

if(CMAKE_HOST_WIN32)
	if(EXISTS "${_lrs_cc}.cmd")
		set(_lrs_cc "${_lrs_cc}.cmd")
	endif()
	if(EXISTS "${_lrs_cxx}.cmd")
		set(_lrs_cxx "${_lrs_cxx}.cmd")
	endif()
endif()

if(NOT EXISTS "${_lrs_cc}")
	message(STATUS "librashader: no NDK clang at ${_lrs_cc} -> DISABLED.")
	set(EMUCOREV_HAVE_LIBRASHADER OFF PARENT_SCOPE)
	return()
endif()

# cargo wants the triple upper-cased with underscores for its per-target linker var.
string(TOUPPER "${_lrs_rust_target}" _lrs_target_upper)
string(REPLACE "-" "_" _lrs_target_upper "${_lrs_target_upper}")
string(REPLACE "-" "_" _lrs_target_under "${_lrs_rust_target}")

# ---- dlltool for a GNU host --------------------------------------------------------
# rustc shells out to `dlltool` (bare name, resolved on PATH) whenever a crate links a
# system DLL through #[link(kind = "raw-dylib")], which getrandom and friends do. An
# msvc host uses lib.exe, but a GNU host needs one, and the GNU toolchain's copy in
# rustlib's self-contained bin dir cannot work here: it delegates to an `as` assembler
# that rustup's mingw bundle does not ship.
#
# The NDK already carries a self-contained equivalent (llvm-dlltool), so stage it under
# the name rustc looks for, next to the libwinpthread it is linked against, and prepend
# that directory to cargo's PATH. Only GNU hosts need this.
if(CMAKE_HOST_WIN32 AND _lrs_rustup_env MATCHES "gnu")
	set(_lrs_host_tools_dir "${CMAKE_CURRENT_BINARY_DIR}/host-tools")
	set(_lrs_dlltool_shim "${_lrs_host_tools_dir}/dlltool.exe")
	file(MAKE_DIRECTORY "${_lrs_host_tools_dir}")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lrs_tc}/bin/llvm-dlltool.exe"
			"${_lrs_dlltool_shim}"
	)
	if(NOT EXISTS "${_lrs_dlltool_shim}")
		message(FATAL_ERROR "librashader: expected llvm-dlltool at ${_lrs_tc}/bin, but it is "
			"missing. The NDK install looks incomplete.")
	endif()
	if(EXISTS "${_lrs_tc}/bin/libwinpthread-1.dll")
		execute_process(
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lrs_tc}/bin/libwinpthread-1.dll"
				"${_lrs_host_tools_dir}/libwinpthread-1.dll"
		)
	endif()

	# Smoke-test the shim with the exact argument shape rustc uses for raw-dylib: a shim
	# that cannot start (missing libwinpthread) would otherwise fail deep inside cargo
	# with an opaque CreateProcess error.
	set(_lrs_dlltool_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/dlltool-probe")
	file(MAKE_DIRECTORY "${_lrs_dlltool_probe_dir}")
	file(WRITE "${_lrs_dlltool_probe_dir}/probe.def"
		"LIBRARY probe.dll\nEXPORTS\n    probe_symbol\n")
	file(REMOVE "${_lrs_dlltool_probe_dir}/probe.lib")
	execute_process(
		COMMAND "${_lrs_dlltool_shim}" -d "${_lrs_dlltool_probe_dir}/probe.def" -D probe.dll
			-l "${_lrs_dlltool_probe_dir}/probe.lib" -m i386:x86-64 -f --64
			--no-leading-underscore --temp-prefix "probe.dll:"
		RESULT_VARIABLE _lrs_dlltool_probe_status
		OUTPUT_QUIET
		ERROR_VARIABLE _lrs_dlltool_probe_error
	)
	if(NOT EXISTS "${_lrs_dlltool_probe_dir}/probe.lib")
		message(FATAL_ERROR "librashader: the staged dlltool shim failed to create an import "
			"library:\n${_lrs_dlltool_probe_error}")
	endif()

	# PATH keeps its semicolons: escaped here so the whole value stays ONE argument to
	# `cmake -E env`, which is what puts the shim first for cargo/rustc.
	set(_lrs_cargo_path "PATH=${_lrs_host_tools_dir};$ENV{PATH}")
	string(REPLACE ";" "\\;" _lrs_cargo_path "${_lrs_cargo_path}")
	message(STATUS "librashader: staged a dlltool shim at ${_lrs_dlltool_shim}")
endif()

# ---- Fetch the pinned source (configure time, nothing vendored) --------------------
# librashader has no root CMakeLists, so point SOURCE_SUBDIR at a path that doesn't
# exist: that populates the tree WITHOUT trying to add_subdirectory() it.
include(FetchContent)
FetchContent_Declare(librashader_src
	GIT_REPOSITORY https://github.com/SnowflakePowered/librashader.git
	GIT_TAG        ${LIBRASHADER_PIN}
	SOURCE_SUBDIR  cmake-does-not-exist
)
FetchContent_MakeAvailable(librashader_src)

set(_lrs_out_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(_lrs_lib "${_lrs_out_dir}/${_lrs_rust_target}/release/liblibrashader_capi.so")

# ---- cargo build ------------------------------------------------------------------
# Shared (cdylib) rather than the 80 MB staticlib: keeps the Rust objects out of
# emucore's LTO/link entirely (no allocator/panic-handler or duplicate-symbol
# interplay with a huge C++ LTO link), at the cost of one extra ~13 MB .so.
add_custom_command(
	OUTPUT "${_lrs_lib}"
	COMMAND ${CMAKE_COMMAND} -E env
		${_lrs_rustup_env}
		${_lrs_cargo_path}
		"CARGO_TARGET_${_lrs_target_upper}_LINKER=${_lrs_cc}"
		"CC_${_lrs_target_under}=${_lrs_cc}"
		"CXX_${_lrs_target_under}=${_lrs_cxx}"
		"AR_${_lrs_target_under}=${_lrs_tc}/bin/llvm-ar"
		"RANLIB_${_lrs_target_under}=${_lrs_tc}/bin/llvm-ranlib"
		# rustc does NOT emit a SONAME for an Android cdylib. Without one the linker
		# records the full path we hand it as emucore's DT_NEEDED
		# ("3rdparty/librashader/cargo/.../liblibrashader_capi.so"), and at runtime
		# Android вЂ” which resolves NEEDED by bare name out of lib/<abi>/ вЂ” can't find
		# it, so the core fails to load. Stamp the soname so DT_NEEDED is just the
		# filename, exactly like a CMake-built .so.
		# Scope the soname flag to the Android triple: plain RUSTFLAGS would also reach
		# the host build scripts cargo compiles first, and a Windows msvc link.exe
		# cannot parse -Wl,... arguments.
		"CARGO_TARGET_${_lrs_target_upper}_RUSTFLAGS=-C link-arg=-Wl,-soname,liblibrashader_capi.so"
		${CARGO_EXECUTABLE} build --release
			--locked
			--manifest-path "${librashader_src_SOURCE_DIR}/librashader-capi/Cargo.toml"
			--target ${_lrs_rust_target}
			--target-dir "${_lrs_out_dir}"
			--no-default-features
			--features runtime-vulkan
	WORKING_DIRECTORY "${librashader_src_SOURCE_DIR}"
	COMMENT "librashader: cargo build (${_lrs_rust_target}, vulkan)"
	VERBATIM
)
add_custom_target(librashader_cargo DEPENDS "${_lrs_lib}")

# AGP only packages .so files that CMake itself emits into the per-ABI library output
# dir. Ours is produced by cargo behind an IMPORTED target, which AGP knows nothing
# about вЂ” without this copy the core links happily and then dies at runtime with
# UnsatisfiedLinkError. adrenotools doesn't need this because its hooks are real
# add_library(SHARED) targets.
#
# libc++_shared.so must ship ALONGSIDE it. The rest of EmuCoreV links libc++ STATICALLY, so
# nothing else in the app pulls libc++_shared.so in and AGP never packages it -- but cargo
# builds librashader's C++ deps (glslang / spirv-cross) against the SHARED runtime, so
# liblibrashader_capi.so records NEEDED libc++_shared.so. Miss this and the failure is
# catastrophic and totally unrelated-looking: emucore's whole dlopen chain fails
#   "library libc++_shared.so not found: needed by liblibrashader_capi.so"
# so EVERY JNI method disappears and the app dies on boot with UnsatisfiedLinkError on
# NativeApp.initialize -- even though nothing touched the shader feature.
# Safe despite the app's own static libc++: librashader's boundary is a pure extern "C" API,
# so no C++ objects or exceptions ever cross between the two runtimes.
# The sysroot lib dir is named after the *toolchain* triple, which matches the rust triple
# on every target except armv7 (rust: armv7-linux-androideabi / sysroot: arm-linux-androideabi),
# the same mismatch the clang-wrapper prefix above has to special-case.
set(_lrs_sysroot_triple "${_lrs_rust_target}")
if(_lrs_sysroot_triple STREQUAL "armv7-linux-androideabi")
	set(_lrs_sysroot_triple "arm-linux-androideabi")
endif()
set(_lrs_cxx_shared "${_lrs_tc}/sysroot/usr/lib/${_lrs_sysroot_triple}/libc++_shared.so")
if(NOT EXISTS "${_lrs_cxx_shared}")
	message(FATAL_ERROR "librashader: libc++_shared.so not found at ${_lrs_cxx_shared} -- "
		"liblibrashader_capi.so links it dynamically, so shipping without it bricks boot.")
endif()

if(CMAKE_LIBRARY_OUTPUT_DIRECTORY)
	add_custom_command(TARGET librashader_cargo POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_lrs_lib}" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_lrs_cxx_shared}" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/"
		COMMENT "librashader: staging liblibrashader_capi.so + libc++_shared.so for packaging"
		VERBATIM
	)
endif()

add_library(librashader SHARED IMPORTED GLOBAL)
set_target_properties(librashader PROPERTIES IMPORTED_LOCATION "${_lrs_lib}")
target_include_directories(librashader INTERFACE "${librashader_src_SOURCE_DIR}/include")
add_dependencies(librashader librashader_cargo)

set(EMUCOREV_HAVE_LIBRASHADER ON PARENT_SCOPE)
set(EMUCOREV_LIBRASHADER_SO "${_lrs_lib}" PARENT_SCOPE)
message(STATUS "librashader: ENABLED (${_lrs_rust_target}, api ${_lrs_api}, vulkan)")
