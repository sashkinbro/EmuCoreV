#pragma once

#include <cstdint>

namespace shader {

// Hash of the translator sources this binary was built from (generated at build time by
// shader/cmake/translator_hash.cmake). The on-disk shader cache is keyed by it, see docs/shader-cache.md.
uint64_t translator_source_hash();

} // namespace shader
