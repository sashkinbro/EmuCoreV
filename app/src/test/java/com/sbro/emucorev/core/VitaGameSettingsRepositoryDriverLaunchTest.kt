package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.KeyboardMouseSettings
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowBuild

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [29], manifest = Config.NONE)
class VitaGameSettingsRepositoryDriverLaunchTest {
    private lateinit var context: Context
    private lateinit var repository: VitaGameSettingsRepository
    private lateinit var global: VitaCoreConfigRepository
    private val title = "PCSE99998"

    @Before fun setup() {
        context = RuntimeEnvironment.getApplication()
        ShadowBuild.setHardware("SM8550")
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
        File(EmulatorStorage.runtimeRoot(context), "config.yml").delete()
        File(EmulatorStorage.runtimeRoot(context), "config").deleteRecursively()
        File(context.filesDir, "driver").deleteRecursively()
        global = VitaCoreConfigRepository(context)
        repository = VitaGameSettingsRepository(context)
    }

    @After fun cleanup() {
        File(EmulatorStorage.runtimeRoot(context), "config").deleteRecursively()
        File(EmulatorStorage.runtimeRoot(context), "config.yml").delete()
        File(context.filesDir, "driver").deleteRecursively()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    private fun attribute(element: String, name: String): String = DocumentBuilderFactory.newInstance()
        .newDocumentBuilder().parse(repository.customConfigFile(title)).documentElement
        .getElementsByTagName(element).item(0).attributes.getNamedItem(name)?.nodeValue.orEmpty()

    private fun installLegacyTurnip(name: String) {
        val dir = File(context.filesDir, "driver/$name").apply { mkdirs() }
        val bytes = ByteArray(256)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        bytes[0] = 0x7f; bytes[1] = 'E'.code.toByte(); bytes[2] = 'L'.code.toByte(); bytes[3] = 'F'.code.toByte()
        bytes[4] = 2; bytes[5] = 1; bytes[6] = 1
        b.putShort(16, 3); b.putShort(18, 183); b.putInt(20, 1); b.putLong(32, 64)
        b.putShort(52, 64); b.putShort(54, 56); b.putShort(56, 1)
        b.putInt(64, 1); b.putLong(72, 0); b.putLong(96, bytes.size.toLong()); b.putLong(104, bytes.size.toLong())
        "turnip".toByteArray(Charsets.US_ASCII).copyInto(bytes, 160)
        File(dir, "libvulkan.so").writeBytes(bytes)
        File(dir, "driver_name.txt").writeText("libvulkan.so", Charsets.UTF_8)
    }

    @Test fun inheritedMissingDriverUsesRuntimeOnlySystemWithoutPersistingGameOverride() {
        global.save(VitaCoreConfig(customDriverName = "missing", audioVolume = 73))
        assertEquals("", repository.syncEffectiveDriverForLaunch(title).customDriverName)
        assertEquals("", attribute("gpu", "custom-driver-name"))
        assertEquals("true", attribute("emucorev", "gpu-driver-launch-only"))
        assertFalse(repository.hasCustomConfig(title))
        assertNull(repository.loadProfile(title).customDriverOverride)
        assertEquals("missing", repository.loadProfile(title).config.customDriverName)
        assertEquals("missing", global.load().customDriverName)
        global.save(global.load().copy(audioVolume = 41, customDriverName = "another missing"))
        assertEquals(41, repository.loadProfile(title).config.audioVolume)
        assertEquals("another missing", repository.loadProfile(title).config.customDriverName)
    }

    @Test fun realCustomRequestSurvivesSystemProjectionWithKeyboardMouseAndOtherSettings() {
        global.save(VitaCoreConfig(customDriverName = "global"))
        val keyboard = KeyboardMouseSettings(mouseSensitivity = 175)
        repository.saveProfile(title, VitaCoreConfig(pstvMode = true, audioVolume = 37), "exact missing", keyboard)
        val requested = repository.loadProfile(title)
        val effective = repository.syncEffectiveDriverForLaunch(title)
        assertEquals("", effective.customDriverName)
        assertEquals("exact missing", attribute("emucorev", "gpu-driver-requested"))
        assertEquals("", attribute("gpu", "custom-driver-name"))
        assertTrue(repository.hasCustomConfig(title))
        assertEquals(requested, repository.loadProfile(title))
        assertEquals(keyboard, repository.loadProfile(title).keyboardMouse)
        assertTrue(repository.loadProfile(title).config.pstvMode)
        assertEquals(37, repository.loadProfile(title).config.audioVolume)
    }

    @Test fun runtimeOnlyDocumentIsNotARealProfileOrFrozenGlobalSnapshot() {
        global.save(VitaCoreConfig(customDriverName = "global", audioVolume = 56))
        repository.customConfigFile(title).apply {
            parentFile.mkdirs()
            writeText("""<config><gpu custom-driver-name=""/><audio audio-volume="1"/><emucorev gpu-driver-mode="global" gpu-driver-launch-only="true"/></config>""", Charsets.UTF_8)
        }
        assertFalse(repository.hasCustomConfig(title))
        assertEquals(56, repository.loadProfile(title).config.audioVolume)
        assertEquals("global", repository.loadProfile(title).config.customDriverName)
    }

    @Test fun requestedOwnAttributeIsReadOnlyForCustomMode() {
        global.save(VitaCoreConfig(customDriverName = "global"))
        repository.customConfigFile(title).apply {
            parentFile.mkdirs()
            writeText("""<config><gpu custom-driver-name=""/><emucorev gpu-driver-mode="custom" gpu-driver-requested="exact requested"/></config>""", Charsets.UTF_8)
        }
        assertEquals("exact requested", repository.loadProfile(title).customDriverOverride)
        repository.customConfigFile(title).writeText(repository.customConfigFile(title).readText(Charsets.UTF_8)
            .replace("mode=\"custom\"", "mode=\"global\""), Charsets.UTF_8)
        assertNull(repository.loadProfile(title).customDriverOverride)
        assertEquals("global", repository.loadProfile(title).config.customDriverName)
    }

    @Test fun staleRuntimeOnlyFallbackIsDeletedWhenGlobalBecomesSystem() {
        global.save(VitaCoreConfig(customDriverName = "missing"))
        repository.syncEffectiveDriverForLaunch(title)
        assertTrue(repository.customConfigFile(title).isFile)
        global.save(global.load().copy(customDriverName = "", audioVolume = 42))
        assertEquals("", repository.syncEffectiveDriverForLaunch(title).customDriverName)
        assertFalse(repository.customConfigFile(title).exists())
        assertFalse(repository.hasCustomConfig(title))
        assertEquals(42, repository.loadProfile(title).config.audioVolume)
    }

    @Test fun staleRuntimeOnlyFallbackIsDeletedWhenExactLegacyTurnipReappears() {
        global.save(VitaCoreConfig(customDriverName = "manual"))
        assertEquals("", repository.syncEffectiveDriverForLaunch(title).customDriverName)
        installLegacyTurnip("manual")
        assertEquals(GpuInfoSource.SOC_CATALOG, DeviceGpuInfoProvider.get(context).source)
        assertEquals("manual", repository.syncEffectiveDriverForLaunch(title).customDriverName)
        assertFalse(repository.customConfigFile(title).exists())
        assertFalse(repository.hasCustomConfig(title))
        assertNull(repository.loadProfile(title).customDriverOverride)
        assertEquals("manual", global.load().customDriverName)
    }

    @Test fun realCustomRequestIsRestoredWhenItsExactPackageReappears() {
        global.save(VitaCoreConfig())
        repository.saveProfile(title, VitaCoreConfig(audioVolume = 38), "manual")
        repository.syncEffectiveDriverForLaunch(title)
        installLegacyTurnip("manual")
        assertEquals("manual", repository.syncEffectiveDriverForLaunch(title).customDriverName)
        assertEquals("manual", attribute("gpu", "custom-driver-name"))
        assertEquals("manual", attribute("emucorev", "gpu-driver-requested"))
        assertEquals("manual", repository.loadProfile(title).customDriverOverride)
        assertEquals(38, repository.loadProfile(title).config.audioVolume)
    }

    @Test fun normalSaveReplacesRuntimeMarkerAndResetRemovesTheRealProfile() {
        global.save(VitaCoreConfig(customDriverName = "missing"))
        repository.syncEffectiveDriverForLaunch(title)
        repository.saveProfile(title, repository.loadProfile(title).config.copy(pstvMode = true), null,
            KeyboardMouseSettings(mouseSensitivity = 150))
        assertTrue(repository.hasCustomConfig(title))
        assertEquals("", attribute("emucorev", "gpu-driver-launch-only"))
        assertNull(repository.loadProfile(title).customDriverOverride)
        repository.syncEffectiveDriverForLaunch(title)
        assertTrue(repository.loadProfile(title).config.pstvMode)
        assertEquals(150, repository.loadProfile(title).keyboardMouse?.mouseSensitivity)
        repository.reset(title)
        assertFalse(repository.hasCustomConfig(title))
        assertFalse(repository.customConfigFile(title).exists())
        assertNull(repository.loadProfile(title).keyboardMouse)
    }

    @Test fun explicitGameSystemRemainsDifferentFromGlobalInheritance() {
        global.save(VitaCoreConfig(customDriverName = "global missing"))
        repository.saveProfile(title, VitaCoreConfig(), "")
        repository.syncEffectiveDriverForLaunch(title)
        assertEquals("system", attribute("emucorev", "gpu-driver-mode"))
        assertEquals("", repository.loadProfile(title).customDriverOverride)
        assertEquals("", repository.loadProfile(title).config.customDriverName)
        assertEquals("global missing", global.load().customDriverName)
    }

    @Test fun driverProjectionPreservesUnrecognizedXmlAttributesAndElements() {
        global.save(VitaCoreConfig())
        repository.saveProfile(title, VitaCoreConfig(audioVolume = 38), "missing")
        val file = repository.customConfigFile(title)
        file.writeText(file.readText(Charsets.UTF_8).replace("<gpu ", "<gpu future-native=\"kept\" ")
            .replace("</config>", "<future-extension value=\"kept\"/></config>"), Charsets.UTF_8)
        repository.syncEffectiveDriverForLaunch(title)
        assertEquals("kept", attribute("gpu", "future-native"))
        assertEquals("kept", attribute("future-extension", "value"))
        assertEquals(38, repository.loadProfile(title).config.audioVolume)
    }

    @Test fun blankTitleReturnsSafeProjectionWithoutCreatingPerGameArtifact() {
        global.save(VitaCoreConfig(customDriverName = "missing"))
        assertEquals("", repository.syncEffectiveDriverForLaunch("  ").customDriverName)
        assertFalse(repository.hasCustomConfig("  "))
        assertFalse(File(EmulatorStorage.runtimeRoot(context), "config").exists())
        assertEquals("missing", global.load().customDriverName)
    }

    @Test fun clearingAbsentKeyboardOverrideDoesNotPromoteRuntimeOnlyDocumentToRealProfile() {
        global.save(VitaCoreConfig(customDriverName = "missing", audioVolume = 71))
        repository.syncEffectiveDriverForLaunch(title)
        val file = repository.customConfigFile(title)
        val before = file.readText(Charsets.UTF_8)
        val profile = repository.loadProfile(title)
        repository.updateKeyboardMouse(title, profile.config, profile.customDriverOverride, null)
        assertFalse(repository.hasCustomConfig(title))
        assertEquals(before, file.readText(Charsets.UTF_8))
        global.save(global.load().copy(audioVolume = 43))
        assertEquals(43, repository.loadProfile(title).config.audioVolume)
    }
}
