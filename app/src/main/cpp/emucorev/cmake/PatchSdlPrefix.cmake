# EmuCoreV adapter layer (Layer 2).
#
# Vanilla Vita3K's vendored SDL3 hardcodes SDL_JAVA_PREFIX inside two TUs
# (SDL_android.c and hidapi/android/hid.cpp). To keep the vita3k tree
# untouched, we materialise patched copies into the build directory and swap
# them into SDL3-shared / SDL3-static SOURCES.

function(emucorev_patch_sdl_prefix vita3k_core_dir vendor_dir)
    if(NOT TARGET SDL3-shared AND NOT TARGET SDL3-static)
        return()
    endif()

    set(_patch_dir "${CMAKE_BINARY_DIR}/emucorev_patches/sdl")
    file(MAKE_DIRECTORY "${_patch_dir}")

    set(_sdl_root "${vendor_dir}/external/sdl")
    set(_originals
        "${_sdl_root}/src/core/android/SDL_android.c"
        "${_sdl_root}/src/hidapi/android/hid.cpp"
    )

    set(_orig_dirs "")

    foreach(_orig IN LISTS _originals)
        get_filename_component(_name "${_orig}" NAME)
        get_filename_component(_orig_dir "${_orig}" DIRECTORY)
        list(APPEND _orig_dirs "${_orig_dir}")
        set(_patched "${_patch_dir}/${_name}")

        # Materialise the patched copy only when missing or stale.
        if(NOT EXISTS "${_patched}" OR "${_orig}" IS_NEWER_THAN "${_patched}")
            file(READ "${_orig}" _content)
            # JNI export macro form: Java_com_sbro_emucorev_core_sdl_SDLActivity_*
            string(REPLACE
                "com_sbro_emucorev_core_sdl"
                "org_libsdl_app"
                _content "${_content}")
            # FindClass / RegisterNatives form: "com/sbro/emucorev/core/sdl/SDLActivity"
            string(REPLACE
                "com/sbro/emucorev/core/sdl"
                "org/libsdl/app"
                _content "${_content}")
            file(WRITE "${_patched}" "${_content}")
        endif()

        foreach(_target IN ITEMS SDL3-shared SDL3-static SDL3-collector)
            if(TARGET "${_target}")
                get_target_property(_sources "${_target}" SOURCES)
                if(_sources)
                    list(TRANSFORM _sources REPLACE "${_orig}" "${_patched}")
                    set_target_properties("${_target}" PROPERTIES SOURCES "${_sources}")
                endif()
                get_target_property(_iface_sources "${_target}" INTERFACE_SOURCES)
                if(_iface_sources)
                    list(TRANSFORM _iface_sources REPLACE "${_orig}" "${_patched}")
                    set_target_properties("${_target}" PROPERTIES INTERFACE_SOURCES "${_iface_sources}")
                endif()
            endif()
        endforeach()
    endforeach()

    # Patched TUs sit in the build tree and lose their `#include "neighbour.h"`
    # resolution. Re-add the original directories so the relocated copies still
    # see SDL_android.h and hid.h.
    list(REMOVE_DUPLICATES _orig_dirs)
    foreach(_target IN ITEMS SDL3-shared SDL3-static)
        if(TARGET "${_target}")
            target_include_directories("${_target}" PRIVATE ${_orig_dirs})
        endif()
    endforeach()
endfunction()
