get_filename_component(EMUCOREV_LAYER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

list(PREPEND CMAKE_MODULE_PATH "${EMUCOREV_LAYER_DIR}/cmake/modules")

if(NOT DEFINED EMUCOREV_VITA3K_PROJECT_HOOK_DEFERRED)
    set(EMUCOREV_VITA3K_PROJECT_HOOK_DEFERRED TRUE)
    cmake_language(
        DEFER
        DIRECTORY "${CMAKE_SOURCE_DIR}"
        CALL include
            "${EMUCOREV_LAYER_DIR}/cmake/AttachToVita3K.cmake"
    )
endif()
