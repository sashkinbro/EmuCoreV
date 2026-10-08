package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.CheatBridge
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.VitaInstallBridge
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.ui.cheats.CheatCatalogRepository
import com.sbro.emucorev.ui.cheats.CheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.cheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.visibleCheatPacks
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Opt-in setup for an authorized real-game test. Skipped by ordinary test runs. */
@RunWith(AndroidJUnit4::class)
class DeviceGameSetupTest {
    @Test
    fun installSelectedGameAndDownloadMatchingCheatPack() = runBlocking {
        val arguments = InstrumentationRegistry.getArguments()
        val archivePath = arguments.getString("gameArchive")
        val titleId = arguments.getString("gameTitleId")
        assumeTrue("Supply gameArchive and gameTitleId to opt in", !archivePath.isNullOrBlank() && !titleId.isNullOrBlank())
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val source = File(archivePath!!)
        assertTrue("Copy the authorized game archive into this app's external cache", source.isFile && source.canRead())
        NativeLibraryLoader.ensureLoaded(context)
        val games = InstalledGameRepository()
        if (games.findByTitleId(context, titleId!!) == null) {
            assertTrue("Native installer rejected the game", VitaInstallBridge.installContent(context, source.absolutePath, 1) > 0)
        }
        val game = games.findByTitleId(context, titleId!!)
        assertNotNull("Installed game is absent from the library", game)
        assertTrue("Installed executable is missing", File(game!!.installPath, "eboot.bin").isFile)
        println("Installed ${game.titleId}: ${game.title}, APP_VER=${game.version}")

        // Avoid changing a pack the user has already selected for this game.
        assumeTrue("An existing cheat pack is left in place", CheatBridge.snapshot(titleId).cheats.isEmpty())
        val repository = CheatCatalogRepository(context)
        val entries = repository.load(force = true)
        val visible = visibleCheatPacks(entries, titleId, game.version, false)
        assertTrue(visible.all { it.titleId == titleId })
        val matching = visible.firstOrNull {
            cheatVersionCompatibility(game.version, it.version) == CheatVersionCompatibility.MATCH
        }
        assertNotNull("No catalog pack explicitly matches this installed revision", matching)
        val snapshot = repository.download(matching!!)
        assertNotNull("Verified catalog download/import failed", snapshot)
        assertTrue(snapshot!!.cheats.isNotEmpty())
        assertEquals(matching.id, repository.installedEntryId(titleId, CheatBridge.snapshot(titleId)))
        println("Imported ${matching.id}: ${snapshot.cheats.size} cheats; receipt matches the native reload")
    }
}
