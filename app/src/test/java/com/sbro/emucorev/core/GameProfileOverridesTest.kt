package com.sbro.emucorev.core

import android.content.Context
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class GameProfileOverridesTest {
    private lateinit var context: Context
    private lateinit var configDirectory: File
    private val titleId = "PCSE00001"

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        configDirectory = File(EmulatorStorage.runtimeRoot(context), "config")
        configDirectory.deleteRecursively()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        configDirectory.deleteRecursively()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    private fun global(update: (VitaCoreConfig) -> VitaCoreConfig) {
        val repository = VitaCoreConfigRepository(context)
        repository.save(update(repository.ensureDefaultsPersisted()))
    }

    @Test
    fun changedFieldsAreDetectedByName() {
        val before = VitaCoreConfig()
        val after = before.copy(frameLimit = 30, showInfoBar = true, customDriverName = "x")
        // the driver has its own override mode and is never an override field
        assertEquals(setOf("frameLimit", "showInfoBar"), ConfigOverrides.changed(before, after))
    }

    @Test
    fun mergeTakesOnlyTheListedFields() {
        val base = VitaCoreConfig(audioVolume = 80)
        val stored = base.copy(audioVolume = 10, frameLimit = 30)
        val merged = ConfigOverrides.merge(base, stored, setOf("frameLimit"))
        assertEquals(30, merged.frameLimit)
        assertEquals(80, merged.audioVolume)
    }

    @Test
    fun firstMenuChangePinsOnlyThatOptionAndGlobalChangesStillApply() {
        val repository = VitaGameSettingsRepository(context)
        val before = repository.loadProfile(titleId).config
        repository.saveOverrides(titleId, before.copy(showInfoBar = !before.showInfoBar), setOf("showInfoBar"))

        val profile = repository.loadProfile(titleId)
        assertEquals(setOf("showInfoBar"), profile.overrides)
        assertEquals(!before.showInfoBar, profile.config.showInfoBar)

        // a later change in the global settings reaches the game for every option the user did not touch
        global { it.copy(audioVolume = 33, frameLimit = 30) }
        val after = repository.loadProfile(titleId).config
        assertEquals(33, after.audioVolume)
        assertEquals(30, after.frameLimit)
        assertEquals(!before.showInfoBar, after.showInfoBar)
    }

    @Test
    fun overridesAccumulateAndTheFileListsThemForTheCore() {
        val repository = VitaGameSettingsRepository(context)
        val base = repository.loadProfile(titleId).config
        repository.saveOverrides(titleId, base.copy(turboMode = true), setOf("turboMode"))
        repository.saveOverrides(titleId, base.copy(turboMode = true, vSync = !base.vSync), setOf("vSync"))
        assertEquals(setOf("turboMode", "vSync"), repository.loadProfile(titleId).overrides)

        repository.syncEffectiveDriverForLaunch(titleId)
        val xml = repository.customConfigFile(titleId).readText()
        assertTrue(xml, xml.contains("turbo-mode=\"true\""))
        assertTrue(xml, xml.contains("user-overrides=\"turboMode,vSync\""))
    }

    @Test
    fun fullConfigSavesPinOnlyWhatChanged() {
        val repository = VitaGameSettingsRepository(context)
        val base = repository.loadProfile(titleId).config
        repository.saveProfile(titleId, base.copy(textureCache = !base.textureCache), null)
        assertEquals(setOf("textureCache"), repository.loadProfile(titleId).overrides)
        global { it.copy(audioVolume = 44) }
        val after = repository.loadProfile(titleId).config
        assertEquals(44, after.audioVolume)
        assertEquals(!base.textureCache, after.textureCache)
    }

    @Test
    fun legacyProfilesFollowGlobalExceptRendererAndInputAndMigrateOnTheNextChange() {
        val repository = VitaGameSettingsRepository(context)
        configDirectory.mkdirs()
        // written before user-overrides existed: every value stored, nothing marked
        repository.customConfigFile(titleId).writeText("""<?xml version="1.0" encoding="utf-8"?>
            <config>
              <gpu backend-renderer="OpenGL" texture-cache="false" disable-surface-sync="true"/>
              <audio audio-volume="50"/>
              <emucorev gpu-driver-mode="global" gamepad-swap-sticks="true"/>
            </config>""".trimIndent())
        global { it.copy(audioVolume = 70) }

        val profile = repository.loadProfile(titleId)
        assertEquals(ConfigOverrides.LEGACY_PINNED, profile.overrides)
        assertEquals("OpenGL", profile.config.backendRenderer)
        assertTrue(profile.config.gamepadSwapSticks)
        assertEquals(70, profile.config.audioVolume)
        assertTrue(profile.config.textureCache)
        assertFalse(profile.config.disableSurfaceSync)

        repository.saveOverrides(titleId, profile.config.copy(turboMode = true), setOf("turboMode"))
        assertEquals(ConfigOverrides.LEGACY_PINNED + "turboMode", repository.loadProfile(titleId).overrides)
        assertTrue(repository.customConfigFile(titleId).readText().contains("user-overrides="))

        repository.reset(titleId)
        assertFalse(repository.hasCustomConfig(titleId))
        assertEquals("Vulkan", repository.loadProfile(titleId).config.backendRenderer)
    }

    @Test
    fun keyboardMouseChangeKeepsTheOverrideSetOfAMenuProfile() {
        val repository = VitaGameSettingsRepository(context)
        val base = repository.loadProfile(titleId).config
        repository.saveOverrides(titleId, base.copy(turboMode = true), setOf("turboMode"))
        repository.updateKeyboardMouse(titleId, com.sbro.emucorev.data.KeyboardMouseSettings(mouseSensitivity = 150))
        assertEquals(setOf("turboMode"), repository.loadProfile(titleId).overrides)
        global { it.copy(audioVolume = 12) }
        assertEquals(12, repository.loadProfile(titleId).config.audioVolume)
    }

    @Test
    fun writeQueueCoalescesChangesAndNeverBlocksTheCaller() {
        val executor = Executors.newSingleThreadExecutor()
        val writes = mutableListOf<Pair<VitaCoreConfig, Set<String>>>()
        val firstStarted = CountDownLatch(1)
        val release = CountDownLatch(1)
        val done = CountDownLatch(2)
        val queue = ProfileWriteQueue(CoroutineScope(SupervisorJob()), executor.asCoroutineDispatcher()) { config, changed ->
            synchronized(writes) { writes += config to changed }
            firstStarted.countDown()
            release.await(5, TimeUnit.SECONDS)
            done.countDown()
        }
        queue.submit(VitaCoreConfig(frameLimit = 30), setOf("frameLimit"))
        assertTrue(firstStarted.await(5, TimeUnit.SECONDS))
        // submitted while the first write is still running: returns at once, merged into one follow-up write
        queue.submit(VitaCoreConfig(audioVolume = 10), setOf("audioVolume"))
        queue.submit(VitaCoreConfig(audioVolume = 20), setOf("audioVolume", "turboMode"))
        queue.submit(VitaCoreConfig(), emptySet()) // nothing changed: ignored
        release.countDown()
        assertTrue(done.await(5, TimeUnit.SECONDS))
        executor.shutdown()
        assertEquals(2, writes.size)
        assertEquals(20, writes[1].first.audioVolume)
        assertEquals(setOf("audioVolume", "turboMode"), writes[1].second)
    }
}
