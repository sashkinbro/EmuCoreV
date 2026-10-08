#pragma once
// Logging is unrelated to the host waiting/suspension regressions.
#define LOG_WARN(...) ((void)0)

#define LOG_ERROR(...) ((void)0)

#define LOG_DEBUG(...) ((void)0)
#define LOG_WARN_ONCE(...) ((void)0)
#define RET_ERROR(error) (error)
