// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: MIT

package com.sbro.emucorev.discord;

/** JNI surface loaded only inside the isolated :discord process. */
final class DiscordNative {
    private static boolean loaded;
    private static boolean attempted;

    private DiscordNative() {}

    static synchronized boolean load() {
        if (attempted) return loaded;
        attempted = true;
        try {
            System.loadLibrary("emucorev_discord");
            loaded = true;
        } catch (Throwable error) {
            android.util.Log.w("EmuCoreVDiscord", "Discord SDK bridge unavailable: " + error.getMessage());
            loaded = false;
        }
        return loaded;
    }

    static native boolean available();
    static native void start(String savedToken);
    static native void authorize();
    static native String takeToken();
    static native int status();
    static native String error();
    static native void setPresence(String details, String state, String coverUrl);
    static native String self();
    static native String friends();
    static native void pump();
    static native void stop();
}
