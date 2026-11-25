// Host shim: the core has unconditional android/log.h includes in a few debug hooks.
#pragma once
#include <cstdio>
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define __android_log_print(prio, tag, ...) (std::fprintf(stderr, "[%s] ", tag), std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr), 0)
