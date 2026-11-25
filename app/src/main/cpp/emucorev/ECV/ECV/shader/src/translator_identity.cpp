#include <shader/translator_identity.h>

#include <translator_identity_generated.h>

namespace shader {

uint64_t translator_source_hash() {
    return ECV_SHADER_TRANSLATOR_HASH;
}

} // namespace shader
