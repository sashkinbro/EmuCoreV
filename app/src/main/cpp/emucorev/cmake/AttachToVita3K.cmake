if(DEFINED EMUCOREV_VITA3K_LAYER_ATTACHED)
    return()
endif()
set(EMUCOREV_VITA3K_LAYER_ATTACHED TRUE)

if(NOT TARGET vita3k)
    message(FATAL_ERROR "EmuCoreV bridge must attach after the Vita3K target is created")
endif()

get_filename_component(EMUCOREV_BRIDGE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(VITA3K_VENDOR_DIR "${EMUCOREV_BRIDGE_DIR}/../vita3k" ABSOLUTE)
set(VITA3K_CORE_DIR "${VITA3K_VENDOR_DIR}/vita3k")

include("${EMUCOREV_BRIDGE_DIR}/cmake/CoreTargetFilter.cmake")
include("${EMUCOREV_BRIDGE_DIR}/cmake/PatchSdlPrefix.cmake")

# Rewrite vita3k's SDL TUs that hardcode SDL_JAVA_PREFIX = com_sbro_emucorev_core_sdl
# back to the upstream org_libsdl_app, without modifying the vita3k tree.
# Our Kotlin SDL classes now live in package org.libsdl.app to match.
emucorev_patch_sdl_prefix("${VITA3K_CORE_DIR}" "${VITA3K_VENDOR_DIR}")

target_include_directories(vita3k PRIVATE
    "${EMUCOREV_BRIDGE_DIR}/include"
    "${VITA3K_CORE_DIR}"
    "${VITA3K_CORE_DIR}/android/jni"
    "${VITA3K_CORE_DIR}/dialog/include"
)

target_compile_definitions(vita3k PRIVATE EMUCOREV_ANDROID_BRIDGE=1)

emucorev_filter_vita3k_target(vita3k "${VITA3K_VENDOR_DIR}" "${VITA3K_CORE_DIR}")

# Drop the upstream file-dialog JNI from vita3k's shared library: it exports
# Java_org_vita3k_emulator_Emulator_filedialogReturn, which our Kotlin
# Emulator (com.sbro.emucorev.core.vita) never calls. The replacement in
# host_dialog below owns both the host::dialog::filesystem implementation and
# our package's JNI symbol.
get_target_property(_emucorev_vita3k_sources vita3k SOURCES)
if(_emucorev_vita3k_sources)
    list(FILTER _emucorev_vita3k_sources EXCLUDE REGEX "android/jni/filesystem_android\\.cpp$")
    set_target_properties(vita3k PROPERTIES SOURCES "${_emucorev_vita3k_sources}")
endif()

# Replace host_dialog's Android source (the upstream copy was removed from the
# vita3k tree to keep it vanilla). host_dialog still publishes
# host::dialog::filesystem to the rest of vita3k.
if(TARGET host_dialog)
    set_target_properties(host_dialog PROPERTIES
        SOURCES "${EMUCOREV_BRIDGE_DIR}/src/file_dialog_bridge.cpp"
    )
endif()

set(EMUCOREV_BRIDGE_SOURCES
    "${EMUCOREV_BRIDGE_DIR}/src/layer_contract.cpp"
    "${EMUCOREV_BRIDGE_DIR}/src/vita_install_bridge.cpp"
    "${EMUCOREV_BRIDGE_DIR}/src/runtime_bridge.cpp"
)

target_sources(vita3k PRIVATE ${EMUCOREV_BRIDGE_SOURCES})
