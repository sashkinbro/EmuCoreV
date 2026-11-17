package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import java.io.File
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in research; never boots a game or changes installed firmware. */
@RunWith(AndroidJUnit4::class)
class ShellFirmwareResearchTest {
    private external fun decode(input: ByteArray): ByteArray
    private external fun link(inputs: Array<ByteArray>, trace: Boolean): String

    @Test
    fun inspect() {
        assumeTrue("Firmware research is opt-in", researchMode() == "decode")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        NativeLibraryLoader.ensureLoaded(context)
        val root = File(EmulatorStorage.vitaRoot(context), "vs0")
        val output = File(context.externalCacheDir, "shell-research")
        val sources = root.walkTopDown().filter {
            it.isFile && (it.extension == "suprx" || it.name == "shell.self")
        }.toList()
        assertTrue("Installed firmware modules must exist", sources.isNotEmpty())
        for (source in sources) {
            val bytes = decode(source.readBytes())
            assertTrue("Cannot decode $source", bytes.isNotEmpty())
            val relative = source.relativeTo(root)
            val target = File(output, relative.path)
            check(target.parentFile!!.isDirectory || target.parentFile!!.mkdirs())
            target.writeBytes(bytes)
            println("Decoded $relative: ${bytes.size}")
        }
    }

    @Test
    fun relocateAndLinkShell() {
        assumeTrue("Firmware linking is opt-in", researchMode() in listOf("link", "trace"))
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        NativeLibraryLoader.ensureLoaded(context)
        val root = File(EmulatorStorage.vitaRoot(context), "vs0")
        val paths = listOf(
            "sys/external/libpaf.suprx",
            "vsh/shell/livespace_db.suprx",
            "sys/external/libipmi_nongame.suprx",
            "sys/external/libshellsvc.suprx",
            "sys/external/common_gui_dialog.suprx",
            "sys/external/bXCe.suprx",
            "sys/external/activity_db.suprx",
            "sys/external/dbutil.suprx",
            "sys/external/sqlite.suprx",
            "sys/external/ini_file_processor.suprx",
            "sys/external/libpvf.suprx",
            "vsh/shell/shell.self",
        )
        val modules = paths.map { path ->
            decode(File(root, path).readBytes()).also {
                assertTrue("Cannot decode $path", it.isNotEmpty())
            }
        }.toTypedArray()
        val report = link(modules, researchMode() == "trace")
        val output = File(context.externalCacheDir, "shell-research/link-report.txt")
        check(output.parentFile!!.isDirectory || output.parentFile!!.mkdirs())
        output.writeText(report)
        println(report)
        assertFalse(report, report.contains("ERROR"))
        assertTrue(report, report.contains("SceShell entry=0x"))
        assertFalse(report, report.contains("SceShell entry=0x0\n"))
        assertTrue(report, report.contains("modules=${paths.size} "))
        val bindings = Regex("native_bindings=(\\d+)").find(report)?.groupValues?.get(1)?.toInt() ?: 0
        assertTrue("Native firmware libraries must bind to each other: $report", bindings > 0)
    }

    private fun researchMode() =
        InstrumentationRegistry.getArguments().getString("shellResearch")
}
