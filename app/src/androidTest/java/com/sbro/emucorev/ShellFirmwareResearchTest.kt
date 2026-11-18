package com.sbro.emucorev

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.SKIP_AUTOMATIC_STATE_LOAD_EXTRA
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.vita.NativeShellCatalogProbe
import com.sbro.emucorev.data.InstalledGameRepository
import java.io.File
import java.security.MessageDigest
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in research; never boots a game or changes installed firmware. */
@RunWith(AndroidJUnit4::class)
class ShellFirmwareResearchTest {
    private external fun requestCatalogImport()
    private external fun installShellProbes(addresses: IntArray)
    private external fun decode(input: ByteArray): ByteArray
    private external fun link(inputs: Array<ByteArray>, trace: Boolean): String
    private external fun prepareBoot(sandbox: String, gpuProbe: String): Boolean
    private external fun verifyKernelCopy(): Boolean
    private external fun verifySystemThreads(): Boolean
    private external fun verifyPackageInfo(root: String): Boolean
    private external fun installPackageFixture(pup: String, root: String): String

    @Test
    fun systemAffinityDoesNotRelaxGameAffinity() {
        assumeTrue("System affinity probe is opt-in", researchMode() == "affinity")
        NativeLibraryLoader.ensureLoaded(InstrumentationRegistry.getInstrumentation().targetContext)
        assertTrue("System CPU mask and game restriction must remain separate", verifySystemThreads())
    }

    @Test
    fun packageInfoUsesInstalledMetadata() {
        assumeTrue("Package info probe is opt-in", researchMode() == "packageInfo")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        NativeLibraryLoader.ensureLoaded(context)
        assertTrue(verifyPackageInfo(File(context.cacheDir, "package-info-probe/vita").absolutePath))
    }

    @Test
    fun fontInstallationRecordsRealPackageVersion() {
        assumeTrue("Real package fixture is opt-in", researchMode() == "packageInstall")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        NativeLibraryLoader.ensureLoaded(context)
        val source = File(context.externalCacheDir, "shell-research/PSP2UPDAT.PUP")
        val target = File(context.cacheDir, "firmware-package-probe/vita")
        assertTrue("Explicit official PUP fixture is required", source.isFile)
        val md5 = MessageDigest.getInstance("MD5")
        source.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                md5.update(buffer, 0, count)
            }
        }
        assertTrue("Official font package identity", md5.digest().joinToString("") { "%02x".format(it) } ==
            "59dcf059d3328fb67be7e51f8aa33418")
        assertTrue("Installer must complete", installPackageFixture(source.absolutePath, target.absolutePath).isNotBlank())
        assertTrue(File(target, "sa0/data/font/pvf/jpn0.pvf").isFile)
        assertTrue(File(target, "system-packages/23.version").readText().trim().toLong(16) == 0x1000010L)
    }

    @Test
    fun kernelCopyWritesOutputAndRejectsInvalidRanges() {
        assumeTrue("Kernel copy probe is opt-in", researchMode() == "copy")
        NativeLibraryLoader.ensureLoaded(InstrumentationRegistry.getInstrumentation().targetContext)
        assertTrue("Kernel-to-user copy and invalid address handling", verifyKernelCopy())
    }

    @Test
    fun bootOriginalShellInPrivateFilesystem() {
        assumeTrue("Shell boot is opt-in", researchMode() in listOf("boot", "bootCatalog"))
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        NativeLibraryLoader.ensureLoaded(context)
        val sandbox = File(context.cacheDir, "native-shell-runtime/vita")
        check(sandbox.isDirectory || sandbox.mkdirs())
        if (researchMode() == "bootCatalog") {
            val games = InstalledGameRepository().loadInstalledGames(context)
            assertTrue("Installed titles are required for the catalog probe", games.isNotEmpty())
            val count = NativeShellCatalogProbe.synchronize(context, sandbox, games)
            assertTrue(count == games.size)
            assertFalse("The shell mirror must not contain game executables",
                File(sandbox, "ux0/app").walkTopDown().any { it.isFile && it.name == "eboot.bin" })
            println("Native shell catalog synchronized: $count installed games")
        }
        val gpuProbe = InstrumentationRegistry.getArguments().getString("shellGpuProbe", "")
        assertTrue(prepareBoot(sandbox.absolutePath, gpuProbe))
        context.startActivity(Intent(context, Emulator::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            .putExtra("AppStartParameters", arrayOf("-r", "__VITA_SHELL_RESEARCH__"))
            .putExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, true))
        // Observational bootstrap only. A successful test is not proof of a
        // rendered shell: inspect native logs and the device frame separately.
        val seconds = InstrumentationRegistry.getArguments().getString("shellSeconds", "30")
            .toLong().coerceIn(10, 900)
        val probes = InstrumentationRegistry.getArguments().getString("shellProbeAddresses", "")
        if (researchMode() == "bootCatalog" || probes.isNotEmpty()) {
            Thread.sleep(15_000)
            if (researchMode() == "bootCatalog") requestCatalogImport()
            if (probes.isNotEmpty()) installShellProbes(probes.split(',').map { java.lang.Long.decode(it).toInt() }.toIntArray())
            Thread.sleep((seconds * 1_000 - 15_000).coerceAtLeast(0))
        } else {
            Thread.sleep(seconds * 1_000)
        }
    }

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
