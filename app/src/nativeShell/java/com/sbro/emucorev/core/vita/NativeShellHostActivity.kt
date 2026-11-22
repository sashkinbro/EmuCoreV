package com.sbro.emucorev.core.vita

import android.content.Intent
import android.widget.Toast
import com.sbro.emucorev.MainActivity
import com.sbro.emucorev.R
import com.sbro.emucorev.core.SaveStateRepository
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.nativeui.shell.ShellSaveGame
import com.sbro.emucorev.nativeui.shell.ShellSaveSlot
import com.sbro.emucorev.nativeui.shell.ShellSaveSource
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.TrophyRepository
import com.sbro.emucorev.data.VitaTrophyGrade
import com.sbro.emucorev.nativeui.shell.ShellTrophy
import com.sbro.emucorev.nativeui.shell.ShellTrophyGame
import com.sbro.emucorev.nativeui.shell.ShellTrophySource
import com.sbro.emucorev.nativeui.BundledImages
import com.sbro.emucorev.nativeui.shell.NativeShellActivity
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/**
 * Native PS Vita shell library mode. It runs in its own process: the game's
 * Emulator process is retired after a session and brings this resident Home
 * back to the front. Browsing never initialises the emulation core.
 */
class NativeShellHostActivity : NativeShellActivity(), NativeLibraryMode.ReturnTarget {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val launching = AtomicBoolean(false)

    override fun loadCatalog(): List<BundledImages.HomeCatalogSource> =
        InstalledGameRepository().loadInstalledGames(this).mapNotNull { game ->
            val icon = game.iconPath?.let(::File)?.takeIf(File::isFile) ?: return@mapNotNull null
            BundledImages.HomeCatalogSource(
                id = game.titleId,
                title = game.title,
                contentId = game.contentId.orEmpty(),
                icon = icon,
                liveAreaRoot = File(game.installPath, "sce_sys/livearea/contents").takeIf(File::isDirectory),
            )
        }

    // Same data the app's achievements screen shows; the shell only reads it.
    override fun trophySource() = ShellTrophySource {
        TrophyRepository().list(this).map { set ->
            ShellTrophyGame(
                id = set.communicationId,
                title = set.gameTitle.ifBlank { set.setName },
                icon = set.gameIconPath?.let(::File),
                trophies = set.trophies.map { trophy ->
                    ShellTrophy(
                        name = trophy.name,
                        detail = trophy.detail,
                        grade = when (trophy.grade) {
                            VitaTrophyGrade.Platinum -> 0
                            VitaTrophyGrade.Gold -> 1
                            VitaTrophyGrade.Silver -> 2
                            else -> 3
                        },
                        hidden = trophy.hidden,
                        unlocked = trophy.unlocked,
                        unlockedAtEpochSeconds = trophy.unlockedAtEpochSeconds,
                        icon = trophy.iconPath?.let(::File),
                    )
                },
            )
        }
    }

    // Reads save-state files directly: the shell process never loads the core.
    override fun saveSource() = object : ShellSaveSource {
        override fun load(): List<ShellSaveGame> {
            val repository = SaveStateRepository(this@NativeShellHostActivity)
            val games = InstalledGameRepository().loadInstalledGames(this@NativeShellHostActivity)
                .associateBy { it.titleId }
            return repository.gamesWithSlots().mapNotNull { titleId ->
                val game = games[titleId] ?: return@mapNotNull null
                val slots = SaveStateRepository.SLOT_ORDER.mapNotNull { slot ->
                    val state = repository.stateFile(titleId, slot)
                    if (!state.isFile || state.length() == 0L) return@mapNotNull null
                    ShellSaveSlot(slot, repository.thumbnailFile(titleId, slot).takeIf(File::isFile),
                        state.lastModified(), state.length())
                }
                ShellSaveGame(titleId, game.title, game.iconPath?.let(::File), slots)
            }
        }

        override fun resume(titleId: String, slot: Int) {
            val path = SaveStateRepository(this@NativeShellHostActivity).stateFile(titleId, slot).absolutePath
            launch { VitaLaunchBridge.launchInstalledTitleWithSaveState(this@NativeShellHostActivity, titleId, path) }
        }
    }

    override fun onLiveAreaTarget(titleId: String, target: String) {
        // The gate (Start) target carries the title id; other live items are
        // web/store links the shell does not open.
        if (target != titleId) return
        launch { VitaLaunchBridge.launchInstalledTitle(this@NativeShellHostActivity, titleId) }
    }

    private fun launch(start: suspend () -> VitaLaunchBridge.LaunchResult) {
        if (!launching.compareAndSet(false, true)) return
        scope.launch {
            try {
                when (start()) {
                    // Busy: an earlier launch has not reported ready (or timed out) yet; do not start another.
                    VitaLaunchBridge.LaunchResult.Success, VitaLaunchBridge.LaunchResult.Busy -> Unit
                    VitaLaunchBridge.LaunchResult.MissingFirmware ->
                        toast(R.string.game_launch_requires_firmware)
                    VitaLaunchBridge.LaunchResult.MissingFirmwareUpdate ->
                        toast(R.string.game_launch_requires_firmware_update)
                    VitaLaunchBridge.LaunchResult.StorageUnavailable -> toast(R.string.storage_unavailable_message)
                    VitaLaunchBridge.LaunchResult.Failure -> toast(R.string.game_launch_failed)
                }
            } finally {
                launching.set(false)
            }
        }
    }

    override fun onExitRequested() {
        NativeLibraryExitDialog(this) {
            NativeLibraryMode.setSelected(this, false)
            startActivity(Intent(this, MainActivity::class.java).apply {
                action = Intent.ACTION_MAIN
                addCategory(Intent.CATEGORY_LAUNCHER)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
                putExtra(NativeLibraryMode.EXIT_NATIVE_LIBRARY, true)
            })
            finish()
        }.show()
    }

    private fun toast(message: Int) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }
}
