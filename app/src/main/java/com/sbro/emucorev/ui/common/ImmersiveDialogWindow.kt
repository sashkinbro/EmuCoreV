package com.sbro.emucorev.ui.common

import android.view.View
import android.view.ViewTreeObserver
import android.view.Window

/** Owns only this modal's listeners; never replaces its key/capture Window.Callback. */
internal class ImmersiveDialogWindowBinding(
    private val window: Window,
    private val restoreParent: () -> Unit,
    private val applyToDialog: (Window) -> Unit = ImmersiveMode::applyToDialog
) : View.OnAttachStateChangeListener, ViewTreeObserver.OnWindowFocusChangeListener,
    ViewTreeObserver.OnPreDrawListener, AutoCloseable {
    private val decor = window.decorView
    private var observer: ViewTreeObserver? = null
    private var firstDrawPending = true
    private var active = true

    init {
        decor.addOnAttachStateChangeListener(this)
        observeCurrentTree()
        refresh()
    }

    fun refresh() {
        if (active) applyToDialog(window)
    }

    private fun observeCurrentTree() {
        val next = decor.viewTreeObserver
        if (observer === next || !next.isAlive) return
        removeTreeListeners()
        // Attaching can merge the old floating observer into this tree and kill
        // the old object. Remove only our migrated entries before registering once.
        next.removeOnWindowFocusChangeListener(this)
        next.removeOnPreDrawListener(this)
        observer = next
        next.addOnWindowFocusChangeListener(this)
        if (firstDrawPending) next.addOnPreDrawListener(this)
    }

    private fun removeTreeListeners() {
        observer?.takeIf { it.isAlive }?.let {
            it.removeOnWindowFocusChangeListener(this)
            it.removeOnPreDrawListener(this)
        }
        observer = null
    }

    override fun onViewAttachedToWindow(view: View) {
        if (!active) return
        observeCurrentTree()
        refresh()
    }

    override fun onViewDetachedFromWindow(view: View) {
        removeTreeListeners()
        firstDrawPending = true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        if (hasFocus) refresh()
    }

    override fun onPreDraw(): Boolean {
        if (active && firstDrawPending) {
            refresh()
            firstDrawPending = false
            observer?.takeIf { it.isAlive }?.removeOnPreDrawListener(this)
        }
        return true
    }

    override fun close() {
        if (!active) return
        active = false
        decor.removeOnAttachStateChangeListener(this)
        removeTreeListeners()
        restoreParent()
    }
}
