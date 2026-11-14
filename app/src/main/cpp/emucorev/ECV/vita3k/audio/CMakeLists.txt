add_library(
    audio
    STATIC
    src/audio.cpp
    src/audio_output.cpp
    src/impl/sdl_audio.cpp
    src/impl/cubeb_audio.cpp)

target_include_directories(audio PUBLIC include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../external/sdl/src/audio)
target_link_libraries(audio PRIVATE kernel util cubeb SDL3::SDL3)
