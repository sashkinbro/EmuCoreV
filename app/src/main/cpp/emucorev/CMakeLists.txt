cmake_minimum_required(VERSION 3.22.1)
if(NOT TARGET ECV)
    message(FATAL_ERROR "EmuCoreV bridge must be added after the Vita3K target")
endif()
set(EMUCOREV_BRIDGE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(VITA3K_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/ECV")
set(VITA3K_CORE_DIR "${VITA3K_VENDOR_DIR}/ECV")
target_include_directories(ECV PRIVATE
    "${EMUCOREV_BRIDGE_DIR}/include"
    "${VITA3K_CORE_DIR}"
    "${VITA3K_CORE_DIR}/android/jni"
)
# Core static libraries that merge EmuCoreV input state need the adapter header.
target_include_directories(ctrl PRIVATE "${EMUCOREV_BRIDGE_DIR}/include")
target_include_directories(app PRIVATE "${EMUCOREV_BRIDGE_DIR}/include")
target_compile_definitions(ECV PRIVATE EMUCOREV_ANDROID_BRIDGE=1)
set(EMUCOREV_BRIDGE_SOURCES
    src/layer_contract.cpp
    src/vita_install_bridge.cpp
    src/runtime_bridge.cpp
    src/input_overlay_bridge.cpp
    src/input_state.cpp
    src/keyboard_mouse_bridge.cpp
)
target_sources(ECV PRIVATE ${EMUCOREV_BRIDGE_SOURCES})