package com.sbro.emucorev.nativeui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** B048: the shell session must survive the death of the :native_shell process. */
class ShellSessionStoreTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private var boot = 7L
    private var clock = 1_000_000L

    private val file get() = File(temporaryFolder.root, ShellSessionStore.FILE_NAME)
    private fun store() = ShellSessionStore(file, { boot }, { clock })

    private val session = "v1\nunlocked 1\nfocus 0 2\nactive 0\ntab PCSE00089\n"

    @Test
    fun roundTripSurvivesProcessDeath() {
        store().save(session)
        // A new store instance is what a recreated process sees.
        assertEquals(session, store().load())
    }

    @Test
    fun nothingStoredMeansLockScreen() {
        assertNull(store().load())
    }

    @Test
    fun otherBootIsNotRestored() {
        store().save(session)
        boot = 8L
        assertNull(store().load())
    }

    @Test
    fun staleSessionIsNotRestored() {
        store().save(session)
        clock += ShellSessionStore.MAX_AGE_MS + 1
        assertNull(store().load())
    }

    @Test
    fun clockMovedBackwardsIsNotRestored() {
        store().save(session)
        clock -= 5_000
        assertNull(store().load())
    }

    @Test
    fun clearForgetsTheSession() {
        val writer = store()
        writer.save(session)
        writer.clear()
        assertNull(store().load())
        assertFalse(file.exists())
    }

    @Test
    fun blankSessionRemovesTheFile() {
        val writer = store()
        writer.save(session)
        writer.save("")
        assertNull(store().load())
    }

    @Test
    fun newerSessionReplacesTheOlderOne() {
        val writer = store()
        writer.save(session)
        val newer = session.replace("focus 0 2", "focus 1 4")
        writer.save(newer)
        assertEquals(newer, store().load())
    }

    @Test
    fun corruptFileIsIgnored() {
        file.writeText("garbage")
        assertNull(store().load())
        file.writeText("x y\nv1\n")
        assertNull(store().load())
    }

    @Test
    fun savingTheSameSessionDoesNotRewrite() {
        val writer = store()
        writer.save(session)
        val first = file.readText()
        clock += 5_000
        writer.save(session)
        assertEquals(first, file.readText())
    }

    @Test
    fun openTitlesListsTheTabs() {
        assertEquals(listOf("A", "B"), ShellSessionStore.openTitles("v1\ntab A\nactive 0\ntab B\n"))
        assertTrue(ShellSessionStore.openTitles("v1\nunlocked 1\n").isEmpty())
    }
}
