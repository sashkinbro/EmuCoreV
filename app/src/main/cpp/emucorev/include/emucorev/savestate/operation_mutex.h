#pragma once

#include <mutex>

namespace emucorev::savestate {
// JNI operations and launch-time Load share session pointer/pause ownership.
// Acquire before publishing a launch that will restore, or reading UI session
// pointers; release only after restoration/failure cleanup and before SDL polling.
inline std::mutex &save_state_operation_mutex() {
    static std::mutex mutex;
    return mutex;
}
}
