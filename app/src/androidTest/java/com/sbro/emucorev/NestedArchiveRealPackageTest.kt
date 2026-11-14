package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.InstallerPackageKind
import com.sbro.emucorev.core.VitaArchiveContainerSupport
import com.sbro.emucorev.core.VitaInstallBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.libsdl.app.SDL
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Optional installer acceptance for a real nested VPK, using only a disposable Vita root. */
@RunWith(AndroidJUnit4::class)
class NestedArchiveRealPackageTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Before
    fun loadNativeLibrary() {
        SDL.setContext(context)
        SDL.loadLibrary("ECV", context)
        SDL.setupJNI()
    }

    @Test
    fun preparedNestedVpkInstallsIntoDisposableRoot() {
        val arguments = InstrumentationRegistry.getArguments()
        val archivePath = arguments.getString("archivePath")
        assumeTrue("Pass archivePath pointing to a wrapper containing a real VPK", !archivePath.isNullOrBlank())
        val expectedTitleId = requireNotNull(arguments.getString("expectedTitleId"))
        val expectedEbootHash = requireNotNull(arguments.getString("expectedEbootSha256"))
        require(expectedTitleId.matches(Regex("[A-Z0-9]{9}")))
        require(expectedEbootHash.matches(Regex("[0-9a-f]{64}")))
        val archive = File(requireNotNull(archivePath))
        assumeTrue("Acceptance archive is unavailable: ${archive.absolutePath}", archive.isFile && archive.canRead())

        val scratch = File(context.cacheDir, "nested-real-package-${UUID.randomUUID()}")
        assertTrue("Could not create isolated test root", scratch.mkdirs())
        try {
            val stagingRoot = File(scratch, "expansion-cache")
            val vitaRoot = File(scratch, "runtime-vita")
            val installerCache = File(scratch, "installer-cache")
            assertTrue(stagingRoot.mkdirs())
            assertTrue(vitaRoot.mkdirs())
            assertTrue(installerCache.mkdirs())

            val plan = VitaArchiveContainerSupport.scan(archive.absolutePath, stagingRoot)
            assertNotNull("Production archive preparation did not find the nested VPK", plan)
            requireNotNull(plan).use { preparedPlan ->
                val packages = preparedPlan.candidates.filter { it.kind == InstallerPackageKind.VPK }
                assertEquals("Wrapper should contain exactly one installable VPK", 1, packages.size)
                preparedPlan.prepare(packages.single()).use { prepared ->
                    // installPreparedEmbeddedPackage targets the user's configured Vita root;
                    // invoke that same production JNI installer with this isolated test root.
                    val install = VitaInstallBridge::class.java.getDeclaredMethod(
                        "nativeInstallContent", String::class.java, String::class.java,
                        String::class.java, Int::class.javaPrimitiveType
                    ).apply { isAccessible = true }
                    val installedCount = install.invoke(
                        VitaInstallBridge,
                        vitaRoot.absolutePath,
                        installerCache.absolutePath,
                        prepared.file.absolutePath,
                        1
                    ) as Int
                    assertEquals("Native installer did not install one package", 1, installedCount)

                    val installedEboot = File(vitaRoot, "ux0/app/$expectedTitleId/eboot.bin")
                    assertTrue("Expected game executable was not installed", installedEboot.isFile)
                    assertEquals(
                        "Installed executable differs from the source VPK",
                        expectedEbootHash,
                        installedEboot.sha256()
                    )
                }
            }
        } finally {
            scratch.deleteRecursively()
        }
    }

    private fun File.sha256(): String {
        val digest = MessageDigest.getInstance("SHA-256")
        inputStream().use { input ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { byte -> "%02x".format(byte) }
    }

}
