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
    fun overridesAccumulateAndTheLaunchRefreshesTheFileTheCoreReads() {
        val repository = VitaGameSettingsRepository(context)
        val base = repository.loadProfile(titleId).config
        repository.saveOverrides(titleId, base.copy(turboMode = true), setOf("turboMode"))
        repository.saveOverrides(titleId, base.copy(turboMode = true, vSync = !base.vSync), setOf("vSync"))
        assertEquals(setOf("turboMode", "vSync"), repository.loadProfile(titleId).overrides)

        global { it.copy(audioVolume = 21) }
        // the core parses the XML itself: the launch step must bring the non-overridden values up to date
        repository.syncEffectiveDriverForLaunch(titleId)
        val xml = repository.customConfigFile(titleId).readText()
        assertTrue(xml, xml.contains("audio-volume=\"21\""))
        assertTrue(xml, xml.contains("turbo-mode=\"true\""))
        assertTrue(xml, xml.contains("user-overrides=\"turboMode,vSync\""))
    }

    @Test
    fun legacyFullProfilesKeepPinningEverythingAndResetToGlobalClearsThem() {
        val repository = VitaGameSettingsRepository(context)
        repository.savePreservingDriverOverride(titleId, VitaCoreConfig(audioVolume = 50))
        assertEquals(null, repository.loadProfile(titleId).overrides)
        global { it.copy(audioVolume = 70) }
        assertEquals(50, repository.loadProfile(titleId).config.audioVolume)
        // a menu change on a legacy profile stays legacy (nothing is silently unpinned)
        val config = repository.loadProfile(titleId).config
        repository.saveOverrides(titleId, config.copy(turboMode = true), setOf("turboMode"))
        assertEquals(null, repository.loadProfile(titleId).overrides)
        assertEquals(50, repository.loadProfile(titleId).config.audioVolume)

        repository.reset(titleId)
        assertFalse(repository.hasCustomConfig(titleId))
        assertEquals(70, repository.loadProfile(titleId).config.audioVolume)
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
