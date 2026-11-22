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

#pragma once

#include <string>
#include <vector>

namespace device {

// Resolves '.', '..' and empty segments of a device-relative guest path. A '..' that would climb
// above the device root is dropped (the path stays inside its mount). The result has no leading
// slash; a trailing slash of the input is kept.
inline std::string resolve_guest_subpath(const std::string &path) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t end = path.find('/', pos);
        if (end == std::string::npos)
            end = path.size();
        const std::string seg = path.substr(pos, end - pos);
        if (seg == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        pos = end + 1;
    }

    std::string out;
    for (const auto &p : parts) {
        if (!out.empty())
            out += '/';
        out += p;
    }
    if (!out.empty() && path.back() == '/')
        out += '/';
    return out;
}

} // namespace device
