package com.sbro.emucorev.ui.cheats

import com.sbro.emucorev.core.VitaCheatEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class CheatEntryRowsTest {
    @Test
    fun equalEntriesKeepDistinctKeysAndOriginalIndicesAfterFiltering() {
        val repeated = VitaCheatEntry(
            name = "Infinite Yen",
            enabled = false,
            enabledOnBoot = false,
            broken = false,
            codes = "\$0200 814B301C 0098967F"
        )
        val rows = indexCheatEntries(
            listOf(repeated.copy(name = "Other cheat"), repeated, repeated)
        ) { it.name == repeated.name }

        assertEquals(listOf(1, 2), rows.map { it.sourceIndex })
        assertNotEquals(rows[0].key, rows[1].key)
    }
}
