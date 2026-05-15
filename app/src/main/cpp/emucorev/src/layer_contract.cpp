#include <emucorev/layers.h>

namespace emucorev {

const char *native_layer_name(NativeLayer layer) {
    switch (layer) {
    case NativeLayer::Vita3KCore:
        return "vita3k-core";
    case NativeLayer::CoreAdapter:
        return "emucorev-core-adapter";
    case NativeLayer::AndroidBridge:
        return "emucorev-android-bridge";
    default:
        return "unknown";
    }
}

} // namespace emucorev
