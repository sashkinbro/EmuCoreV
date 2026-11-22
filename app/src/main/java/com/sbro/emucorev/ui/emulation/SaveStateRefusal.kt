package com.sbro.emucorev.ui.emulation

import com.sbro.emucorev.R

/** Why an in-game save-state action was not started. Shown to the user and logged. */
internal enum class SaveStateRefusal(val messageRes: Int) {
    NoGame(R.string.emulation_savestate_refused_no_game),
    Exiting(R.string.emulation_savestate_refused_exiting),
    Loading(R.string.emulation_savestate_refused_loading),
    Busy(R.string.emulation_savestate_refused_busy)
}

/** Returns the reason a save-state action must be refused right now, or null when it may start. */
internal fun saveStateRefusalReason(
    gameIdBlank: Boolean,
    exitInProgress: Boolean,
    launchStateLoading: Boolean,
    waitingForStateFrame: Boolean,
    saveStateBusy: Boolean
): SaveStateRefusal? = when {
    gameIdBlank -> SaveStateRefusal.NoGame
    exitInProgress -> SaveStateRefusal.Exiting
    launchStateLoading || waitingForStateFrame -> SaveStateRefusal.Loading
    saveStateBusy -> SaveStateRefusal.Busy
    else -> null
}
