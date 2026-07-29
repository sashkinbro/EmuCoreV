package com.sbro.emucorev.ui.settings

internal fun centeredTabScrollDelta(
    itemOffset: Int,
    itemSize: Int,
    viewportStart: Int,
    viewportEnd: Int
): Float {
    val itemCenter = itemOffset + (itemSize / 2f)
    val viewportCenter = viewportStart + ((viewportEnd - viewportStart) / 2f)
    return itemCenter - viewportCenter
}
