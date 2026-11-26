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

    @Test fun plantsVsZombiesStartupSchedulingInheritsOtherSettingsAndCanBeOverridden() {
        val base = VitaCoreConfig(memoryMapping = "page-table")
        val recommended = database().applyDefaults(base, "PCSE00064")
        assertEquals(base.copy(accurateThreadScheduling = true, eagerSurfaceSync = true), recommended)
        val disabled = database("<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu accurate-thread-scheduling=\"false\"/></profile></game-db>")
        assertFalse(disabled.applyDefaults(recommended, "EXACT").accurateThreadScheduling)
        val invalid = database("<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu accurate-thread-scheduling=\"yes\"/></profile></game-db>")
        assertNull(invalid.recommendationFor("EXACT"))
    }

    @Test fun eagerSurfaceSyncIsOptionalStrictAndAllowsExplicitFalse() {
        val base = VitaCoreConfig(eagerSurfaceSync = true)
        val off = database("""<game-db version="1"><profile><title-id>EXACT</title-id><gpu eager-surface-sync="false"/></profile></game-db>""")
        assertFalse(off.applyDefaults(base, "EXACT").eagerSurfaceSync)
        assertTrue(off.applyDefaults(base, "OTHER").eagerSurfaceSync)
        val invalid = database("""<game-db version="1"><profile><title-id>EXACT</title-id><gpu eager-surface-sync="yes"/></profile></game-db>""")
        assertNull(invalid.recommendationFor("EXACT"))
    }

    @Test fun allFifaRegionsAndModsInheritGlobalSettings() {
        val ids = "PCSB00051 PCSB00052 PCSG00039 PCSB00082 PCSB00083 PCSB00084 PCSB00085 PCSB00086 PCSE00055 PCSE00059 PCSE00060 PCSG90012 PCSB00170 PCSB00171 PCSB00174 PCSE00093 PCSE00096 PCSG00107 PCSB00339 PCSB00340 PCSE00263 PCSG00201 PCSB00603 PCSB00604 PCSB00605 PCSB00606 PCSB00607 PCSE00481 PCSE00482 PCSE00483 PCSG00404".split(" ")
        assertEquals(31, ids.size)
        val db = database()
        listOf("disabled", "double-buffer", "external-host", "page-table", "native-buffer").forEach { mapping ->
            val global = VitaCoreConfig(backendRenderer = "OpenGL", memoryMapping = mapping,
                useAngle = true, customDriverName = "working-driver")
            (ids + "UNKNOWN").forEach { id ->
                listOf("EA SPORTS FC mod", "FIFA Football", "FIFA 13", "FIFA 14", "FIFA 15", "FIFA 26 mod").forEach { title ->
                    assertSame("$id / $title / $mapping", global, db.applyDefaults(global, id, title))
                }
            }
        }
        assertNull(db.recommendationFor(" pcse00483 "))
    }

    @Test fun removedHighAccuracyAttributeIsIgnored() {
        val template = "<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu %s/></profile></game-db>"
        listOf("high-accuracy=\"true\"", "high-accuracy=\"false\"", "high-accuracy=\"invalid\"").forEach {
            assertNull(it, database(template.format(it)).recommendationFor("EXACT"))
        }
        val global = VitaCoreConfig(backendRenderer = "OpenGL", memoryMapping = "disabled", useAngle = true)
        val fullValid = "backend-renderer=\"Vulkan\" memory-mapping=\"native-buffer\" use-angle=\"false\" high-accuracy=\"invalid\""
        assertEquals(global.copy(backendRenderer = "Vulkan", memoryMapping = "native-buffer", useAngle = false),
            database(template.format(fullValid)).applyDefaults(global, "EXACT"))
        listOf("", "unknown=\"true\"", "backend-renderer=\"Vulkan\"").forEach {
            assertNull(it, database(template.format(it)).recommendationFor("EXACT"))
        }
        listOf("PCSE00383", "PCSE00001", "PCSB00356").forEach { assertNull(it, database().recommendationFor(it)) }
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

    @Test fun nativeDepthPolicyKeepsEditableGpuSettingsAndRejectsMalformedValues() {
        val profile = """<game-db version="1"><profile><title-id>PCSE00280</title-id><gpu depth-clamp="false"/></profile></game-db>"""
        val base = VitaCoreConfig()
        assertNotNull(database(profile).recommendationFor("PCSE00280"))
        assertEquals(base, database(profile).applyDefaults(base, "PCSE00280"))
        assertNull(database(profile.replace("depth-clamp=\"false\"", "depth-clamp=\"invalid\"")).recommendationFor("PCSE00280"))
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
        val native = app.resolve("src/main/cpp/emucorev/ECV/ECV/config/src/settings.cpp").readText()
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
