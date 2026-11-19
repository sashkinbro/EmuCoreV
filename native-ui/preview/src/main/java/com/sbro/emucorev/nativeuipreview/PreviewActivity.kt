package com.sbro.emucorev.nativeuipreview

import android.annotation.SuppressLint
import android.app.NativeActivity
import android.os.Bundle
import android.os.Build
import android.view.View
import android.view.WindowManager
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import com.sbro.emucorev.nativeui.DeviceStatusSource
import com.sbro.emucorev.nativeui.BundledImages
import com.sbro.emucorev.nativeui.BundledResources

/** Independent Android host; it loads only our native UI library. */
@Suppress("DEPRECATION")
class PreviewActivity : NativeActivity() {
    private lateinit var deviceStatus: DeviceStatusSource
    private lateinit var images: BundledImages
    private var previewScene = 0
    private external fun nativeImage(id: Int, width: Int, height: Int, rgba: ByteArray)
    private external fun nativePreviewScene(scene: Int)
    private external fun nativeResourceRoot(path: String)
    private external fun nativeHomeCatalog(
        ids: Array<String>, titles: Array<String>, contentIds: Array<String>,
        iconImageIds: IntArray, labelImageIds: IntArray,
    )
    private external fun nativeHomeLiveArea(
        titleId: String, backgroundId: Int, backgroundRect: FloatArray,
        gateId: Int, gateRect: FloatArray, frameImageIds: IntArray, frameRects: FloatArray,
        textImageIds: IntArray, textRects: FloatArray, textColors: IntArray,
        targetIds: Array<String>, targetRects: FloatArray,
    )
    private external fun nativeReleaseHomeCallback()
    private external fun nativeBackRequested()
    private var lastLiveAreaTarget: String? = null
    private var backCallback: OnBackInvokedCallback? = null

    private external fun nativeStatus(
        battery: Int, charging: Boolean, powerSource: Int,
        wifi: Int, connected: Boolean, rssi: Int, bluetooth: Int,
        minute: Long, zone: String, clock24: Boolean,
    )

    override fun onCreate(state: Bundle?) {
        val resources = BundledResources.prepare(this)
        nativeResourceRoot(resources.absolutePath)
        val sceneArgument = intent.extras?.get("scene")
        val sceneExtra = sceneArgument as? String
        val sceneInt = sceneArgument as? Int ?: -1
        previewScene = when {
            sceneExtra == "busy" || sceneInt == 1 -> 1
            sceneExtra == "shell" || sceneInt == 0 -> 0
            sceneExtra == "lock" || sceneInt == 2 -> 2
            sceneExtra == "home" || sceneInt == 3 -> 3
            else -> 2
        }
        nativePreviewScene(previewScene)
        super.onCreate(state)
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER
            preferredRefreshRate = 60f
        }
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        deviceStatus = DeviceStatusSource(this)
        images = BundledImages(resources, ::nativeImage)
        when (previewScene) {
            1 -> images.loadBusyImage()
            2 -> images.loadLockImages { publishHomeCatalog(it) }
            3 -> {
                images.loadShellImages()
                images.loadHomeImages { publishHomeCatalog(it) }
            }
            else -> images.loadShellImages()
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = OnBackInvokedCallback {
                if (::images.isInitialized) nativeBackRequested()
            }
            backCallback = callback
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback,
            )
        }
    }

    private fun publishHomeCatalog(entries: List<BundledImages.HomeCatalogEntry>) {
        nativeHomeCatalog(
            entries.map { it.id }.toTypedArray(),
            entries.map { it.title }.toTypedArray(),
            entries.map { it.contentId }.toTypedArray(),
            entries.map { it.iconImageId }.toIntArray(),
            entries.map { it.labelImageId }.toIntArray(),
        )
        images.loadHomeLiveAreas(entries) { snapshot ->
            nativeHomeLiveArea(
                snapshot.titleId,
                snapshot.backgroundImageId,
                snapshot.backgroundRect,
                snapshot.gateImageId,
                snapshot.gateRect,
                snapshot.frameImageIds,
                snapshot.frameRects,
                snapshot.textImageIds,
                snapshot.textRects,
                snapshot.textColors,
                snapshot.targetIds,
                snapshot.targetRects,
            )
        }
    }

    fun onNativeLiveAreaTarget(target: String) {
        lastLiveAreaTarget = target
    }

    @SuppressLint("GestureBackNavigation")
    override fun onBackPressed() {
        // NativeActivity otherwise finishes the Activity after the native input
        // callback returns, even when it handled Back to close LiveArea.
        if (::images.isInitialized) nativeBackRequested()
        else super.onBackPressed()
    }

    override fun onResume() {
        super.onResume()
        if (previewScene != 0 && previewScene != 2 && previewScene != 3) return
        deviceStatus.start { status ->
            images.updateClock(status)
            if (previewScene == 2) {
                images.updateLockClock(status)
            }
            nativeStatus(status.batteryPercent, status.charging, status.powerSource,
                status.wifiEnabled, status.wifiConnected, status.wifiRssi, status.bluetoothEnabled,
                status.epochMinute, status.timeZoneId, status.use24HourClock)
        }
    }

    override fun onPause() {
        deviceStatus.stop()
        super.onPause()
    }

    override fun onDestroy() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            backCallback?.let(onBackInvokedDispatcher::unregisterOnBackInvokedCallback)
            backCallback = null
        }
        if (::images.isInitialized) images.release()
        nativeReleaseHomeCallback()
        super.onDestroy()
    }

    companion object {
        init { System.loadLibrary("paf_preview") }
    }
}
