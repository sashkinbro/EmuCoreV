package com.sbro.emucorev.core

import java.nio.file.Files
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [29], manifest = Config.NONE)
class GpuDriverEvidenceIntegrationTest {
    private val mali = DeviceGpuInfoResolver.resolve("Mali-G715 MC7", null)
    private val adreno = DeviceGpuInfoResolver.resolve("Adreno 740", null)
    private fun driver(id: String = "panvk-1", family: GpuDriverFamily = GpuDriverFamily.PANVK,
        gpu: String = "Mali Bifrost/Valhall", models: List<String> = emptyList()) = RemoteGpuDriver(
        id, id, "", gpu, "", false, "https://example.test/$id.zip", "", "", null,
        models, family, "1.0", "v1.0")

    private fun elf(marker: String = "panvk"): ByteArray = ByteArray(256).also { bytes ->
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        bytes[0] = 0x7f; bytes[1] = 'E'.code.toByte(); bytes[2] = 'L'.code.toByte(); bytes[3] = 'F'.code.toByte()
        bytes[4] = 2; bytes[5] = 1; bytes[6] = 1
        b.putShort(16, 3); b.putShort(18, 183); b.putInt(20, 1); b.putLong(32, 64)
        b.putShort(52, 64); b.putShort(54, 56); b.putShort(56, 1)
        b.putInt(64, 1); b.putLong(72, 0); b.putLong(96, bytes.size.toLong()); b.putLong(104, bytes.size.toLong())
        marker.toByteArray(Charsets.US_ASCII).copyInto(bytes, 160)
    }

    private fun zip(vararg files: Pair<String, ByteArray>): ByteArray = ByteArrayOutputStream().also { output ->
        ZipOutputStream(output).use { zip -> files.forEach { (name, bytes) ->
            zip.putNextEntry(ZipEntry(name)); zip.write(bytes); zip.closeEntry()
        } }
    }.toByteArray()

    private fun <T> store(device: DeviceGpuSnapshot = mali, action: (GpuDriverStore, File) -> T): T {
        val root = Files.createTempDirectory("gpu-driver-evidence-").toFile()
        return try { action(GpuDriverStore(root) { device }, root) } finally { root.deleteRecursively() }
    }

    private fun legacy(root: File, name: String, library: String = "libvulkan.so", marker: String = "panvk") =
        File(root, name).apply {
            mkdirs(); File(this, library).writeBytes(elf(marker)); File(this, "driver_name.txt").writeText(library, Charsets.UTF_8)
        }
    @Test fun physicalRendererWinsOverConflictingSocCatalogRecommendation() {
        val measured = GpuModelInfo(GpuVendor.ADRENO, "Adreno 750")
        assertEquals("Adreno 750", GpuDriverRecommendations.profileForGpu("Snapdragon 8 Gen 2", measured)?.adrenoName)
    }

