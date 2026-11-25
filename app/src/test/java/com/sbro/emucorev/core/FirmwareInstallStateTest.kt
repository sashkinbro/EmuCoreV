package com.sbro.emucorev.core

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File
import java.nio.file.Files

class FirmwareInstallStateTest {
    private lateinit var root: File

    @Before
    fun setUp() {
        root = Files.createTempDirectory("fwstate").toFile()
    }

    @After
    fun tearDown() {
        root.deleteRecursively()
    }

    private fun write(relative: String, text: String = "x") {
        File(root, relative).apply { parentFile.mkdirs() }.writeText(text)
    }

    private fun marker(vararg counts: Pair<String, Long>, version: String = "3.74") = buildString {
        append("emucorev-firmware-marker 1\n")
        append("version=$version\n")
        counts.forEach { (name, count) -> append("$name.files=$count\n") }
    }

    @Test
    fun emptyAndMissingRootAreNotInstalled() {
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
        assertFalse(FirmwareInstallState.isFirmwareInstalled(File(root, "nope")))
        assertFalse(FirmwareInstallState.isFontPackageInstalled(root))
    }

    @Test
    fun anyFileUnderVs0IsNoLongerEnough() {
        // The old check treated this half-written tree as an installed firmware.
        write("vs0/data/something.bin")
        write("vs0/vsh/other.self")
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
    }

    @Test
    fun emptyKeyFileIsNotInstalled() {
        write(FirmwareInstallState.SHELL_KEY_FILE, "")
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
    }

    @Test
    fun completeInstallWithMarkerIsInstalled() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.MARKER_NAME, marker("os0" to 69L, "vs0" to 1473L))
        assertTrue(FirmwareInstallState.isFirmwareInstalled(root))
        assertEquals("3.74", FirmwareInstallState.installedVersion(root))
        assertEquals(setOf("os0", "vs0"), FirmwareInstallState.readMarker(root)?.partitions)
        // fonts not installed yet
        assertFalse(FirmwareInstallState.isFontPackageInstalled(root))
    }

    @Test
    fun markerWithoutVs0CountDoesNotCountAsBaseFirmware() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.MARKER_NAME, marker("sa0" to 178L))
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
    }

    @Test
    fun corruptMarkerMeansNotInstalled() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.MARKER_NAME, "garbage\nversion=3.74\nvs0.files=10\n")
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
        assertNull(FirmwareInstallState.readMarker(root))
    }

    @Test
    fun zeroFileCountMeansNotInstalled() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.MARKER_NAME, marker("vs0" to 0L))
        assertFalse(FirmwareInstallState.isFirmwareInstalled(root))
    }

    @Test
    fun legacyInstallWithoutMarkerIsAcceptedOnKeyFiles() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.FONT_KEY_FILE)
        assertTrue(FirmwareInstallState.isFirmwareInstalled(root))
        assertTrue(FirmwareInstallState.isFontPackageInstalled(root))
        assertNull(FirmwareInstallState.installedVersion(root))
    }

    @Test
    fun fontPackageNeedsItsKeyFileAndMarkerCoverage() {
        write(FirmwareInstallState.SHELL_KEY_FILE)
        write(FirmwareInstallState.MARKER_NAME, marker("vs0" to 5L, "sa0" to 178L))
        assertFalse(FirmwareInstallState.isFontPackageInstalled(root)) // marker says sa0, files missing
        write(FirmwareInstallState.FONT_KEY_FILE)
        assertTrue(FirmwareInstallState.isFontPackageInstalled(root))
        write(FirmwareInstallState.MARKER_NAME, marker("vs0" to 5L))
        assertFalse(FirmwareInstallState.isFontPackageInstalled(root)) // sa0 tree without a marker entry
    }

    @Test
    fun markerParserToleratesCrLfAndUnknownLines() {
        val parsed = FirmwareInstallState.parseMarker(
            "emucorev-firmware-marker 1\r\nversion=3.74\r\nfuture=thing\r\nvs0.files=7\r\nsa0.files=x\r\n"
        )
        assertEquals("3.74", parsed?.version)
        assertEquals(mapOf("vs0" to 7L), parsed?.fileCounts)
        assertNull(FirmwareInstallState.parseMarker(""))
    }

    @Test
    fun markerWrittenByNativeInstallerRoundTrips() {
        // Same text the host tests assert the native installer writes.
        val text = "emucorev-firmware-marker 1\nversion=3.74\nos0.files=69\nsa0.files=178\nvs0.files=1473\n"
        val parsed = FirmwareInstallState.parseMarker(text)!!
        assertEquals("3.74", parsed.version)
        assertEquals(mapOf("os0" to 69L, "sa0" to 178L, "vs0" to 1473L), parsed.fileCounts)
    }
}
