#pragma once

#include <emuenv/app_launch_request.h>
#include <string>

struct EmuEnvState;
struct ThreadState;

inline bool is_native_library(const AppLaunchRequest &request) {
    return request.app_path == "__VITA_NATIVE_LIBRARY__";
}

bool prepare_native_library(EmuEnvState &emuenv);
void poll_native_library_catalog(EmuEnvState &emuenv, ThreadState &thread);
int dispatch_native_library_game_launch(EmuEnvState &emuenv, const char *path);

bool configure_native_library(const std::string &path, const std::string &probe = {});
void request_native_library_catalog_import();
int native_library_shell_event_count();
int receive_native_library_shell_event(void *output);
int native_library_app_status(int app_id, void *output);
int activate_native_library_app(int app_id);
int native_library_app_id(const char *name, int *process_id);
int native_library_app_name(int process_id, char *name);
int native_library_app_path(int app_id, char *path);
