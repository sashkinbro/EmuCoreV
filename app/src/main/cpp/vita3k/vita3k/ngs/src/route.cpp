// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <ngs/system.h>

#include <util/vector_utils.h>

namespace ngs {
bool deliver_data_to_master(const MemState &mem, Voice *master, Voice *source, const VoiceProduct &data_to_deliver) {
    if (!master || !source || !data_to_deliver.data)
        return false;

    Patch implicit{};
    implicit.output_index = 0;
    implicit.output_sub_index = 0;
    implicit.dest_index = 0;
    implicit.source = Ptr<Voice>(source, mem);
    implicit.dest = Ptr<Voice>(master, mem);
    memcpy(implicit.volume_matrix, source->implicit_volume_matrix, sizeof(implicit.volume_matrix));

    const std::lock_guard<std::mutex> guard(*master->voice_mutex);
    return master->inputs.receive(mem, &implicit, data_to_deliver) == 0;
}

bool deliver_data(const MemState &mem, const std::vector<Voice *> &voice_queue, Voice *source, const uint8_t output_port,
    const VoiceProduct &data_to_deliver) {
    if (!data_to_deliver.data) {
        return false;
    }

    bool delivered_to_anyone = false;

    for (auto &patch_ptr : source->patches[output_port]) {
        Patch *patch = patch_ptr.get(mem);

        if (!patch || !patch->is_active())
            continue;

        Voice *dest = patch->dest.get(mem);
        if (!dest || !std::ranges::contains(voice_queue, dest))
            continue;

        const bool carries_audio = patch->volume_matrix[0][0] != 0.0f
            || patch->volume_matrix[0][1] != 0.0f
            || patch->volume_matrix[1][0] != 0.0f
            || patch->volume_matrix[1][1] != 0.0f;

        const std::lock_guard<std::mutex> guard(*dest->voice_mutex);
        if (dest->inputs.receive(mem, patch, data_to_deliver) == 0 && carries_audio)
            delivered_to_anyone = true;
    }

    return delivered_to_anyone;
}
} // namespace ngs
