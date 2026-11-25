#include <emucorev/layers.h>

namespace emucorev {

const char *native_layer_name(NativeLayer layer) {
    switch (layer) {
    case NativeLayer::EcvCore:
        return "ecv-core";
    case NativeLayer::CoreAdapter:
        return "emucorev-core-adapter";
    case NativeLayer::AndroidBridge:
        return "emucorev-android-bridge";
    default:
        return "unknown";
    }
}

} // namespace emucorev
