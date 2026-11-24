package com.sbro.emucorev.core.vita

import com.sbro.emucorev.nativeui.FirmwarePack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** B029: the native shell is offered only when installed firmware can supply its resource pack. */
class NativeLibraryModeGateTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private val sources = listOf(
        "vs0/vsh/shell/topmenu_plugin.rco",
        "vs0/data/external/common/common_resource.rco",
        "vs0/data/internal/keylock/keylock.png",
        "sa0/data/font/pvf/ltn0.pvf",
        "sa0/data/font/pvf/ltn4.pvf",
    )

    private fun install(root: File, only: List<String> = sources) = only.forEach {
        File(root, it).apply { parentFile!!.mkdirs() }.writeText("x")
    }

    @Test
    fun notAvailableWithoutFirmware() {
        val root = temporaryFolder.newFolder("vita")
        assertFalse(NativeLibraryMode.isAvailable(true, root))
        assertNull(FirmwarePack.key(root))
    }

    @Test
    fun notAvailableWithPartialFirmware() {
        val root = temporaryFolder.newFolder("vita")
        // Core firmware without the font package (sa0) cannot build the pack.
        install(root, sources.filter { it.startsWith("vs0/") })
        assertFalse(NativeLibraryMode.isAvailable(true, root))
        install(root, sources.filter { it.startsWith("sa0/") })
        assertTrue(NativeLibraryMode.isAvailable(true, root))
    }

    @Test
    fun availableOnlyWhenShellIsBuiltAndFirmwareInstalled() {
        val root = temporaryFolder.newFolder("vita")
        install(root)
        assertTrue(NativeLibraryMode.isAvailable(true, root))
        assertFalse(NativeLibraryMode.isAvailable(false, root))
    }

    @Test
    fun packKeyChangesWhenFirmwareIsReinstalled() {
        val root = temporaryFolder.newFolder("vita")
        install(root)
        val first = FirmwarePack.key(root)
        assertNotNull(first)
        assertEquals(first, FirmwarePack.key(root))
        File(root, sources[0]).apply { writeText("a different firmware build"); setLastModified(lastModified() + 5000) }
        assertNotEquals(first, FirmwarePack.key(root))
    }
}
