package com.sbro.emucorev.core

import android.content.Context
import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.w3c.dom.Element

/**
 * The per-game profile field table is shared with the native core (config/profile_overrides.h): the core applies
 * exactly the fields named in user-overrides, so every name, section and attribute must agree with what the app
 * writes, and the legacy rule must be the same on both sides.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class GameProfileFieldTableTest {
    private data class Entry(val name: String, val section: String, val attribute: String)

    private lateinit var context: Context
    private lateinit var configDirectory: File
    private val titleId = "PCSE00002"

    private val header: String by lazy {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val app = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) } ?: error("app module not found")
        app.resolve("src/main/cpp/emucorev/ECV/ECV/config/include/config/profile_overrides.h").toFile().readText()
    }

    private fun entries(visitor: String): List<Entry> {
        val body = header.substringAfter("void $visitor(").substringBefore("\n}\n")
        return Regex("""fn\("(\w+)", "(\w+)", "([\w-]+)"""").findAll(body)
            .map { Entry(it.groupValues[1], it.groupValues[2], it.groupValues[3]) }.toList()
    }

    private val shared get() = entries("visit_shared_fields")
    private val native get() = entries("visit_native_fields")

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
    fun sharedNamesAreVitaCoreConfigFieldsAndNativeOnlyNamesAreNot() {
        val fields = VitaCoreConfig::class.java.declaredFields.map { it.name }.toSet()
        assertTrue(shared.size > 30)
        shared.forEach { assertTrue("${it.name} is not a VitaCoreConfig field", it.name in fields) }
        native.forEach { assertTrue("${it.name} must stay native-only", it.name !in fields) }
    }

    @Test
    fun theAppWritesEverySharedFieldWhereTheCoreReadsIt() {
        val repository = VitaGameSettingsRepository(context)
        val config = repository.loadProfile(titleId).config.copy(audioVolume = 37, textureCache = false, guestCores = 3)
        repository.saveOverrides(titleId, config, setOf("audioVolume"))
        val root = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(repository.customConfigFile(titleId)).documentElement
        shared.forEach { entry ->
            val element = root.child(entry.section)
            assertTrue("${entry.section}/${entry.attribute} missing", element?.hasAttribute(entry.attribute) == true)
            val field = VitaCoreConfig::class.java.getDeclaredField(entry.name).apply { isAccessible = true }
            assertEquals(entry.name, field.get(config).toString(), element!!.getAttribute(entry.attribute))
        }
    }

    @Test
    fun everyAttributeTheAppWritesForTheCoreIsInTheTable() {
        // Attributes in native sections the core deliberately does not take from a profile:
        // the driver (always applied, resolved per launch) and app-only values.
        val appOnly = setOf("custom-driver-name", "cpu-pool-size", "use-angle", "frame-limit")
        val repository = VitaGameSettingsRepository(context)
        repository.saveOverrides(titleId, repository.loadProfile(titleId).config.copy(turboMode = true), setOf("turboMode"))
        val root = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(repository.customConfigFile(titleId)).documentElement
        val known = (shared + native).map { it.section to it.attribute }.toSet()
        listOf("core", "cpu", "gpu", "audio", "system", "emulator", "debug", "network").forEach { section ->
            val attributes = root.child(section)?.attributes ?: return@forEach
            for (i in 0 until attributes.length) {
                val name = attributes.item(i).nodeName
                assertTrue("$section/$name is neither in profile_overrides.h nor app-only",
                    (section to name) in known || name in appOnly)
            }
        }
    }

    @Test
    fun legacyRuleMatchesTheCore() {
        val nativeLegacy = Regex("""legacy_pinned\[\] = \{([^}]*)\}""").find(header)!!.groupValues[1]
            .split(',').map { it.trim().trim('"') }.filter { it.isNotEmpty() }.toSet()
        val sharedNames = shared.map { it.name }.toSet()
        assertEquals(nativeLegacy intersect sharedNames, ConfigOverrides.LEGACY_PINNED intersect sharedNames)
        (nativeLegacy - sharedNames).forEach { name -> assertTrue(name, native.any { it.name == name }) }
    }

    private fun Element.child(name: String): Element? {
        val nodes = getElementsByTagName(name)
        for (i in 0 until nodes.length) {
            val element = nodes.item(i) as? Element ?: continue
            if (element.parentNode == this) return element
        }
        return null
    }
}
