#pragma once

#ifndef NDEBUG
#include <emuenv/app_launch_request.h>

struct EmuEnvState;
struct ThreadState;

inline bool is_shell_firmware_research(const AppLaunchRequest &request) {
    return request.app_path == "__VITA_SHELL_RESEARCH__";
}

bool prepare_shell_firmware_research(EmuEnvState &emuenv);
void poll_shell_catalog_research(EmuEnvState &emuenv, ThreadState &thread);
int dispatch_shell_game_launch_research(EmuEnvState &emuenv, const char *path);
#endif
