package com.sbro.emucorev

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.graphics.Rect
import android.os.Build
import android.view.ViewTreeObserver
import android.view.WindowManager
import androidx.compose.material3.Text
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.junit4.createComposeRule
import com.sbro.emucorev.core.vita.NativeLibraryExitDialog
import com.sbro.emucorev.ui.common.ImmersiveMode
import java.util.concurrent.CopyOnWriteArrayList
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/** Regression: asymmetric cutouts must not move the modal after its first frame. */
class NativeLibraryExitDialogDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun exitPanelHasItsFinalPositionFromTheFirstDraw() {
        lateinit var activity: Activity
        compose.setContent {
            activity = LocalContext.current.activity()
            Text("Vita exit dialog window test")
        }
        compose.runOnIdle {
            activity.window.attributes = activity.window.attributes.apply {
                layoutInDisplayCutoutMode = if (Build.VERSION.SDK_INT >= 30) {
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                } else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
            ImmersiveMode.apply(activity.window)
        }
        repeat(3) {
            val frames = CopyOnWriteArrayList<Rect>()
            lateinit var dialog: NativeLibraryExitDialog
            compose.runOnIdle {
                dialog = NativeLibraryExitDialog(activity) { error("Exit must not be invoked") }
                val decor = requireNotNull(dialog.window).decorView
                decor.viewTreeObserver.addOnPreDrawListener(object : ViewTreeObserver.OnPreDrawListener {
                    override fun onPreDraw(): Boolean {
                        val xy = IntArray(2)
                        decor.getLocationOnScreen(xy)
                        frames += Rect(xy[0], xy[1], xy[0] + decor.width, xy[1] + decor.height)
                        if (frames.size < 20) decor.postInvalidateOnAnimation()
                        else decor.viewTreeObserver.removeOnPreDrawListener(this)
                        return true
                    }
                })
                dialog.show()
            }
            compose.waitUntil(5_000) { frames.size >= 20 }
            compose.runOnIdle {
                try {
                    val first = frames.first()
                    assertTrue("Panel must be measured before drawing: $first", first.width() > 0 && first.height() > 0)
                    frames.forEachIndexed { frame, bounds ->
                        assertEquals("Panel moved on frame $frame", first, bounds)
                    }
                    assertEquals(0, requireNotNull(dialog.window).attributes.windowAnimations)
                } finally {
                    dialog.dismiss()
                }
            }
        }
    }

    private tailrec fun Context.activity(): Activity = when (this) {
        is Activity -> this
        is ContextWrapper -> baseContext.activity()
        else -> error("Activity context required")
    }
}
