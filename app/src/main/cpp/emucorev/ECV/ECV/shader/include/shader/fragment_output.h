// Register layout of the fragment colour (o0 for native-colour programs, pa0 otherwise).
#pragma once

#include <gxm/types.h>

namespace shader {

// How the recompiler reads the final fragment colour from its register, or preloads o0 with the current
// surface colour (programmable blending).
//
// A non-native-colour program leaves its result in pa0 in the declared output type; the GXM shader patcher
// appends the conversion to the surface format, which the host emulates by reading the declared type and
// letting the attachment convert.
//
// A native-colour program (SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED) works on o0 directly. o0 is preloaded with
// the surface pixel in its raw register layout (a U8U8U8U8 pixel is one word of four bytes, read back with
// e.g. `OR r0.x o0.x` + VPCKF16U8), and the program normally leaves its result in that same layout (it packs
// itself, e.g. VPCKU8F16 + a raw f16 move into o0). Its declared output type (often half4) does not describe
// o0 then. Programs flagged SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF work on unpacked halves in o0 both ways, and
// SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF_OUTPUT ones leave unpacked halves there at the end (the pixel back end
// packs them); that is the declared type. Only byte-per-channel surfaces are remapped: F16/F32 surfaces match
// the declared type and packed formats are not modelled here.
enum class FragmentColorLayout {
    Declared, // the program's declared output type
    PackedU8, // four unsigned bytes in one word
    PackedS8, // four signed bytes in one word
};

inline FragmentColorLayout fragment_color_layout(bool surface_layout, SceGxmColorFormat color_format) {
    if (!surface_layout)
        return FragmentColorLayout::Declared;

    switch (static_cast<SceGxmColorBaseFormat>(color_format & SCE_GXM_COLOR_BASE_FORMAT_MASK)) {
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8:
        return FragmentColorLayout::PackedU8;
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8S8S8:
        return FragmentColorLayout::PackedS8;
    default:
        return FragmentColorLayout::Declared;
    }
}

// Whether o0 holds the surface pixel in its register layout when the program starts (programmable blending).
inline bool fragment_input_in_surface_layout(const SceGxmProgram &program) {
    return program.is_native_color() && !(program.program_flags & SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF);
}

// Whether o0 holds the surface pixel in its register layout when the program ends.
inline bool fragment_output_in_surface_layout(const SceGxmProgram &program) {
    return fragment_input_in_surface_layout(program) && !(program.program_flags & SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF_OUTPUT);
}

} // namespace shader
