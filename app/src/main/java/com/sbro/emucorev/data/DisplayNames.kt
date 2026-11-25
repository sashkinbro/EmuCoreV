package com.sbro.emucorev.data

import java.text.BreakIterator

/** Maximum visible characters (grapheme clusters) in a player display name. */
const val MAX_DISPLAY_NAME_GRAPHEMES = 32

/** Hard cap on UTF-16 code units so a name made of long ZWJ sequences stays bounded in Firestore. */
const val MAX_DISPLAY_NAME_UTF16 = 64

/**
 * Truncates to at most [maxGraphemes] user-perceived characters (and [maxUtf16] UTF-16 units)
 * without ever cutting a surrogate pair or an emoji/combining sequence in half (B073).
 */
fun String.truncateGraphemes(
    maxGraphemes: Int = MAX_DISPLAY_NAME_GRAPHEMES,
    maxUtf16: Int = MAX_DISPLAY_NAME_UTF16
): String {
    if (isEmpty()) return this
    val iterator = BreakIterator.getCharacterInstance()
    iterator.setText(this)
    var end = 0
    var count = 0
    var boundary = iterator.next()
    while (boundary != BreakIterator.DONE) {
        if (count + 1 > maxGraphemes || boundary > maxUtf16) break
        end = boundary
        count++
        boundary = iterator.next()
    }
    return substring(0, end)
}

/** Number of user-perceived characters, matching [truncateGraphemes]. */
fun String.graphemeCount(): Int {
    if (isEmpty()) return 0
    val iterator = BreakIterator.getCharacterInstance()
    iterator.setText(this)
    var count = 0
    while (iterator.next() != BreakIterator.DONE) count++
    return count
}
