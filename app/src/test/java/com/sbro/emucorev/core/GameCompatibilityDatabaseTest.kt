package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.*
import org.junit.Test

class GameCompatibilityDatabaseTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }
    private val xml = app.resolve("src/main/assets/${GameCompatibilityDatabase.ASSET_PATH}").readText()
    private fun database(text: String = xml) = text.byteInputStream().use(GameCompatibilityDatabase::parse)

    @Test fun allRegionalIdsAndRenamedModsReceiveRecommendations() {
        val ids = Regex("<title-id>([^<]+)</title-id>").findAll(xml).map { it.groupValues[1] }.toSet()
        assertEquals(31, ids.size)
        val db = database()
        ids.forEach { id ->
            val recommended = db.recommendationFor(id, "EA SPORTS FC mod")!!
            assertEquals("native-buffer", recommended.memoryMapping)
            assertEquals("Vulkan", recommended.backendRenderer)
            assertFalse(recommended.useAngle)
        }
        assertNotNull(db.recommendationFor(" pcse00483 "))
    }

    @Test fun familyNamesAreDataDrivenAndDoNotMatchUnrelatedWords() {
        val db = database()
        listOf("FIFA Football", "EA SPORTS™ FIFA Soccer Demo", "fifa13", "FIFA 14", "FIFA 15 Legacy Edition", "FIFA 26 mod").forEach {
            assertNotNull(it, db.recommendationFor("UNKNOWN", it))
        }
        listOf("NotFIFA", "FIFAworld", "Football Manager", "").forEach {
            assertNull(it, db.recommendationFor("UNKNOWN", it))
        }
        assertNull(db.recommendationFor("", "FIFA 15"))
        val changed = database(xml.replace("title-word=\"FIFA\"", "title-word=\"SAMPLE\""))
        assertNull(changed.recommendationFor("UNKNOWN", "FIFA 15"))
        assertNotNull(changed.recommendationFor("UNKNOWN", "SAMPLE 15"))
    }

    @Test fun recommendationsDoNotMutateGlobalSettingsOrSelectDrivers() {
        val global = VitaCoreConfig(memoryMapping = "double-buffer", audioVolume = 37, customDriverName = "working-driver")
        val db = database()
        val effective = db.applyDefaults(global, "PCSE00481")
        assertEquals("double-buffer", global.memoryMapping)
        assertEquals(37, effective.audioVolume)
        assertEquals("working-driver", effective.customDriverName)
        assertSame(global, db.applyDefaults(global, "OTHER", "Killzone"))
        assertSame(global, db.applyDefaults(global, ""))
    }

    @Test fun missingUnknownOrInvalidDatabaseCannotForceSettings() {
        assertNull(database("<game-db version=\"1\"/>").recommendationFor("PCSE00481"))
        assertNull(database(xml.replace("version=\"1\"", "version=\"99\"")).recommendationFor("PCSE00481"))
        assertNull(database(xml.replace("memory-mapping=\"native-buffer\"", "memory-mapping=\"invalid\"")).recommendationFor("PCSE00481"))
        assertNull(GameCompatibilityDatabase.EMPTY.recommendationFor("PCSE00481"))
    }

    @Test fun exactIdsTakePrecedenceOverFamilyNames() {
        val extra = "<profile title-word=\"FIFA\"><title-id>EXACT</title-id><gpu backend-renderer=\"OpenGL\" memory-mapping=\"disabled\" use-angle=\"true\"/></profile>"
        val db = database(xml.replace("</game-db>", "$extra</game-db>"))
        assertEquals("OpenGL", db.recommendationFor("EXACT", "FIFA 15")!!.backendRenderer)
    }

    @Test fun uiAndCoreLoadUserChoicesAfterTheSameDatabaseAndNeverForceThemAfterwards() {
        val repository = app.resolve("src/main/java/com/sbro/emucorev/core/VitaGameSettingsRepository.kt").readText()
            .substringAfter("fun loadProfile(").substringBefore("fun save(")
        assertTrue(repository.indexOf("gameDatabase.applyDefaults") < repository.indexOf("readCustomConfig"))
        assertFalse(repository.substringAfter("readCustomConfig").contains("applyDefaults"))
        val native = app.resolve("src/main/cpp/vita3k/vita3k/config/src/settings.cpp").readText()
            .substringAfter("void set_current_config(Config &cfg").substringBefore("void copy_current_config_to_global")
        assertTrue(native.contains(GameCompatibilityDatabase.ASSET_PATH))
        assertTrue(native.indexOf("game_database::recommendation") < native.indexOf("load_custom_config"))
        val viewModel = app.resolve("src/main/java/com/sbro/emucorev/ui/gamemanager/GameManagerViewModel.kt").readText()
            .substringAfter("fun updateSelected(").substringBefore("fun selectCustomDriverOverride")
        assertTrue(viewModel.contains("val updated = transform(_uiState.value.config)"))
        val ui = app.resolve("src/main/java/com/sbro/emucorev/ui/gamemanager/GameManagerScreen.kt").readText()
        assertTrue(ui.contains("config.backendRenderer, listOf(\"Vulkan\", \"OpenGL\")"))
        assertTrue(ui.contains("config.memoryMapping, listOf(\"disabled\", \"double-buffer\", \"external-host\", \"page-table\", \"native-buffer\")"))
    }
}
