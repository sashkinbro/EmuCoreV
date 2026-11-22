package com.sbro.emucorev.core

import android.content.Context
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class LiveResolutionPersistenceTest {
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

    @Test
    fun liveResolutionIsStoredInTheGameProfileTheNextLaunchReads() {
        val repository = VitaGameSettingsRepository(context)
        repository.savePreservingDriverOverride(titleId, VitaCoreConfig(resolutionMultiplier = 1f))
        assertEquals(1f, repository.loadProfile(titleId).config.resolutionMultiplier)

        // what the in-game menu does after the core accepted 2.5x
        val running = repository.loadProfile(titleId).config
        repository.savePreservingDriverOverride(titleId, running.copy(resolutionMultiplier = 2.5f))
        assertEquals(2.5f, repository.loadProfile(titleId).config.resolutionMultiplier)

        // and back
        val back = repository.loadProfile(titleId).config.copy(resolutionMultiplier = 1f)
        repository.savePreservingDriverOverride(titleId, back)
        assertEquals(1f, repository.loadProfile(titleId).config.resolutionMultiplier)
    }

    @Test
    fun otherSettingsSurviveAResolutionSave() {
        val repository = VitaGameSettingsRepository(context)
        repository.savePreservingDriverOverride(titleId, VitaCoreConfig(frameLimit = 30, screenFilter = "Nearest"))
        val config = repository.loadProfile(titleId).config
        repository.savePreservingDriverOverride(titleId, config.copy(resolutionMultiplier = 3f))
        val reloaded = repository.loadProfile(titleId).config
        assertEquals(3f, reloaded.resolutionMultiplier)
        assertEquals(30, reloaded.frameLimit)
        assertEquals("Nearest", reloaded.screenFilter)
    }
}
