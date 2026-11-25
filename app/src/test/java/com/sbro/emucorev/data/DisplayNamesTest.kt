package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DisplayNamesTest {
    private val grin = "😀" // one code point, two UTF-16 units

    @Test fun plainTextIsCutAtTheLimit() {
        assertEquals("abc", "abcdef".truncateGraphemes(maxGraphemes = 3))
        assertEquals("abc", "abc".truncateGraphemes())
    }

    @Test fun surrogatePairsAreNeverSplit() {
        val name = "a".repeat(31) + grin + grin
        val cut = name.truncateGraphemes()
        assertEquals(32, cut.graphemeCount())
        assertTrue(cut.endsWith(grin))
        assertFalse(Character.isHighSurrogate(cut.last()))
    }

    @Test fun aNameOfOnlyEmojiKeepsWholeEmoji() {
        val cut = grin.repeat(40).truncateGraphemes()
        assertEquals(32, cut.graphemeCount())
        assertEquals(grin.repeat(32), cut)
    }

    @Test fun combiningMarksStayWithTheirBase() {
        val accented = "é" // e + combining acute = one user-perceived character
        assertEquals(1, accented.graphemeCount())
        assertEquals(accented + accented, accented.repeat(5).truncateGraphemes(maxGraphemes = 2))
    }

    @Test fun utf16BoundStopsBeforeAWholeCluster() {
        val cut = grin.repeat(40).truncateGraphemes(maxGraphemes = 32, maxUtf16 = 5)
        assertEquals(grin + grin, cut)
    }

    @Test fun emptyStaysEmpty() {
        assertEquals("", "".truncateGraphemes())
        assertEquals(0, "".graphemeCount())
    }
}
