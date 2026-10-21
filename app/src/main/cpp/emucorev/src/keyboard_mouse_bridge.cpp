// EmuCoreV adapter layer: keyboard/mouse pad snapshots.
//
// The Kotlin KeyboardMouseManager merges keyboard and mouse bindings into one
// snapshot per player. This bridge stores those snapshots in the EmuCoreV input
// layer, which the Vita3K controller read path merges with physical controllers
// and the touch overlay.

#include "android_state.h"

#include <emucorev/input_state.h>
#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_setKeyboardMousePadState(
    JNIEnv *, jobject, jint pad_index, jint buttons, jfloat left_x, jfloat left_y,
    jfloat right_x, jfloat right_y) {
    if (!get_emuenv())
        return;

    emucorev::input_state::set_pad_state(
        static_cast<int>(pad_index),
        static_cast<uint32_t>(buttons),
        static_cast<float>(left_x),
        static_cast<float>(left_y),
        static_cast<float>(right_x),
        static_cast<float>(right_y));
}
