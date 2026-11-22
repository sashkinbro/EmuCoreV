package com.sbro.emucorev.core.vita

/**
 * Decision logic for the microphone permission flow of sceAudioIn.
 *
 * The prompt is shown at most once per session (a denied or "don't ask again" answer is final until
 * the next session), and the native capture device is re-opened only when the permission was granted
 * as the result of that prompt.
 */
class RecordAudioPermissionGate {
    private var requested = false

    /** True exactly once, and only while the permission is missing. */
    @Synchronized
    fun shouldRequest(granted: Boolean): Boolean {
        if (granted || requested) return false
        requested = true
        return true
    }

    /** True when the native side should re-open the capture device for this result. */
    fun shouldReopenCapture(requestCode: Int, granted: Boolean): Boolean =
        requestCode == REQUEST_CODE && granted

    companion object {
        const val REQUEST_CODE = 0x4D1C
    }
}
