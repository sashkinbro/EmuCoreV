package com.sbro.emucorev

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EmuCoreVAppProcessGatingTest {
    private val pkg = "com.sbro.emucorev"

    @Test fun helperAndFirmwareProcessesSkipAppStartup() {
        listOf(":phoenix", ":discord", ":native_shell", ":native_library", ":native_trophies").forEach {
            assertTrue(it, EmuCoreVApp.skipsAppStartup("$pkg$it"))
        }
    }

    @Test fun mainProcessStillRunsAppStartup() {
        assertFalse(EmuCoreVApp.skipsAppStartup(pkg))
        assertFalse(EmuCoreVApp.skipsAppStartup(null))
    }

    @Test fun startupGateRunsBeforeAnyGameBookkeeping() {
        val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
            .first { Files.isDirectory(it.resolve("src/main")) }
        val source = app.resolve("src/main/java/com/sbro/emucorev/EmuCoreVApp.kt").readText()
        val gate = source.indexOf("skipsAppStartup(Application.getProcessName())")
        assertTrue(gate > 0)
        for (work in listOf("finishOpenSessions", "TrophyCloudRepository.resumePending", "DiscordIntegration.initialize",
            "EmulatorStorage.prepareRuntime", "NativeLibraryLoader.ensureLoaded")) {
            assertTrue(work, source.indexOf(work) > gate)
        }
    }
}
