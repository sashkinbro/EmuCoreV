package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.BuildConfig
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class VitaCoreConfigRepositoryTest {
    private lateinit var context: Context
    private lateinit var configFile: File

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        configFile = File(EmulatorStorage.runtimeRoot(context), "config.yml")
        configFile.delete()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        configFile.delete()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test
    fun debugValidationSelectionRoundTripsFileAndPreferenceMirror() {
        assertTrue("This regression exercises the debug build policy", BuildConfig.DEBUG)
        val repository = VitaCoreConfigRepository(context)
        repository.save(VitaCoreConfig(validationLayer = true))
        assertTrue(repository.load().validationLayer)
        assertTrue(configFile.readText().contains("validation-layer: true"))
        assertEquals("true", context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE)
            .getString("validation-layer", null))

        // Native config rewrites can omit keys; the existing preference mirror must still retain the choice.
        configFile.writeText(configFile.readLines().filterNot { it.startsWith("validation-layer:") }.joinToString("\n"))
        assertTrue(VitaCoreConfigRepository(context).load().validationLayer)
    }

    @Test
    fun existingDebugValidationDoesNotAloneRewriteCurrentSchemaConfig() {
        val repository = VitaCoreConfigRepository(context)
        repository.save(VitaCoreConfig())
        configFile.writeText(configFile.readText().replace("validation-layer: false", "validation-layer: true") +
            "\ncontroller-axis-binds: []\ncontroller-binds: []\ncontroller-led-color: []\nime-langs: []\nlle-modules: []\ntracy-advanced-profiling-modules: []\n")
        val before = configFile.readBytes()
        val timestamp = 1_700_000_000_000L
        assertTrue(configFile.setLastModified(timestamp))
        assertTrue(repository.ensureDefaultsPersisted().validationLayer)
        assertTrue("Current debug validation must not trigger an atomic rewrite", before.contentEquals(configFile.readBytes()))
        assertEquals(timestamp, configFile.lastModified())
    }

    @Test
    fun defaultsAndLegacySchemaMigrationKeepValidationDisabled() {
        val repository = VitaCoreConfigRepository(context)
        assertFalse(repository.load().validationLayer)
        configFile.writeText("config-schema-version: 0\nvalidation-layer: true\n")
        assertFalse(repository.ensureDefaultsPersisted().validationLayer)
    }

    @Test
    fun releaseBuildDisablesValidationInSavedAndIncomingConfigurations() {
        assumeFalse(BuildConfig.DEBUG)
        val repository = VitaCoreConfigRepository(context)
        repository.save(VitaCoreConfig(validationLayer = true))
        assertFalse(repository.load().validationLayer)
        assertTrue(configFile.readText().contains("validation-layer: false"))
        configFile.writeText(configFile.readText().replace("validation-layer: false", "validation-layer: true"))
        assertFalse(repository.ensureDefaultsPersisted().validationLayer)
        assertTrue(configFile.readText().contains("validation-layer: false"))
    }
}
