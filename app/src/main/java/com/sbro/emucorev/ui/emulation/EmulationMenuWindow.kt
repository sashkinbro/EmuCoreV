package com.sbro.emucorev.ui.emulation

import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCompositionContext
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.findViewTreeLifecycleOwner
import androidx.lifecycle.findViewTreeViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.findViewTreeSavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner

/**
 * The game controls use a software Activity window to avoid game-driver HWUI fence ANRs.
 * A scrolling menu needs its own accelerated window rather than rasterizing the whole game
 * display on the input thread. Keep key focus in the Activity for its Back/gamepad handlers.
 */
@Composable
internal fun EmulationMenuWindow(content: @Composable () -> Unit) {
    val anchor = LocalView.current
    val compositionContext = rememberCompositionContext()
    val currentContent by rememberUpdatedState(content)
    DisposableEffect(anchor, compositionContext) {
        val dialog = Dialog(anchor.context)
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
        val window = requireNotNull(dialog.window)
        window.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        window.addFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED or
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val host = ComposeView(anchor.context).apply {
            setParentCompositionContext(compositionContext)
            setViewTreeLifecycleOwner(anchor.findViewTreeLifecycleOwner())
            setViewTreeViewModelStoreOwner(anchor.findViewTreeViewModelStoreOwner())
            setViewTreeSavedStateRegistryOwner(anchor.findViewTreeSavedStateRegistryOwner())
            setContent { currentContent() }
        }
        dialog.setContentView(host, ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        dialog.setCancelable(false)
        dialog.show()
        window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        WindowCompat.getInsetsController(window, host).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        onDispose {
            dialog.dismiss()
            host.disposeComposition()
        }
    }
}
