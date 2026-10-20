// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

struct EmuEnvState;
struct SceKernelModuleInfo;

// Called after relocation and before the newly loaded module can execute.
void apply_guest_code_fixes(EmuEnvState &emuenv, const SceKernelModuleInfo &module);
