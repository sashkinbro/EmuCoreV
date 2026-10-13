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

    @Test fun allFifaRegionsAndModsInheritGlobalSettings() {
        val ids = "PCSB00051 PCSB00052 PCSG00039 PCSB00082 PCSB00083 PCSB00084 PCSB00085 PCSB00086 PCSE00055 PCSE00059 PCSE00060 PCSG90012 PCSB00170 PCSB00171 PCSB00174 PCSE00093 PCSE00096 PCSG00107 PCSB00339 PCSB00340 PCSE00263 PCSG00201 PCSB00603 PCSB00604 PCSB00605 PCSB00606 PCSB00607 PCSE00481 PCSE00482 PCSE00483 PCSG00404".split(" ")
        assertEquals(31, ids.size)
        val db = database()
        listOf("disabled", "double-buffer", "external-host", "page-table", "native-buffer").forEach { mapping ->
            val global = VitaCoreConfig(backendRenderer = "OpenGL", memoryMapping = mapping,
                useAngle = true, customDriverName = "working-driver", highAccuracy = false)
            (ids + "UNKNOWN").forEach { id ->
                listOf("EA SPORTS FC mod", "FIFA Football", "FIFA 13", "FIFA 14", "FIFA 15", "FIFA 26 mod").forEach { title ->
                    assertSame("$id / $title / $mapping", global, db.applyDefaults(global, id, title))
                }
            }
        }
        assertNull(db.recommendationFor(" pcse00483 "))
    }

    @Test fun highAccuracyOnlyRecommendationInheritsOtherGlobalSettings() {
        val db = database("<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu high-accuracy=\"true\"/></profile></game-db>")
        val global = VitaCoreConfig(backendRenderer = "OpenGL", memoryMapping = "page-table",
            useAngle = true, customDriverName = "working-driver", audioVolume = 37, highAccuracy = false)
        assertEquals(global.copy(highAccuracy = true), db.applyDefaults(global, "EXACT"))
        assertFalse(global.highAccuracy)
    }

    @Test fun accuracyRecommendationsAcceptExplicitFalseAndRejectMalformedOrEmptyGpu() {
        val global = VitaCoreConfig(highAccuracy = true)
        val template = "<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu %s/></profile></game-db>"
        assertFalse(database(template.format("high-accuracy=\"false\"")).applyDefaults(global, "EXACT").highAccuracy)
        val fullValid = "backend-renderer=\"Vulkan\" memory-mapping=\"native-buffer\" use-angle=\"false\" high-accuracy=\"true\""
        assertEquals(global.copy(backendRenderer = "Vulkan", memoryMapping = "native-buffer", useAngle = false),
            database(template.format(fullValid)).applyDefaults(global, "EXACT"))
        listOf("", "unknown=\"true\"", "high-accuracy=\"TRUE\"", "high-accuracy=\"\"",
            "high-accuracy=\"enabled\"", "backend-renderer=\"Vulkan\" high-accuracy=\"true\"").forEach {
            assertNull(it, database(template.format(it)).recommendationFor("EXACT"))
        }
        val full = "backend-renderer=\"Vulkan\" memory-mapping=\"native-buffer\" use-angle=\"false\" high-accuracy=\"invalid\""
        assertNull(database(template.format(full)).recommendationFor("EXACT"))
    }

    @Test fun verifiedBorderlandsProfileChangesOnlyAccuracyAndDoesNotGuessOtherRegions() {
        val db = database()
        val global = VitaCoreConfig(backendRenderer = "OpenGL", memoryMapping = "disabled", useAngle = true)
        assertEquals(global.copy(highAccuracy = true), db.applyDefaults(global, " pcse00383 ", "Renamed game"))
        assertSame(global, db.applyDefaults(global, "UNVERIFIED", "Borderlands 2"))
        assertSame(global, db.applyDefaults(global, "", "Borderlands 2"))
    }

    @Test fun verifiedRidgeRacerStartsWithAccurateGpuWithoutChangingOtherGamesOrSettings() {
        val db = database()
        val global = VitaCoreConfig(highAccuracy = false, memoryMapping = "double-buffer",
            customDriverName = "working-driver", resolutionMultiplier = 2f, audioVolume = 37)
        assertEquals(global.copy(highAccuracy = true), db.applyDefaults(global, " pcse00001 ", "Renamed game"))
        assertFalse(global.highAccuracy)
        assertSame(global, db.applyDefaults(global, "UNVERIFIED", "RIDGE RACER"))
        assertSame(global, db.applyDefaults(global, "", "RIDGE RACER"))
    }

    @Test fun familyNamesAreDataDrivenAndDoNotMatchUnrelatedWords() {
        val familyXml = "<game-db version=\"1\"><profile title-word=\"FIFA\"><gpu backend-renderer=\"Vulkan\" memory-mapping=\"double-buffer\" use-angle=\"false\"/></profile></game-db>"
        val db = database(familyXml)
        listOf("FIFA Football", "EA SPORTS™ FIFA Soccer Demo", "fifa13", "FIFA 14", "FIFA 15 Legacy Edition", "FIFA 26 mod").forEach {
            assertNotNull(it, db.recommendationFor("UNKNOWN", it))
        }
        listOf("NotFIFA", "FIFAworld", "Football Manager", "").forEach {
            assertNull(it, db.recommendationFor("UNKNOWN", it))
        }
        assertNull(db.recommendationFor("", "FIFA 15"))
        val changed = database(familyXml.replace("title-word=\"FIFA\"", "title-word=\"SAMPLE\""))
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
        val malformed = "<game-db version=\"1\"><profile><title-id>PCSE00481</title-id><gpu backend-renderer=\"Vulkan\" memory-mapping=\"invalid\" use-angle=\"false\"/></profile></game-db>"
        assertNull(database(malformed).recommendationFor("PCSE00481"))
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
