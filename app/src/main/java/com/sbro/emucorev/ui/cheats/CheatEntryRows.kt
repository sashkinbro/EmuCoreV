package com.sbro.emucorev.ui.cheats

import com.sbro.emucorev.core.VitaCheatEntry

internal data class IndexedCheatEntry(
    val sourceIndex: Int,
    val entry: VitaCheatEntry
) {
    val key: String get() = "cheat-$sourceIndex"
}

internal fun indexCheatEntries(
    entries: List<VitaCheatEntry>,
    include: (VitaCheatEntry) -> Boolean = { true }
): List<IndexedCheatEntry> = entries.mapIndexedNotNull { index, entry ->
    if (include(entry)) IndexedCheatEntry(index, entry) else null
}
