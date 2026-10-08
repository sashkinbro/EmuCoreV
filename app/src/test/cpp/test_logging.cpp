#include <spdlog/spdlog.h>

// The allocator's fault diagnostics use the application logging flush hook.
// Tests have only the default spdlog sink and do not initialize app storage.
namespace logging {
void flush() {
    spdlog::default_logger()->flush();
}
}
