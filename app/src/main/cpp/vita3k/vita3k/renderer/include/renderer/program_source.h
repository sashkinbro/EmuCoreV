// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <renderer/types.h>

namespace renderer {

// A queued compile must own the source it will parse, independently of tracking
// maps and of the guest allocation from which the program was originally made.
struct ProgramSource {
    std::shared_ptr<const std::vector<uint8_t>> owner;
    const SceGxmProgram *program = nullptr;

    const SceGxmProgram *get() const { return program; }
};

namespace detail {
template <typename Program>
inline const SceGxmProgram *get_wrapper_program(const Program &program, const MemState &mem) {
    if (program.renderer_data && program.renderer_data->program_source) {
        const auto &source = *program.renderer_data->program_source;
        return source.size() >= sizeof(SceGxmProgram)
            ? reinterpret_cast<const SceGxmProgram *>(source.data()) : nullptr;
    }
    return program.program.get(mem);
}
template <typename Program>
inline ProgramSource capture_wrapper_program(const Program &program, const MemState &mem) {
    return { program.renderer_data ? program.renderer_data->program_source : nullptr,
        get_wrapper_program(program, mem) };
}
} // namespace detail

// Only live vertex/fragment wrappers use retained source semantics. Registered
// program IDs and raw SceGxmProgram APIs deliberately retain guest semantics.
inline const SceGxmProgram *get_program(const SceGxmFragmentProgram &program, const MemState &mem) {
    return detail::get_wrapper_program(program, mem);
}
inline const SceGxmProgram *get_program(const SceGxmVertexProgram &program, const MemState &mem) {
    return detail::get_wrapper_program(program, mem);
}
inline ProgramSource capture_program_source(const SceGxmFragmentProgram &program, const MemState &mem) {
    return detail::capture_wrapper_program(program, mem);
}
inline ProgramSource capture_program_source(const SceGxmVertexProgram &program, const MemState &mem) {
    return detail::capture_wrapper_program(program, mem);
}

// Guest hostFreeCallback does not run C++ destructors. Drop the source owner
// explicitly after renderer/compiler use has drained, before freeing/reusing
// the guest wrapper or restoring guest RAM over it.
inline void release_program_source(SceGxmFragmentProgram &program) {
    if (program.renderer_data)
        program.renderer_data->program_source.reset();
}
inline void release_program_source(SceGxmVertexProgram &program) {
    if (program.renderer_data)
        program.renderer_data->program_source.reset();
}

} // namespace renderer