    @Test fun unknownPackageOrDeviceVendorIsNotCompatible() {
        assertFalse(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.UNKNOWN, GpuDriverVendor.MALI))
        assertFalse(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.MALI, GpuDriverVendor.UNKNOWN))
    }

    @Test fun fourByteElfMagicDoesNotProveAValidArm64SharedLibrary() {
        val file = Files.createTempFile("driver-header-", ".so").toFile()
        try {
            file.writeBytes(byteArrayOf(0x7f, 'E'.code.toByte(), 'L'.code.toByte(), 'F'.code.toByte()))
            assertFalse(isValidDriverLibrary(file))
        } finally { file.delete() }
    }

    @Test fun catalogParsingPreservesExactVersionModelsAndDoesNotInventAdrenoOrNewerDeclarations() {
        val rows = parseGpuDriverCatalog("""[
          {"id":"mali-sx2-old","downloadUrl":"https://example.test/a.zip","family":"Mali","version":"0.1","tag":"v0.1","gpu":"Mali","supportedGpuModels":["G57","G78"]},
          {"id":"mali-sx2-new","downloadUrl":"https://example.test/b.zip","family":"Mali","version":"0.5","tag":"v0.5","gpu":"Mali","supportedGpuModels":["G715"]},
          {"id":"unknown","downloadUrl":"https://example.test/c.zip"}
        ]""")
        assertEquals(listOf("G57", "G78"), rows[0].supportedGpuModels)
        assertEquals("0.1", rows[0].version)
        assertEquals("v0.1", rows[0].tag)
        assertEquals(GpuDriverFamily.MALI_SX2, rows[0].family)
        assertTrue(rows[2].supportedGpuModels.isEmpty())
        assertEquals("", rows[2].gpu)
        assertEquals(GpuDriverVendor.UNKNOWN, rows[2].vendor)
    }

    @Test fun genericProprietaryCatalogRowsAreNotInventedPanvkOrTurnip() {
        val rows = parseGpuDriverCatalog("""[
          {"id":"arm-stock","name":"ARM Mali G57","family":"Mali","gpu":"Mali-G57 (T820)","downloadUrl":"https://example.test/a.zip"},
          {"id":"qualcomm-stock","family":"Qualcomm","gpu":"Adreno 8xx","downloadUrl":"https://example.test/b.zip"}
        ]""")
        assertTrue(rows.all { it.family == GpuDriverFamily.OTHER })
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED, GpuDriverCompatibility.catalogCompatibility(rows[0], mali).state)
    }

    @Test fun sx2CatalogEligibilityIsMeasuredAndVersionSpecificWithoutNewestFallback() {
        val old = driver("mali-sx2-old", GpuDriverFamily.MALI_SX2, models = listOf("G57"))
        val latest = old.copy(id = "mali-sx2-latest", supportedGpuModels = listOf("G715"))
        assertEquals(GpuDriverCompatibilityState.INCOMPATIBLE, GpuDriverCompatibility.catalogCompatibility(old, mali).state)
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, GpuDriverCompatibility.catalogCompatibility(latest, mali).state)
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED,
            GpuDriverCompatibility.catalogCompatibility(latest, mali.copy(source = GpuInfoSource.SOC_CATALOG)).state)
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED,
            GpuDriverCompatibility.catalogCompatibility(old.copy(supportedGpuModels = emptyList()), mali).state)
    }

    @Test fun exactAdrenoPackageRequirementsRejectWrongGenerationExplicitModelAndMissingDeclarations() {
        val pkg = InstalledGpuDriver("turnip", "libvulkan.so", "/not-used", true, GpuDriverVendor.ADRENO,
            GpuDriverFamily.TURNIP, fileState = GpuDriverFileState.VALID, gpu = "Adreno 8xx (830/840)")
        for (gpu in listOf("Adreno 740", "Adreno 810")) {
            val result = GpuDriverCompatibility.resolveSelection(pkg.name, null,
                DeviceGpuInfoResolver.resolve(gpu, null), listOf(pkg))
            assertEquals(gpu, GpuDriverSelectionDecision.INCOMPATIBLE_HARDWARE, result.decision)
            assertEquals(GpuDriverCompatibilityReason.PACKAGE_MODEL_NOT_DECLARED, result.compatibility?.reason)
            assertFalse(result.requestable)
        }
        val elite = DeviceGpuInfoResolver.resolve("Adreno 830", null)
        val good = GpuDriverCompatibility.resolveSelection(pkg.name, null, elite, listOf(pkg))
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_NATIVE, good.decision)
        assertTrue(good.requestable)
        val missing = GpuDriverCompatibility.resolveSelection(pkg.name, null, elite, listOf(pkg.copy(gpu = "Adreno")))
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_HARDWARE, missing.decision)
        assertEquals(GpuDriverCompatibilityReason.PACKAGE_REQUIREMENTS_UNVERIFIED, missing.compatibility?.reason)
    }

    @Test fun installedExactMetadataRetainsModelsVersionAndUnverifiedNativeCapability() = store { store, _ ->
        val remote = driver("mali-sx2-0.5", GpuDriverFamily.MALI_SX2, models = listOf("G715"))
        val name = store.installFromArchive(ByteArrayInputStream(zip("libvulkan_malisx2.so" to elf("mali_kbase"))),
            "SX2 0.5.zip", catalogDriver = remote)
        val installed = store.listInstalledDrivers().single()
        assertEquals("SX2 0.5", name)
        assertEquals(remote.id, installed.catalogId)
        assertEquals(remote.version, installed.version)
        assertEquals(remote.tag, installed.tag)
        assertEquals(listOf("G715"), installed.supportedGpuModels)
        assertEquals(GpuDriverFileState.VALID, installed.fileState)
        assertEquals(GpuDriverNativeCapability.UNKNOWN, installed.nativeCapability)
        assertTrue(installed.isUsable)
        assertEquals(installed.mainLibraryPath, store.readMainLibraryPath(name))
    }

    @Test fun installedLegacySx2CannotBorrowModelsFromNewerInstalledPackage() = store { store, root ->
        val old = legacy(root, "SX2 old", "libvulkan_malisx2.so", "mali_kbase")
        File(old, "driver_vendor.txt").writeText("MALI", Charsets.UTF_8)
        store.installFromArchive(ByteArrayInputStream(zip("libvulkan_malisx2.so" to elf("mali_kbase"))),
            "SX2 new.zip", catalogDriver = driver("mali-sx2-new", GpuDriverFamily.MALI_SX2, models = listOf("G715")))
        val listed = store.listInstalledDrivers().associateBy { it.name }
        assertTrue(listed.getValue("SX2 old").supportedGpuModels.isEmpty())
        assertFalse(listed.getValue("SX2 old").isUsable)
        assertTrue(listed.getValue("SX2 new").isUsable)
        assertNull(store.readMainLibraryPath("SX2 old"))
    }

    @Test fun changingLibraryInvalidatesItsExactVersionEvidence() = store { store, root ->
        store.installFromArchive(ByteArrayInputStream(zip("libvulkan.so" to elf())), "one.zip", catalogDriver = driver())
        File(root, "one/libvulkan.so").appendBytes(byteArrayOf(1))
        val installed = store.listInstalledDrivers().single()
        assertEquals(GpuDriverFileState.UNKNOWN, installed.fileState)
        assertNull(installed.catalogId)
        assertFalse(installed.isUsable)
        assertNull(store.readMainLibraryPath("one"))
    }

    @Test fun invalidReinstallAndWrongChecksumPreserveExistingPackage() = store { store, root ->
        legacy(root, "one")
        val before = File(root, "one/libvulkan.so").readBytes()
        assertThrows(IllegalStateException::class.java) {
            store.installFromArchive(ByteArrayInputStream(zip("libvulkan.so" to byteArrayOf(1, 2))), "one.zip")
        }
        assertArrayEquals(before, File(root, "one/libvulkan.so").readBytes())
        assertThrows(IllegalStateException::class.java) {
            store.installFromArchive(ByteArrayInputStream(zip("libvulkan.so" to elf())), "one.zip",
                catalogDriver = driver().copy(archiveSha256 = "0".repeat(64)))
        }
        assertArrayEquals(before, File(root, "one/libvulkan.so").readBytes())
        assertTrue(root.listFiles().orEmpty().none { it.name.startsWith(".") })
    }

    @Test fun managedSelectedPathNeverChoosesAnotherInstalledPackage() = store { store, root ->
        val good = legacy(root, "good")
        val path = File(good, "libvulkan.so").canonicalPath
        assertEquals(path, store.resolveUsableDriverPath(path))
        assertNull(store.resolveUsableDriverPath(null))
        assertNull(store.resolveUsableDriverPath(""))
        assertNull(store.resolveUsableDriverPath(File(root, "missing/libvulkan.so").path))
        assertNull(store.resolveUsableDriverPath(File(root, "missing/../good/libvulkan.so").path))
        assertNull(store.readMainLibraryPath("missing"))
    }

    @Test fun corruptMetadataPathsAndArchiveTraversalDoNotEscapePackageRoot() = store { store, root ->
        val good = legacy(root, "good")
        val before = File(good, "libvulkan.so").readBytes()
        val bad = File(root, "bad").apply { mkdir() }
        File(bad, "driver_name.txt").writeText("../good/libvulkan.so", Charsets.UTF_8)
        assertNull(store.readMainLibraryPath("bad"))
        assertNull(store.readMainLibraryPath("../good"))
        assertThrows(IllegalStateException::class.java) {
            store.installFromArchive(ByteArrayInputStream(zip("../good/libvulkan.so" to elf("bad"))), "bad.zip")
        }
        assertArrayEquals(before, File(good, "libvulkan.so").readBytes())
    }

    @Test fun proprietaryCatalogFamilyIsNotPromotedByIncidentalMesaMarker() = store(
        DeviceGpuInfoResolver.resolve("Adreno 830", null)) { store, _ ->
        val stock = driver("qualcomm-stock", GpuDriverFamily.OTHER, "Adreno 8xx")
        store.installFromArchive(ByteArrayInputStream(zip("libvulkan.so" to elf("adreno freedreno"))),
            "stock.zip", catalogDriver = stock)
        val installed = store.listInstalledDrivers().single()
        assertEquals(GpuDriverFamily.OTHER, installed.family)
        assertFalse(installed.isUsable)
    }

    @Test fun elfValidationRejectsArchitectureTableAndSegmentTruncation() = store { _, root ->
        val file = File(root, "validation.so")
        file.writeBytes(elf()); assertTrue(isValidDriverLibrary(file))
        for (bytes in listOf(elf().also { it[4] = 1 }, elf().also { it[18] = 62 }, elf().copyOf(100),
            elf().also { ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN).putLong(96, Long.MAX_VALUE) })) {
            file.writeBytes(bytes); assertFalse(isValidDriverLibrary(file))
        }
    }

    @Test fun legacyManualTurnipMayAttemptWithoutInventingExactPackageCompatibility() = store(adreno) { store, root ->
        legacy(root, "manual turnip", marker = "turnip")
        val installed = store.listInstalledDrivers().single()
        assertEquals(GpuDriverFamily.TURNIP, installed.family)
        assertTrue(installed.isUsable)
        val result = GpuDriverCompatibility.resolveSelection(installed.name, null, adreno, listOf(installed))
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED, result.compatibility?.state)
        assertEquals(GpuDriverCompatibilityReason.PACKAGE_REQUIREMENTS_UNVERIFIED, result.compatibility?.reason)
        assertTrue(result.requestable)
        assertEquals(installed.name, result.nextLaunchName)
        assertEquals(GpuDriverNativeCapability.UNKNOWN, result.nativeCapability)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
    }

    @Test fun unverifiedManualTurnipCannotBypassExplicitNativeUnsupported() {
        val pkg = InstalledGpuDriver("manual", "libvulkan.so", "/unused", false, GpuDriverVendor.ADRENO,
            GpuDriverFamily.TURNIP, fileState = GpuDriverFileState.VALID,
            nativeCapability = GpuDriverNativeCapability.UNSUPPORTED)
        val result = GpuDriverCompatibility.resolveSelection(pkg.name, null, adreno, listOf(pkg))
        assertFalse(result.requestable)
        assertEquals("", result.nextLaunchName)
        assertEquals(GpuDriverSelectionDecision.NATIVE_UNSUPPORTED, result.decision)
        assertEquals(GpuDriverNativeCapability.UNSUPPORTED, result.nativeCapability)
    }
}
