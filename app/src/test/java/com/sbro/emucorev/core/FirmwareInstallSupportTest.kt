package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FirmwareInstallSupportTest {
    @Test
    fun parsesSuccessWithPartitions() {
        val result = FirmwareInstallResult.parse("OK|3.74|os0:69,vs0:1473")
        assertTrue(result.success)
        assertEquals("3.74", result.version)
        assertEquals(mapOf("os0" to 69L, "vs0" to 1473L), result.partitionFiles)
    }

    @Test
    fun successWithEmptyVersionIsStillSuccess() {
        // B031: a package without version.txt used to be reported as a failed install although it was extracted.
        val result = FirmwareInstallResult.parse("OK||vs0:3")
        assertTrue(result.success)
        assertEquals("", result.version)
    }

    @Test
    fun parsesEveryFailureStatus() {
        listOf("NOT_PUP", "TRUNCATED", "CORRUPT", "IO_ERROR", "CANCELLED", "INTERNAL").forEach { name ->
            val result = FirmwareInstallResult.parse("$name|some detail|with bar")
            assertEquals(name, result.status.name)
            assertFalse(result.success)
            assertEquals("some detail|with bar", result.detail)
        }
    }

    @Test
    fun noSpaceCarriesRequiredBytes() {
        val result = FirmwareInstallResult.parse("NO_SPACE|366000000")
        assertEquals(FirmwareInstallStatus.NO_SPACE, result.status)
        assertEquals(366_000_000L, result.requiredBytes)
    }

    @Test
    fun garbageAndNullAreInternalFailures() {
        assertEquals(FirmwareInstallStatus.INTERNAL, FirmwareInstallResult.parse(null).status)
        assertEquals(FirmwareInstallStatus.INTERNAL, FirmwareInstallResult.parse("").status)
        assertEquals(FirmwareInstallStatus.INTERNAL, FirmwareInstallResult.parse("WAT|x").status)
    }

    @Test
    fun preflightRequiresThreeTimesThePackagePlusMargin() {
        val pup = 133_834_240L
        val needed = FirmwareInstallPreflight.requiredBytes(pup)
        assertEquals(pup * 3 + 64L * 1024 * 1024, needed)
        assertTrue(FirmwareInstallPreflight.hasEnoughSpace(needed, pup))
        assertFalse(FirmwareInstallPreflight.hasEnoughSpace(needed - 1, pup))
        assertFalse(FirmwareInstallPreflight.hasEnoughSpace(10L * 1024 * 1024, pup))
        assertTrue(FirmwareInstallPreflight.hasEnoughSpace(Long.MAX_VALUE, pup))
        assertEquals(FirmwareInstallPreflight.MARGIN_BYTES, FirmwareInstallPreflight.requiredBytes(-5))
    }

    @Test
    fun progressDetailParsing() {
        val extract = FirmwareProgressDetail.parse("extract:vs0:1234")!!
        assertEquals(FirmwareProgressDetail.Phase.EXTRACT, extract.phase)
        assertEquals("vs0", extract.partition)
        assertEquals(1234L, extract.files)
        assertEquals(FirmwareProgressDetail.Phase.DECRYPT, FirmwareProgressDetail.parse("decrypt:os0:0")!!.phase)
        assertEquals(FirmwareProgressDetail.Phase.COMMIT, FirmwareProgressDetail.parse("commit::5")!!.phase)
        assertEquals(FirmwareProgressDetail.Phase.VALIDATE, FirmwareProgressDetail.parse("validate::0")!!.phase)
        assertNull(FirmwareProgressDetail.parse(null))
        assertNull(FirmwareProgressDetail.parse("nonsense"))
        assertNull(FirmwareProgressDetail.parse("bogus:vs0:1"))
    }

    @Test
    fun pinnedSourcesAreWellFormed() {
        listOf(FirmwareSources.base, FirmwareSources.update).forEach { source ->
            assertEquals(64, source.sha256.length)
            assertTrue(source.sha256.all { it in '0'..'9' || it in 'a'..'f' })
            assertTrue(source.sizeBytes > 0)
            // https first, the published http URL only as the fallback
            assertEquals(2, source.candidateUrls.size)
            assertTrue(source.candidateUrls[0].startsWith("https://"))
            assertTrue(source.candidateUrls[1].startsWith("http://"))
            assertEquals(source.candidateUrls[0].removePrefix("https://"), source.candidateUrls[1].removePrefix("http://"))
        }
        // exact sizes of the files served by the update servers (Content-Length)
        assertEquals(133_834_240L, FirmwareSources.base.sizeBytes)
        assertEquals(56_778_752L, FirmwareSources.update.sizeBytes)
    }
}
