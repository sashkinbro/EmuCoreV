package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SaveStateContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private fun kotlin(path: String) = app.resolve("src/main/java/com/sbro/emucorev/$path").readText()
    private fun bridge(path: String) = app.resolve("src/main/cpp/emucorev/$path").readText()
    private fun core(path: String) = app.resolve("src/main/cpp/vita3k/vita3k/$path").readText()

    @Test
    fun jniBridgeSymbolsMatchKotlinExternals() {
        val native = bridge("src/savestate_bridge.cpp")
        val kotlin = kotlin("core/SaveStateBridge.kt")
        listOf(
            "nativeSaveState",
            "nativeLoadState",
            "nativeInspectSaveState",
            "nativeDeleteSaveState",
            "nativeCaptureThumbnail",
            "nativeGetRunningTitleId"
        ).forEach { name ->
            assertTrue("Missing native JNI export $name", native.contains("Java_com_sbro_emucorev_core_SaveStateBridge_$name"))
            assertTrue("Missing Kotlin external $name", kotlin.contains("external fun $name"))
        }
    }

    @Test
    fun coreEngineExposesSaveLoadAndRebuildHooks() {
        val api = bridge("include/emucorev/savestate/savestate.h")
        assertTrue(api.contains("Result save_state("))
        assertTrue(api.contains("Result load_state("))
        assertTrue(api.contains("bool is_current_session("))
        val archive = bridge("include/emucorev/savestate/archive.h")
        assertTrue(archive.contains("kFormatVersion"))
        assertTrue(archive.contains("SectionId::Memory") || archive.contains("Memory = 2"))
        val threadState = core("kernel/include/kernel/thread/thread_state.h")
        assertTrue(threadState.contains("struct Snapshot"))
        assertTrue(threadState.contains("capture_snapshot()"))
        assertTrue(threadState.contains("apply_private_snapshot"))
        val kernelState = core("kernel/include/kernel/state.h")
        assertTrue(kernelState.contains("create_thread_from_snapshot"))
        assertTrue(kernelState.contains("clear_paused_threads_state"))
        val cpu = core("cpu/include/cpu/common.h")
        assertTrue(cpu.contains("tpidruro"))
    }

    @Test
    fun gxmObjectsAreDestroyedAndRebuiltAroundRamRestore() {
        val gxm = core("gxm/include/gxm/savestate.h")
        listOf(
            "capture_contexts",
            "capture_sync_objects",
            "capture_render_targets",
            "destroy_runtime_objects",
            "restore_contexts",
            "restore_sync_objects",
            "restore_render_targets",
            "restore_memory_regions"
        ).forEach { hook ->
            assertTrue("Missing GXM save-state hook $hook", gxm.contains(hook))
        }
        val engine = bridge("src/savestate/savestate.cpp")
        assertTrue(engine.contains("gxm::destroy_runtime_objects(emuenv)"))
        assertTrue(engine.contains("read_memory(emuenv, reader"))
    }

    @Test
    fun buildIncludesSaveStateSources() {
        val cmake = bridge("cmake/AttachToVita3K.cmake")
        assertTrue(cmake.contains("src/savestate/archive.cpp"))
        assertTrue(cmake.contains("src/savestate/savestate.cpp"))
        assertTrue(cmake.contains("src/savestate_bridge.cpp"))
        val proguard = app.resolve("proguard-rules.pro").readText()
        assertTrue(proguard.contains("com.sbro.emucorev.core.SaveStateBridge"))
    }

    @Test
    fun inGameUiWiresQuickSaveLoadAndSlotTab() {
        val menu = kotlin("ui/emulation/EmulationMenu.kt")
        assertTrue(menu.contains("SaveStates"))
        assertTrue(menu.contains("SaveStateMenuState"))
        assertTrue(menu.contains("onSaveStateSave"))
        assertTrue(menu.contains("onQuickSaveState"))
        val overlay = kotlin("ui/emulation/EmulationOverlay.kt")
        assertTrue(overlay.contains("SaveStateRepository"))
        assertTrue(overlay.contains("performSaveStateAction"))
        assertTrue(overlay.contains("SaveStateAction.Save(SaveStateRepository.QUICK_SLOT"))
        assertTrue(overlay.contains("onQuickLoadState"))
    }

    @Test
    fun standaloneManagerScreenIsSeparateFromSavedata() {
        val navigation = kotlin("navigation/AppNavigation.kt")
        assertTrue(navigation.contains("ROUTE_SAVE_STATES"))
        assertTrue(navigation.contains("SaveStatesScreen("))
        val detail = kotlin("ui/detail/GameDetailScreen.kt")
        assertTrue(detail.contains("onOpenSaveStates"))
        assertTrue(detail.contains("manageSaveStatesLabel"))
        val screen = kotlin("ui/savestates/SaveStatesScreen.kt")
        assertTrue(screen.contains("fun SaveStatesScreen("))
        assertTrue(screen.contains("viewModel.isRunning"))
    }

    @Test
    fun everyLocaleContainsTheSaveStateStrings() {
        val resourceRoot = app.resolve("src/main/res")
        val required = listOf(
            "emulation_tab_savestates",
            "emulation_menu_section_savestates",
            "emulation_savestate_saved_toast",
            "emulation_savestate_session_mismatch_toast",
            "emulation_quickbar_quick_save",
            "emulation_quickbar_quick_load",
            "savestate_manager_title",
            "savestate_manager_open_for_game"
        )
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") && it.fileName.toString() != "values-night"
            }.toList()
        }
        assertEquals(11, localizedDirectories.size)
        val targets = listOf(resourceRoot.resolve("values/strings.xml")) +
            localizedDirectories.map { it.resolve("strings.xml") }
        targets.forEach { file ->
            val keys = stringKeys(file)
            required.forEach { key ->
                assertTrue("Missing $key in ${file.parent.fileName}", key in keys)
            }
        }
    }

    private fun stringKeys(path: Path): Set<String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        return buildSet {
            for (index in 0 until nodes.length) {
                nodes.item(index).attributes?.getNamedItem("name")?.nodeValue?.let(::add)
            }
        }
    }
}
