package com.sbro.emucorev.core.sdl

import android.os.Build
import android.text.Editable
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.widget.EditText

class SDLInputConnection(targetView: View, fullEditor: Boolean) : BaseInputConnection(targetView, fullEditor) {
    private val editText = EditText(SDL.getContext())
    private var committedText = ""

    override fun getEditable(): Editable {
        return editText.editableText
    }

    override fun sendKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_ENTER && SDLActivity.onNativeSoftReturnKey()) {
            return true
        }
        return super.sendKeyEvent(event)
    }

    override fun commitText(text: CharSequence, newCursorPosition: Int): Boolean {
        if (!super.commitText(text, newCursorPosition)) {
            return false
        }
        updateText()
        return true
    }

    override fun setComposingText(text: CharSequence, newCursorPosition: Int): Boolean {
        if (!super.setComposingText(text, newCursorPosition)) {
            return false
        }
        updateText()
        return true
    }

    override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.Q && beforeLength > 0 && afterLength == 0) {
            repeat(beforeLength) {
                nativeGenerateScancodeForUnichar('\b')
            }
            return true
        }

        if (!super.deleteSurroundingText(beforeLength, afterLength)) {
            return false
        }
        updateText()
        return true
    }

    private fun updateText() {
        val content = editable
        val text = content.toString()
        val compareLength = minOf(text.length, committedText.length)
        var matchLength = 0

        while (matchLength < compareLength) {
            val codePoint = committedText.codePointAt(matchLength)
            if (codePoint != text.codePointAt(matchLength)) {
                break
            }
            matchLength += Character.charCount(codePoint)
        }

        var offset = matchLength
        while (offset < committedText.length) {
            val codePoint = committedText.codePointAt(offset)
            nativeGenerateScancodeForUnichar('\b')
            offset += Character.charCount(codePoint)
        }

        if (matchLength < text.length) {
            val pendingText = text.substring(matchLength)
            if (!SDLActivity.dispatchingKeyEvent()) {
                var pendingOffset = 0
                while (pendingOffset < pendingText.length) {
                    val codePoint = pendingText.codePointAt(pendingOffset)
                    if (codePoint == '\n'.code && SDLActivity.onNativeSoftReturnKey()) {
                        return
                    }
                    if (codePoint in 1..127) {
                        nativeGenerateScancodeForUnichar(codePoint.toChar())
                    }
                    pendingOffset += Character.charCount(codePoint)
                }
            }
            nativeCommitText(pendingText, 0)
        }
        committedText = text
    }

    companion object {
        @JvmStatic
        external fun nativeCommitText(text: String, newCursorPosition: Int)

        @JvmStatic
        external fun nativeGenerateScancodeForUnichar(c: Char)
    }
}
