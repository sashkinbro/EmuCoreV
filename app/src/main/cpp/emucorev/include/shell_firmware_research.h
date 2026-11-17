#pragma once

#ifndef NDEBUG
#include <emuenv/app_launch_request.h>

struct EmuEnvState;

inline bool is_shell_firmware_research(const AppLaunchRequest &request) {
    return request.app_path == "__VITA_SHELL_RESEARCH__";
}

bool prepare_shell_firmware_research(EmuEnvState &emuenv);
#endif
