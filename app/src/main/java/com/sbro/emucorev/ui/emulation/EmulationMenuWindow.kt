package com.sbro.emucorev.ui.emulation

import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
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
import com.sbro.emucorev.core.DeviceGpuInfoProvider
import com.sbro.emucorev.core.GpuVendor
import com.sbro.emucorev.ui.theme.WindowAwareRipple

/**
 * The game controls use a software Activity window to avoid game-driver HWUI fence ANRs.
 * A scrolling menu needs its own accelerated window rather than rasterizing the whole game
 * display on the input thread. Keep key focus in the Activity for its Back/gamepad handlers.
 */
@Composable
internal fun EmulationMenuWindow(
    rendererBackend: String = "",
    snapshotSourceRoot: View? = null,
    backdropHidden: Boolean = false,
    content: @Composable () -> Unit
) {
    val anchor = LocalView.current
    val compositionContext = rememberCompositionContext()
    val currentContent by rememberUpdatedState(content)
    val currentBackdropHidden by rememberUpdatedState(backdropHidden)
    val backdrop = remember(anchor, compositionContext) { arrayOfNulls<PausedMenuBackdrop>(1) }
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
            setContent { WindowAwareRipple { currentContent() } }
        }
        val useSnapshot = rendererBackend.equals("Vulkan", ignoreCase = true) && usesPausedMenuBackdrop(
            DeviceGpuInfoProvider.get(anchor.context).model?.vendor == GpuVendor.MALI,
            rendererBackend
        )
        val dialogContent = if (useSnapshot) {
            FrameLayout(anchor.context).apply {
                addView(host, FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
                backdrop[0] = PausedMenuBackdrop(window, this, snapshotSourceRoot, currentBackdropHidden)
            }
        } else host
        dialog.setContentView(dialogContent, ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        dialog.setCancelable(false)
        dialog.show()
        window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        WindowCompat.getInsetsController(window, host).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        backdrop[0]?.capture()
        onDispose {
            backdrop[0]?.dispose()
            backdrop[0] = null
            try {
                // Cancel any platform ripple animators while their host RenderNodes are still
                // attached. A released press can still be fading when this window is closed.
                host.disposeComposition()
            } finally {
                dialog.dismiss()
            }
        }
    }
    SideEffect { backdrop[0]?.setHidden(backdropHidden) }
}
