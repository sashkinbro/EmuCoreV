package com.sbro.emucorev.core.sdl

import android.content.Context
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

class SDLDummyEdit(context: Context) : View(context), View.OnKeyListener {
    private var inputConnection: InputConnection? = null
    private var inputTypeValue: Int = 0

    init {
        isFocusableInTouchMode = true
        isFocusable = true
        setOnKeyListener(this)
    }

    fun setInputType(inputType: Int) {
        inputTypeValue = inputType
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onKey(v: View, keyCode: Int, event: KeyEvent): Boolean {
        return SDLActivity.handleKeyEvent(v, keyCode, event, inputConnection)
    }

    override fun onKeyPreIme(keyCode: Int, event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
            val textEdit = SDLActivity.mTextEdit
            if (textEdit != null && textEdit.visibility == VISIBLE) {
                SDLActivity.onNativeKeyboardFocusLost()
            }
        }
        return super.onKeyPreIme(keyCode, event)
    }

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        val connection = SDLInputConnection(this, true)
        inputConnection = connection
        outAttrs.inputType = inputTypeValue
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        return connection
    }
}
