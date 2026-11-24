package com.sbro.emucorev.core

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import java.io.File

/**
 * A firmware package was installed completely (partitions swapped in, marker written).
 *
 * @property version version.txt of the package, e.g. "3.74" (may be empty for a package without one)
 * @property partitions partitions this package installed ("os0", "vs0", "sa0", "pd0"); a fonts package reports only "sa0"
 * @property vitaRoot the Vita root the firmware lives in (`<vitaRoot>/vs0/...`)
 */
data class FirmwareInstalledEvent(
    val version: String,
    val partitions: Set<String>,
    val vitaRoot: File
)

/**
 * Integration point for components that depend on the installed firmware (for example the shell asset pack).
 *
 * Collect [installed] to be told right after every successful install. Late subscribers do not get a replay; they should
 * ask [FirmwareInstallState] (`isFirmwareInstalled`, `installedVersion`) for the current state instead.
 */
object FirmwareInstallEvents {
    private val _installed = MutableSharedFlow<FirmwareInstalledEvent>(extraBufferCapacity = 4)
    val installed: SharedFlow<FirmwareInstalledEvent> = _installed.asSharedFlow()

    internal fun notifyInstalled(event: FirmwareInstalledEvent) {
        _installed.tryEmit(event)
    }
}
