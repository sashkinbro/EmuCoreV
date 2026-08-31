package com.sbro.emucorev.core.vita

/** Snapshot of the Vita text service, not a second editable copy of game text. */
data class NativeImeState(
    val sceImeActive: Boolean,
    val dialogActive: Boolean,
    val text: String,
    val preeditStart: Int,
    val preeditLength: Int,
    val caretIndex: Int,
    val multiline: Boolean,
    val enterLabel: String
) {
    val active: Boolean get() = sceImeActive || dialogActive
    val preview: String get() {
        var caret = caretIndex.coerceIn(0, text.length)
        if (caret in 1 until text.length && text[caret].isLowSurrogate() && text[caret - 1].isHighSurrogate()) caret--
        return text.substring(0, caret) + "│" + text.substring(caret)
    }
}

/** Small offline fallback; the Android IME remains available for other scripts. */
object NativeImeKeyboard {
    val latinRows = listOf("qwertyuiop", "asdfghjkl", "zxcvbnm")
    val symbolRows = listOf("1234567890", "@#%&*()-_", ".,!?/:;'+=")
}
