package com.sbro.emucorev.ui.emulation

import java.nio.file.Files
import java.nio.file.Path
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SaveStateRefusalTest {
    private fun reason(
        blank: Boolean = false,
        exiting: Boolean = false,
        launchLoading: Boolean = false,
        waitingFrame: Boolean = false,
        busy: Boolean = false
    ) = saveStateRefusalReason(blank, exiting, launchLoading, waitingFrame, busy)

    @Test
    fun idleAllowsAction() = assertNull(reason())

    @Test
    fun eachBlockerMapsToItsReason() {
        assertEquals(SaveStateRefusal.NoGame, reason(blank = true))
        assertEquals(SaveStateRefusal.Exiting, reason(exiting = true))
        assertEquals(SaveStateRefusal.Loading, reason(launchLoading = true))
        assertEquals(SaveStateRefusal.Loading, reason(waitingFrame = true))
        assertEquals(SaveStateRefusal.Busy, reason(busy = true))
    }

    @Test
    fun priorityIsNoGameThenExitingThenLoadingThenBusy() {
        assertEquals(SaveStateRefusal.NoGame, reason(blank = true, exiting = true, waitingFrame = true, busy = true))
        assertEquals(SaveStateRefusal.Exiting, reason(exiting = true, launchLoading = true, busy = true))
        assertEquals(SaveStateRefusal.Loading, reason(waitingFrame = true, busy = true))
    }

    /** The Save/Load/Delete bar must be a sibling of the scrolling column, never inside it. */
    @Test
    fun actionBarIsOutsideScrollingContainer() {
        val root = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
            .first { Files.isDirectory(it.resolve("src/main")) }
        val src = Files.readString(root.resolve("src/main/java/com/sbro/emucorev/ui/emulation/EmulationMenu.kt"))
            .replace("\r\n", "\n")
        val start = src.indexOf("private fun MenuScrollableContent(")
        val end = src.indexOf("private fun MenuSelectedContent(")
        assertTrue(start in 0 until end)
        val body = src.substring(start, end)
        val scrollAt = body.indexOf(".verticalScroll(")
        val barAt = body.indexOf("SaveStateActionBar(")
        assertTrue("scroll container not found", scrollAt >= 0)
        assertTrue("action bar not rendered by MenuScrollableContent", barAt > scrollAt)
        // The scrolling Column's block must close before the bar is emitted.
        val open = body.lastIndexOf('{', body.indexOf("MenuHeader("))
        var depth = 0
        var close = -1
        for (i in open until body.length) {
            if (body[i] == '{') depth++
            if (body[i] == '}') { depth--; if (depth == 0) { close = i; break } }
        }
        assertTrue("action bar is inside the scrolling column", close in 0 until barAt)
        val tab = src.substring(src.indexOf("private fun SaveStatesTab("), src.indexOf("private fun SaveStateActionBar("))
        assertTrue("SaveStatesTab must not render the actions", !tab.contains("SaveStateActionButton("))
    }
}
