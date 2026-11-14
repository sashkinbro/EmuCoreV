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

#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_OFF
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <boost/core/demangle.hpp>
#include <boost/describe/enum.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <spdlog/spdlog.h>
#include <util/exit_code.h>
#include <util/fs.h>

#include <atomic>
#include <functional>
#include <type_traits>

#ifdef NDEBUG
#define LOG_AT_LEVEL(level, ...) static_cast<void>(0)
#else
// Check severity before evaluating arguments: muted diagnostics must not
// format strings, inspect guest memory or construct hex dumps.
#define LOG_AT_LEVEL(level, ...)                                                     \
    (spdlog::default_logger_raw()->should_log(level)                                 \
            ? spdlog::default_logger_raw()->log(                                    \
                  spdlog::source_loc{ __FILE__, __LINE__, SPDLOG_FUNCTION }, level, __VA_ARGS__) \
            : static_cast<void>(0))
#endif

#define LOG_TRACE(...) LOG_AT_LEVEL(spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) LOG_AT_LEVEL(spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...) LOG_AT_LEVEL(spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) LOG_AT_LEVEL(spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) LOG_AT_LEVEL(spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) LOG_AT_LEVEL(spdlog::level::critical, __VA_ARGS__)

#ifdef NDEBUG
#define LOG_IF(level, log_function, flag, ...) static_cast<void>(0)
#else
#define LOG_IF(level, log_function, flag, ...) \
    ((spdlog::default_logger_raw()->should_log(level) && (flag)) ? log_function(__VA_ARGS__) : static_cast<void>(0))
#endif

#define LOG_TRACE_IF(flag, ...) LOG_IF(spdlog::level::trace, LOG_TRACE, flag, __VA_ARGS__)
#define LOG_DEBUG_IF(flag, ...) LOG_IF(spdlog::level::debug, LOG_DEBUG, flag, __VA_ARGS__)
#define LOG_INFO_IF(flag, ...) LOG_IF(spdlog::level::info, LOG_INFO, flag, __VA_ARGS__)
#define LOG_WARN_IF(flag, ...) LOG_IF(spdlog::level::warn, LOG_WARN, flag, __VA_ARGS__)
#define LOG_ERROR_IF(flag, ...) LOG_IF(spdlog::level::err, LOG_ERROR, flag, __VA_ARGS__)
#define LOG_CRITICAL_IF(flag, ...) LOG_IF(spdlog::level::critical, LOG_CRITICAL, flag, __VA_ARGS__)

#ifdef NDEBUG
#define LOG_ONCE(level, log_function, ...) static_cast<void>(0)
#else
#define LOG_ONCE(level, log_function, ...)   \
    do {                                    \
        static std::atomic_flag has_logged; \
        if (spdlog::default_logger_raw()->should_log(level) && !has_logged.test_and_set()) \
            log_function(__VA_ARGS__);      \
    } while (0)
#endif

#define LOG_TRACE_ONCE(...) LOG_ONCE(spdlog::level::trace, LOG_TRACE, __VA_ARGS__)
#define LOG_DEBUG_ONCE(...) LOG_ONCE(spdlog::level::debug, LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO_ONCE(...) LOG_ONCE(spdlog::level::info, LOG_INFO, __VA_ARGS__)
#define LOG_WARN_ONCE(...) LOG_ONCE(spdlog::level::warn, LOG_WARN, __VA_ARGS__)
#define LOG_ERROR_ONCE(...) LOG_ONCE(spdlog::level::err, LOG_ERROR, __VA_ARGS__)
#define LOG_CRITICAL_ONCE(...) LOG_ONCE(spdlog::level::critical, LOG_CRITICAL, __VA_ARGS__)

namespace logging {

ExitCode init(const Root &root_paths, bool use_stdout);
void set_level(spdlog::level::level_enum log_level);
ExitCode add_sink(const fs::path &log_path);
void flush();
void set_log_callback(std::function<void(std::string, int)> cb);

} // namespace logging

#define RET_ERROR(error)                                                            \
    ([&]() {                                                                        \
        LOG_ERROR_ONCE("{} returned {} ({})", export_name, #error, log_hex(error)); \
        return static_cast<int>(error);                                             \
    })()

/*
    returns: A string with the input number formatted in hexadecimal
    Examples:
        * `12` returns: `"0xC"`
        * `1337` returns: `"0x539"`
        * `72742069` returns: `"0x455F4B5"`
*/
template <typename T>
std::string log_hex(T val) {
    return fmt::format("0x{:X}", static_cast<std::make_unsigned_t<T>>(val));
}

/*
    returns: A string with the input number formatted in hexadecimal with padding of the inputted type size
    Examples:
        * `uint8_t 5` returns: `"0x05"`
        * `uint8_t 15` returns: `"0x0F"`
        * `uint8_t 255` returns: `"0xFF"`

        * `uint16_t 15` returns: `"0x000F"`
        * `uint16_t 1337` returns: `"0x0539"`
        * `uint16_t 65535` returns: `"0xFFFF"`

        * `uint32_t 15` returns: `"0x0000000F"`
        * `uint32_t 1337` returns: `"0x00000539"`
        * `uint32_t 65535` returns: `"0x0000FFFF"`
        * `uint32_t 134217728` returns: `"0x08000000"`
*/
template <typename T>
std::string log_hex_full(T val) {
    return fmt::format("0x{:0{}X}", static_cast<std::make_unsigned_t<T>>(val), sizeof(T) * 2);
}

template <class T>
class Ptr;
FMT_BEGIN_NAMESPACE
template <typename T, typename Char>
struct formatter<Ptr<T>, Char> : formatter<string_view, Char> {
public:
    template <typename FormatContext>
    auto format(const Ptr<T> p, FormatContext &ctx) const {
        return detail::write(ctx.out(),
            basic_string_view<Char>(log_hex_full(p.address())));
    }
};
template <typename T, typename Char>
    requires(boost::describe::has_describe_enumerators<T>::value)
struct formatter<T, Char> : formatter<string_view, Char> {
public:
    template <typename FormatContext>
    auto format(const T e, FormatContext &ctx) const {
        auto name = boost::describe::enum_to_string(e, nullptr);
        if (name != nullptr) {
            return detail::write(ctx.out(), name);
        } else {
            auto enum_as_uint = static_cast<std::make_unsigned_t<T>>(e);
            auto enum_as_string = fmt::format("{}(0x{:0{}X})", boost::core::demangle(typeid(T).name()), enum_as_uint, sizeof(enum_as_uint) * 2);
            return detail::write(ctx.out(), basic_string_view<Char>(enum_as_string));
        }
    }
};
FMT_END_NAMESPACE
