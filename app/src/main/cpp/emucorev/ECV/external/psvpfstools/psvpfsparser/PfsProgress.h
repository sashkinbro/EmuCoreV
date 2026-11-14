#pragma once

#include <functional>
#include <cstddef>
#include <string>

using PfsProgressCallback = std::function<void(std::uint64_t processed, std::uint64_t total, const std::string& path)>;
