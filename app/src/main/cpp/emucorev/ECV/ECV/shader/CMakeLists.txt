add_library(
	shader
	STATIC
	src/translator/alu.cpp
	src/translator/ialu.cpp
	src/translator/branch_cond.cpp
	src/translator/data.cpp
	src/translator/special.cpp
	src/translator/illegal.cpp
	src/translator/texture.cpp
	src/translator/utils.cpp
	src/gxp_parser.cpp
	src/usse_dynamic_uniform.cpp
	src/usse_disasm.cpp
	src/usse_program_analyzer.cpp
	src/usse_decode_helpers.cpp
	src/usse_translator_entry.cpp
	src/usse_utilities.cpp
	src/spirv_recompiler.cpp
)

target_include_directories(shader PUBLIC include)
target_link_libraries(shader PUBLIC features gxm util)
target_link_libraries(shader PRIVATE SPIRV spirv-cross-glsl)

# Marshmallow Tracy linking
if(TRACY_ENABLE_ON_CORE_COMPONENTS)
	target_link_libraries(shader PRIVATE tracy)
endif()


# Identity of the translator for the on-disk shader cache: a hash of everything that decides what SPIR-V a
# program is translated to (the shader library, the GXM program parser, the variant key and the cache loader,
# and the vendored SPIR-V builder), computed at BUILD time so a translator change can never keep serving
# stale SPIR-V because someone forgot to bump shader::CURRENT_VERSION.
get_filename_component(ECV_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE TRANSLATOR_IDENTITY_INPUTS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*" "${CMAKE_CURRENT_SOURCE_DIR}/include/*"
	"${ECV_CORE_DIR}/gxm/src/*" "${ECV_CORE_DIR}/gxm/include/*"
	"${ECV_CORE_DIR}/features/include/*"
	"${ECV_CORE_DIR}/../external/glslang/SPIRV/*.cpp" "${ECV_CORE_DIR}/../external/glslang/SPIRV/*.h" "${ECV_CORE_DIR}/../external/glslang/SPIRV/*.hpp")
list(APPEND TRANSLATOR_IDENTITY_INPUTS
	"${ECV_CORE_DIR}/renderer/include/renderer/shader_variant.h"
	"${ECV_CORE_DIR}/renderer/src/shaders.cpp")
list(SORT TRANSLATOR_IDENTITY_INPUTS)
# The input list goes through a file: on Windows the full path list exceeds cmd.exe's 8191-character command line.
string(REPLACE ";" "
" TRANSLATOR_IDENTITY_INPUTS_TEXT "${TRANSLATOR_IDENTITY_INPUTS}")
set(TRANSLATOR_IDENTITY_LIST "${CMAKE_CURRENT_BINARY_DIR}/generated/translator_identity_inputs.txt")
file(WRITE "${TRANSLATOR_IDENTITY_LIST}.tmp" "${TRANSLATOR_IDENTITY_INPUTS_TEXT}
")
file(COPY_FILE "${TRANSLATOR_IDENTITY_LIST}.tmp" "${TRANSLATOR_IDENTITY_LIST}" ONLY_IF_DIFFERENT)
set(TRANSLATOR_IDENTITY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/translator_identity_generated.h")
add_custom_command(
	OUTPUT "${TRANSLATOR_IDENTITY_HEADER}"
	COMMAND ${CMAKE_COMMAND} "-DINPUTS_FILE=${TRANSLATOR_IDENTITY_LIST}" "-DROOT=${ECV_CORE_DIR}" "-DOUT=${TRANSLATOR_IDENTITY_HEADER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/translator_hash.cmake"
	DEPENDS ${TRANSLATOR_IDENTITY_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/translator_hash.cmake"
	COMMENT "Hashing shader translator sources"
	VERBATIM)
target_sources(shader PRIVATE src/translator_identity.cpp "${TRANSLATOR_IDENTITY_HEADER}")
set_source_files_properties(src/translator_identity.cpp PROPERTIES OBJECT_DEPENDS "${TRANSLATOR_IDENTITY_HEADER}")
target_include_directories(shader PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
