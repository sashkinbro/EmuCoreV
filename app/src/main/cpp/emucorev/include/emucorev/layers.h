#pragma once

namespace emucorev {

enum class NativeLayer {
    EcvCore = 1,
    CoreAdapter = 2,
    AndroidBridge = 3,
};

const char *native_layer_name(NativeLayer layer);

} // namespace emucorev
