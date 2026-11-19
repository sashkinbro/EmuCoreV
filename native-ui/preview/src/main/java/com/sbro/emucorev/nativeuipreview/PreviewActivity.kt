package com.sbro.emucorev.nativeuipreview

import android.app.NativeActivity
import android.os.Bundle
import android.view.View
import android.view.WindowManager
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
        when (previewScene) { 1 -> images.loadBusyImage(); 2 -> images.loadLockImages(); else -> images.loadShellImages() }
    }

    override fun onResume() {
        super.onResume()
        if (previewScene != 0 && previewScene != 2) return
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
        if (::images.isInitialized) images.release()
        super.onDestroy()
    }

    companion object {
        init { System.loadLibrary("paf_preview") }
    }
}
