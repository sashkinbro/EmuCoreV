package com.sbro.emucorev.core

import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class VitaEmbeddedPackageSupportTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun findsBySignatureAndValidatesNestedVpkMetadata() {
        val wrapper = temp.newFile("misnamed-wrapper.bin").also {
            writeZip(it, listOf(
                "bundle/not-a-vpk-extension.bin" to vpk("PCSE00001"),
                "bundle/license.dat" to pkgHeader()
            ))
        }

        val selection = VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root)

        assertTrue(selection is InstallerPackageSelection.RequiresChoice)
        val choices = (selection as InstallerPackageSelection.RequiresChoice).candidates
        assertEquals(listOf("bundle/license.dat", "bundle/not-a-vpk-extension.bin"), choices.map { it.path })
        assertEquals(listOf(InstallerPackageKind.PKG, InstallerPackageKind.VPK), choices.map { it.kind })
    }

    @Test fun scansGeneratedNestedVpkAndPkgWrapperFixture() {
        val fixtureRoot = System.getenv("INSTALLER_EXPANSION_FIXTURES")?.let(::File)
        val wrapper = fixtureRoot?.resolve("nested-vpk-pkg-wrapper.zip")
        if (wrapper == null || !wrapper.isFile) return

        val selection = VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root)

        assertTrue(selection is InstallerPackageSelection.RequiresChoice)
        val candidates = (selection as InstallerPackageSelection.RequiresChoice).candidates
        assertEquals(listOf("wrapper/Game/game.vpk", "wrapper/licenses/synthetic.pkg"), candidates.map { it.path })
        assertEquals(listOf(InstallerPackageKind.VPK, InstallerPackageKind.PKG), candidates.map { it.kind })
    }

    @Test fun selectedVpkIsExtractedAtomicallyAndSessionCanBeCleaned() {
        val expected = vpk("PCSE00002")
        val wrapper = wrapper("folder/game.vpk" to expected)
        val sourceBefore = wrapper.readBytes()
        val candidate = (VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root) as InstallerPackageSelection.Ready).candidate

        val prepared = VitaEmbeddedPackageSupport.extractSelected(wrapper.path, candidate, temp.root)
        val extracted = prepared.file
        assertArrayEquals(expected, extracted.readBytes())
        assertArrayEquals(sourceBefore, wrapper.readBytes())
        assertTrue(VitaArchiveInspector.isInstallableVpk(extracted.path))
        prepared.close()
        assertFalse(extracted.exists())
    }

    @Test fun multipleCandidatesAreSortedAndNeverImplicitlySelected() {
        val wrapper = wrapper("z/game.pkg" to pkgHeader(), "a/game.pkg" to pkgHeader())

        val selection = VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root)

        assertTrue(selection is InstallerPackageSelection.RequiresChoice)
        assertEquals(listOf("a/game.pkg", "z/game.pkg"), (selection as InstallerPackageSelection.RequiresChoice).candidates.map { it.path })
    }

    @Test fun cancellationStopsScanAndDoesNotLeaveStaging() {
        val wrapper = wrapper("folder/game.vpk" to vpk("PCSE00003"))

        val error = runCatching {
            VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root) { true }
        }.exceptionOrNull()

        assertTrue(error is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.CANCELLED, (error as InstallerExpansionException).failure)
        assertTrue(temp.root.listFiles()?.none { it.name.startsWith("installer-expansion-") } != false)
    }

    @Test fun extractionCancellationRemovesItsPrivatePartialSession() {
        val bytes = pkgHeader() + ByteArray(256 * 1024) { (it and 0xff).toByte() }
        val wrapper = wrapper("folder/title.pkg" to bytes)
        val candidate = (VitaEmbeddedPackageSupport.scanZip(wrapper.path, temp.root) as InstallerPackageSelection.Ready).candidate
        var cancelled = false

        val error = runCatching {
            VitaEmbeddedPackageSupport.extractSelected(
                wrapper.path,
                candidate,
                temp.root,
                isCancelled = { cancelled },
                onProgress = { cancelled = true }
            )
        }.exceptionOrNull()

        assertTrue(error is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.CANCELLED, (error as InstallerExpansionException).failure)
        assertTrue(temp.root.listFiles()?.none { it.name.startsWith("installer-expansion-") } != false)
    }

    @Test fun rejectsDeclaredExpansionOverConfiguredBudget() {
        val wrapper = wrapper("folder/title.pkg" to (pkgHeader() + ByteArray(200)))

        val error = runCatching {
            VitaEmbeddedPackageSupport.scanZip(
                wrapper.path,
                temp.root,
                limits = InstallerExpansionLimits(maxExpandedBytes = 100, freeSpaceReserveBytes = 0)
            )
        }.exceptionOrNull()

        assertTrue(error is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, (error as InstallerExpansionException).failure)
    }

    @Test fun rejectsTraversalAndHeaderOnlyMalformedVpk() {
        val traversal = temp.newFile("traversal.zip")
        writeZip(traversal, listOf("../outside.pkg" to pkgHeader()))
        val traversalError = runCatching { VitaEmbeddedPackageSupport.scanZip(traversal.path, temp.root) }.exceptionOrNull()
        assertTrue(traversalError is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.UNSUPPORTED_PATH, (traversalError as InstallerExpansionException).failure)

        val invalid = wrapper("nested/game.vpk" to zipBytes("sce_sys/param.sfo" to "not an SFO".toByteArray()))
        val invalidError = runCatching { VitaEmbeddedPackageSupport.scanZip(invalid.path, temp.root) }.exceptionOrNull()
        assertTrue(invalidError is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.MALFORMED_PACKAGE, (invalidError as InstallerExpansionException).failure)

        val hostileVpk = zipBytes(
            "sce_sys/param.sfo" to validSfo("PCSE00004"),
            "../outside.bin" to byteArrayOf(1)
        )
        val hostileWrapper = wrapper("nested/safe-name.vpk" to hostileVpk)
        val nestedError = runCatching { VitaEmbeddedPackageSupport.scanZip(hostileWrapper.path, temp.root) }.exceptionOrNull()
        assertTrue(nestedError is InstallerExpansionException)
        assertEquals(InstallerExpansionFailure.UNSUPPORTED_PATH, (nestedError as InstallerExpansionException).failure)
    }

    private fun wrapper(vararg entries: Pair<String, ByteArray>): File =
        temp.newFile("wrapper-${System.nanoTime()}.zip").also { writeZip(it, entries.toList()) }

    private fun writeZip(file: File, entries: List<Pair<String, ByteArray>>) {
        ZipOutputStream(file.outputStream()).use { zip ->
            entries.forEach { (name, bytes) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
    }

    private fun zipBytes(vararg entries: Pair<String, ByteArray>): ByteArray =
        ByteArrayOutputStream().also { output ->
            ZipOutputStream(output).use { zip ->
                entries.forEach { (name, bytes) ->
                    zip.putNextEntry(ZipEntry(name))
                    zip.write(bytes)
                    zip.closeEntry()
                }
            }
        }.toByteArray()

    private fun vpk(titleId: String): ByteArray = zipBytes(
        "sce_sys/param.sfo" to validSfo(titleId),
        "eboot.bin" to byteArrayOf(1, 2, 3)
    )

    private fun validSfo(titleId: String): ByteArray {
        val keys = "TITLE_ID\u0000CATEGORY\u0000".toByteArray()
        val values = listOf("$titleId\u0000".toByteArray(), "gd\u0000".toByteArray())
        val dataStart = 20 + 2 * 16 + keys.size
        return ByteBuffer.allocate(dataStart + values.sumOf { it.size }).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0x46535000); putInt(0x101); putInt(52); putInt(dataStart); putInt(2)
            var offset = 0
            values.forEachIndexed { index, value ->
                putShort(if (index == 0) 0 else 9); putShort(0x204)
                putInt(value.size); putInt(value.size); putInt(offset)
                offset += value.size
            }
            put(keys); values.forEach { put(it) }
        }.array()
    }

    private fun pkgHeader(): ByteArray = byteArrayOf(0x7f, 0x50, 0x4b, 0x47) + ByteArray(20)
}
