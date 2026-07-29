package com.sbro.emucorev.ui.library

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryGridSizingTest {
    @Test
    fun defaultPhonePreviewMatchesRealThreeColumnLibrary() {
        assertEquals(3, LibraryGridSizing.columnsForWidth(412f, 100))
    }

    @Test
    fun largerCoversAlwaysReduceOrKeepColumnCount() {
        val small = LibraryGridSizing.columnsForWidth(412f, 70)
        val default = LibraryGridSizing.columnsForWidth(412f, 100)
        val large = LibraryGridSizing.columnsForWidth(412f, 150)

        assertTrue(small >= default)
        assertTrue(default >= large)
        assertTrue(large >= 1)
    }

    @Test
    fun invalidPercentagesAreClamped() {
        assertEquals(
            LibraryGridSizing.columnsForWidth(600f, 70),
            LibraryGridSizing.columnsForWidth(600f, -100)
        )
        assertEquals(
            LibraryGridSizing.columnsForWidth(600f, 150),
            LibraryGridSizing.columnsForWidth(600f, 900)
        )
    }
}
