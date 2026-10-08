#include <emucorev/savestate/operation_mutex.h>
#include <functional>

namespace save_state_operation_test {
std::mutex *ui_mutex() { return &emucorev::savestate::save_state_operation_mutex(); }
int read_and_pause_ui_session(const std::function<void()> &before_lock, const std::function<int()> &access) {
    before_lock();
    const std::lock_guard operation(emucorev::savestate::save_state_operation_mutex());
    return access();
}
}
