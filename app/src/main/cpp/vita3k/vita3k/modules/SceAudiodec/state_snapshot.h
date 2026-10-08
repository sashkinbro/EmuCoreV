// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct EmuEnvState;

namespace audiodec {

struct PreparedState;

// Ensure the module-owned host state exists. Called by LIBRARY_INIT and tests.
void initialize_state(EmuEnvState &emuenv);

// Legacy savestates without an Audiodec section cannot reconstruct decoder handles.
void clear_state(EmuEnvState &emuenv);

bool capture_state(EmuEnvState &emuenv, std::vector<uint8_t> &blob, std::string &error);
bool prepare_state(const std::vector<uint8_t> &blob, std::shared_ptr<PreparedState> &prepared, std::string &error);
std::vector<int32_t> decoder_handles(const PreparedState &prepared);
void apply_state(EmuEnvState &emuenv, std::shared_ptr<PreparedState> prepared);
bool validate_state(const std::vector<uint8_t> &blob, std::string &error);
bool restore_state(EmuEnvState &emuenv, const std::vector<uint8_t> &blob, std::string &error);

} // namespace audiodec
