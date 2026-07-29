package com.sbro.emucorev.core.vita.overlay

/**
 * Keeps the SDL virtual controller stable while the Compose overlay recomposes.
 *
 * Re-attaching an already attached controller resets its native joystick state,
 * which can drop held buttons and briefly stall input.
 */
internal class ControllerAttachmentState {
    var isAttached: Boolean = false
        private set

    fun synchronize(
        shouldAttach: Boolean,
        attach: () -> Boolean,
        detach: () -> Unit
    ) {
        if (shouldAttach == isAttached) return
        if (shouldAttach) {
            isAttached = attach()
        } else {
            detach()
            isAttached = false
        }
    }
}
