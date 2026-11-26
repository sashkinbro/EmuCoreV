#pragma once
// Host-test stub of util/log.h: logging is dropped, the arguments are not evaluated.
#include <cstdint>
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
template <typename T>
inline T log_hex(T value) { return value; }
