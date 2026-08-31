package com.sbro.emucorev.core.vita

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.readText
import org.junit.Assert.*
import org.junit.Test

class NativeImeStateTest {
    private fun state(text: String = "", caret: Int = 0, sce: Boolean = true, dialog: Boolean = false) =
        NativeImeState(sce, dialog, text, 0, 0, caret, false, "")

    @Test fun bothVitaInputServicesHaveAVisibleState() {
        assertTrue(state(sce = true).active)
        assertTrue(state(sce = false, dialog = true).active)
        assertFalse(state(sce = false, dialog = false).active)
    }

    @Test fun previewClampsBadCaretsAndPreservesSurrogatePairs() {
        assertEquals("│", state().preview)
        assertEquals("│abc", state("abc", -10).preview)
        assertEquals("abc│", state("abc", 99).preview)
        assertEquals("A│😀B", state("A😀B", 2).preview)
        assertEquals("A😀│B", state("A😀B", 3).preview)
    }

    @Test fun offlineKeyboardCoversLettersDigitsAndCommonNameCharacters() {
        assertEquals(('a'..'z').toSet(), NativeImeKeyboard.latinRows.joinToString("").toSet())
        val symbols = NativeImeKeyboard.symbolRows.joinToString("")
        assertTrue(('0'..'9').all { it in symbols })
        assertTrue("@._-".all { it in symbols })
    }

    @Test fun visibilityRetriesAreBoundedAndCancellationInvalidatesThem() {
        val activity = appModule().resolve("src/main/java/com/sbro/emucorev/core/vita/Emulator.kt").readText()
        assertTrue("Fallback must not depend on a game-drawn editor", "if (!visible) showBuiltInKeyboard()" in activity)
        assertTrue("Retries need real window focus", "!hasWindowFocus()" in activity)
        assertTrue("A stopped SDL editor has zero size and needs relayout", "editor.layoutParams.width <= 0" in activity)
        assertTrue("Closing or replacing IME must invalidate delayed shows", "generation != keyboardRequestGeneration" in activity)
        val pause = activity.substringAfter("override fun onPause() {").substringBefore("override fun onDestroy()")
        assertTrue("Backgrounding must cancel delayed UI work before pausing SDL",
            "keyboardRequestGeneration++" in pause &&
                pause.indexOf("keyboardRequestGeneration++") < pause.indexOf("super.onPause()"))
        val submit = appModule().resolve("src/main/cpp/emucorev/src/runtime_bridge.cpp").readText()
            .substringAfter("Emulator_submitNativeIme").substringBefore("extern \"C\"")
        assertTrue("Enter must follow queued Android text", "SDL_PushEvent" in submit)
        assertFalse("UI must not submit stale text directly", "submit_current_ime" in submit)
        val inputConnection = appModule().resolve("src/main/java/org/libsdl/app/SDLInputConnection.kt").readText()
        assertTrue("Pasted newlines must not synthesize Enter ahead of the text", "codePoint != '\\n'.code" in inputConnection)
    }

    @Test fun fallbackStaysInTheGameWindowAndDoesNotPauseItsCallbacks() {
        val source = appModule().resolve("src/main/java/com/sbro/emucorev/ui/emulation/NativeImeOverlay.kt").readText()
        assertFalse("An Android Dialog would steal SDL window focus", "AlertDialog(" in source)
        assertTrue("Both IME types must use the fallback", "state.active" in source)
        assertTrue("Display native text, not a competing editable buffer", "state.preview" in source)
        assertTrue("Expose Enter", "activity.completeNativeIme()" in source)
        assertTrue("Respect the native cancelability check", "completeNativeIme(cancel = true)" in source)
    }

    @Test fun everyLocaleContainsTheKeyboardControls() {
        val root = appModule().resolve("src/main/res")
        val required = setOf("done", "system", "builtin", "left", "right", "delete", "shift", "space", "newline")
            .map { "emulation_ime_$it" }.toSet()
        Files.list(root).use { paths ->
            paths.filter { it.fileName.toString().startsWith("values") && Files.exists(it.resolve("strings.xml")) }
                .forEach { directory ->
                    val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(directory.resolve("strings.xml").toFile())
                    val nodes = document.getElementsByTagName("string")
                    val keys = (0 until nodes.length).map { nodes.item(it).attributes.getNamedItem("name").nodeValue }.toSet()
                    assertTrue("Missing IME labels in ${directory.fileName}", keys.containsAll(required))
                }
        }
    }

    private fun appModule(): Path {
        val cwd = Path.of(System.getProperty("user.dir"))
        return sequenceOf(cwd, cwd.resolve("app")).first { Files.isDirectory(it.resolve("src/main")) }
    }
}
