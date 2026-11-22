// Register layout of the fragment colour (o0 for native-colour programs, pa0 otherwise).
#pragma once

#include <gxm/types.h>
#include <shader/usse_types.h>

namespace shader {

// Data type the recompiler uses to read the final fragment colour from its register, or to preload o0 with
// the current surface colour (programmable blending).
//
// A non-native-colour program leaves its result in pa0 in the declared output type; the GXM shader patcher
// appends the conversion to the surface format, which the host emulates by reading the declared type and
// letting the attachment convert.
//
// A native-colour program (SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED) works on o0 directly. o0 is preloaded with
// the surface pixel in its raw register layout (a U8U8U8U8 pixel is one word of four bytes, read back with
// e.g. `OR r0.x o0.x` + VPCKF16U8), and the program normally leaves its result in that same layout (it packs
// itself, e.g. VPCKU8F16 + a raw f16 move into o0). Its declared output type (often half4) does not describe
// o0 then, so `surface_layout` must be true for byte-per-channel surfaces. Programs flagged
// SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF_OUTPUT leave unpacked half values in o0 instead (the pixel back end
// packs them), which is the declared type. Other surfaces keep the declared type (F16/F32 surfaces match it;
// packed formats are not modelled here).
inline usse::DataType fragment_color_register_type(usse::DataType declared, bool surface_layout, SceGxmColorFormat color_format, uint8_t output_comp_count) {
    // if the shader tries to write a INT32 or UINT32, it means the raw content
    if (declared == usse::DataType::INT32 || declared == usse::DataType::UINT32)
        declared = usse::DataType::F32;

    const auto base = static_cast<SceGxmColorBaseFormat>(color_format & SCE_GXM_COLOR_BASE_FORMAT_MASK);

    // if the output component count is greater than the surface component count,
    // it means we must pack multiple components (with lower precision) into one of the surface component
    // this is used in assassin creed 3
    if (base == SCE_GXM_COLOR_BASE_FORMAT_F32F32 && output_comp_count > 2 && declared == usse::DataType::F16)
        return usse::DataType::F32;

    if (surface_layout) {
        switch (base) {
        case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8:
        case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8:
            return usse::DataType::UINT8;
        case SCE_GXM_COLOR_BASE_FORMAT_S8S8S8S8:
            return usse::DataType::INT8;
        default:
            break;
        }
    }
    return declared;
}

// Layout of the colour a program leaves in its output register at the end, see fragment_color_register_type.
inline bool fragment_output_in_surface_layout(const SceGxmProgram &program) {
    return program.is_native_color() && !(program.program_flags & SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_HALF_OUTPUT);
}

} // namespace shader
