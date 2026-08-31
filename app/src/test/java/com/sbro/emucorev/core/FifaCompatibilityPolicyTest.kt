package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.*
import org.junit.Test

class FifaCompatibilityPolicyTest {
    @Test
    fun everyKnownRegionalIdAndRenamedModUsesNativeBuffer() {
        val custom = VitaCoreConfig(memoryMapping = "page-table", backendRenderer = "OpenGL", useAngle = true)
        assertEquals(31, FifaCompatibilityPolicy.titleIds.size)
        FifaCompatibilityPolicy.titleIds.forEach { id ->
            val effective = FifaCompatibilityPolicy.apply(custom, id, "EA SPORTS FC 27 mod")
            assertEquals(id, "native-buffer", effective.memoryMapping)
            assertEquals(id, "Vulkan", effective.backendRenderer)
            assertFalse(id, effective.useAngle)
        }
        assertTrue(FifaCompatibilityPolicy.appliesTo(" pcse00483 "))
    }

    @Test
    fun namesCoverOtherRegionsDemosAndAllSeriesWithoutMatchingUnrelatedWords() {
        listOf("EA SPORTS™ FIFA Football", "FIFA Soccer Demo", "fifa13", "FIFA 14", "FIFA 15 Legacy Edition", "FIFA 26 mod", "FIFA 13 WORLD CLASS SOCCER").forEach {
            assertTrue(it, FifaCompatibilityPolicy.appliesTo("UNKNOWN", it))
        }
        listOf("Football Manager", "FIFAworld", "NotFIFA", "").forEach {
            assertFalse(it, FifaCompatibilityPolicy.appliesTo("PCSB99999", it))
        }
    }

    @Test
    fun overrideDoesNotMutateGlobalConfigOrUnrelatedOptions() {
        val global = VitaCoreConfig(memoryMapping = "double-buffer", audioVolume = 37, resolutionMultiplier = 2f, customDriverName = "my-driver")
        val effective = FifaCompatibilityPolicy.apply(global, "PCSE00481")
        assertEquals("double-buffer", global.memoryMapping)
        assertEquals(37, effective.audioVolume)
        assertEquals(2f, effective.resolutionMultiplier)
        assertEquals("my-driver", effective.customDriverName)
        assertSame(global, FifaCompatibilityPolicy.apply(global, "PCSA00107", "Killzone"))
        assertSame(global, FifaCompatibilityPolicy.apply(global, ""))
    }

    @Test
    fun nativeAndAndroidTitleIdsStayIdenticalAndOverrideFollowsCustomConfig() {
        val app = appModule()
        val header = app.resolve("src/main/cpp/vita3k/vita3k/config/include/config/game_compatibility.h").readText()
        val ids = Regex("PC[A-Z]{2}[0-9]{5}").findAll(header).map { it.value }.toSet()
        assertEquals(FifaCompatibilityPolicy.titleIds, ids)
        val settings = app.resolve("src/main/cpp/vita3k/vita3k/config/src/settings.cpp").readText()
            .substringAfter("void set_current_config(Config &cfg")
        assertTrue(settings.indexOf("load_custom_config") < settings.indexOf("game_compatibility::is_fifa"))
        val launch = app.resolve("src/main/cpp/vita3k/vita3k/app/src/app_init.cpp").readText()
        assertTrue("game_compatibility::is_fifa(app_path, title)" in launch)
    }

    private fun appModule(): Path {
        val cwd = Path.of(System.getProperty("user.dir"))
        return sequenceOf(cwd, cwd.resolve("app")).first { Files.isDirectory(it.resolve("src/main")) }
    }
}
