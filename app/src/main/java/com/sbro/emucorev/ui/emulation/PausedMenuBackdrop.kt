package com.sbro.emucorev.ui.emulation

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.PixelCopy
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.widget.FrameLayout
import android.widget.ImageView
import com.sbro.emucorev.BuildConfig
import org.libsdl.app.SDLSurface
import kotlin.math.max
import kotlin.math.roundToInt

internal fun usesPausedMenuBackdrop(isMali: Boolean, backend: String): Boolean =
    isMali && backend.equals("Vulkan", ignoreCase = true)

/** One request per mounted menu. A UI timeout invalidates delivery, not the native copy. */
internal class MenuSnapshotGate(private val deadline: Long) {
    var generation: Int = 1
        private set
    private var settled = false
    private var disposed = false

    fun complete(token: Int, now: Long): Boolean {
        if (disposed || settled || token != generation) return false
        settled = true
        return now < deadline
    }

    fun expire() {
        settled = true
        generation++
    }

    fun dispose() {
        disposed = true
        generation++
    }
}

internal fun menuSnapshotSize(width: Int, height: Int): Pair<Int, Int>? {
    if (width <= 0 || height <= 0) return null
    val scale = minOf(1.0, 1920.0 / max(width, height))
    return max(1, (width * scale).roundToInt()) to max(1, (height * scale).roundToInt())
}

private fun findMenuSurface(view: View): SurfaceView? {
    if (view is SDLSurface) return view
    if (view is ViewGroup) {
        for (index in 0 until view.childCount) {
            findMenuSurface(view.getChildAt(index))?.let { return it }
        }
    }
    return null
}

/** The bitmap covers only the original SDL view bounds; black preserves its letterbox area. */
internal class PausedMenuBackdrop(
    private val window: Window,
    private val container: FrameLayout,
    sourceRoot: View?,
    initiallyHidden: Boolean
) {
    private val source = sourceRoot?.let(::findMenuSurface)
    private val handler = Handler(Looper.getMainLooper())
    private var gate: MenuSnapshotGate? = null
    private val image = ImageView(container.context).apply {
        scaleType = ImageView.ScaleType.FIT_XY
        visibility = View.GONE
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    private var hidden = initiallyHidden
    private var disposed = false
    private var opaque = false
    private var bitmap: Bitmap? = null
    private var sourceWidth = 0
    private var sourceHeight = 0
    private val sourcePosition = IntArray(2)
    private val windowPosition = IntArray(2)
    private val timeout = Runnable {
        gate?.expire()
        record("transparent fallback: capture exceeded 200ms")
    }
    private val layoutListener = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
        render()
    }

    init {
        container.addView(image, 0, FrameLayout.LayoutParams(1, 1))
        container.addOnLayoutChangeListener(layoutListener)
        source?.addOnLayoutChangeListener(layoutListener)
    }

    fun capture() {
        if (disposed || gate != null) return
        val request = MenuSnapshotGate(SystemClock.uptimeMillis() + 200L)
        gate = request
        val view = source
        if (view == null || !view.isAttachedToWindow || !view.holder.surface.isValid) {
            record("transparent fallback: no valid SDL surface")
            return
        }
        val size = menuSnapshotSize(view.width, view.height)
        if (size == null) {
            record("transparent fallback: empty SDL surface")
            return
        }
        sourceWidth = view.width
        sourceHeight = view.height
        view.getLocationOnScreen(sourcePosition)
        val target = try {
            Bitmap.createBitmap(size.first, size.second, Bitmap.Config.ARGB_8888)
        } catch (_: OutOfMemoryError) {
            record("transparent fallback: bitmap allocation failed")
            return
        }
        val token = request.generation
        handler.postDelayed(timeout, 200L)
        try {
            PixelCopy.request(view, target, { result ->
                handler.removeCallbacks(timeout)
                val accepted = request.complete(token, SystemClock.uptimeMillis())
                if (accepted && result == PixelCopy.SUCCESS && geometryMatches()) {
                    // Never recycle a published bitmap: ImageView/RenderNode can retain it.
                    target.setHasAlpha(false)
                    bitmap = target
                    image.setImageBitmap(target)
                    render()
                    record("paused backdrop ready: ${size.first}x${size.second}")
                } else {
                    // Completion owns the destination again, even after close or UI timeout.
                    target.recycle()
                    if (!disposed && accepted) record("transparent fallback: copy result=$result or changed geometry")
                }
            }, handler)
        } catch (_: IllegalArgumentException) {
            handler.removeCallbacks(timeout)
            request.expire()
            target.recycle()
            record("transparent fallback: surface unavailable during request")
        }
    }

    fun setHidden(value: Boolean) {
        if (hidden == value) return
        hidden = value
        render()
    }

    private fun geometryMatches(): Boolean {
        val view = source ?: return false
        if (disposed || !view.isAttachedToWindow || !view.holder.surface.isValid ||
            view.width != sourceWidth || view.height != sourceHeight) return false
        val position = IntArray(2)
        view.getLocationOnScreen(position)
        return position.contentEquals(sourcePosition)
    }

    private fun render() {
        if (disposed) return
        if (bitmap != null && !geometryMatches()) {
            image.setImageDrawable(null)
            bitmap = null // Geometry changes fall back without another copy.
        }
        val show = !hidden && bitmap != null
        if (show) {
            container.getLocationOnScreen(windowPosition)
            val params = image.layoutParams as FrameLayout.LayoutParams
            val left = sourcePosition[0] - windowPosition[0]
            val top = sourcePosition[1] - windowPosition[1]
            if (params.width != sourceWidth || params.height != sourceHeight ||
                params.leftMargin != left || params.topMargin != top) {
                params.width = sourceWidth
                params.height = sourceHeight
                params.leftMargin = left
                params.topMargin = top
                image.layoutParams = params
            }
        }
        image.visibility = if (show) View.VISIBLE else View.GONE
        if (opaque != show) {
            opaque = show
            val color = if (show) Color.BLACK else Color.TRANSPARENT
            container.setBackgroundColor(color)
            window.setBackgroundDrawable(ColorDrawable(color))
            window.setFormat(if (show) PixelFormat.OPAQUE else PixelFormat.TRANSLUCENT)
        }
    }

    fun dispose() {
        disposed = true
        gate?.dispose()
        handler.removeCallbacks(timeout)
        source?.removeOnLayoutChangeListener(layoutListener)
        container.removeOnLayoutChangeListener(layoutListener)
        image.setImageDrawable(null)
        bitmap = null
        // An in-flight PixelCopy retains its target until its callback; never recycle here.
    }

    private fun record(message: String) {
        if (BuildConfig.DEBUG) Log.d("MenuSnapshot", message)
    }
}
