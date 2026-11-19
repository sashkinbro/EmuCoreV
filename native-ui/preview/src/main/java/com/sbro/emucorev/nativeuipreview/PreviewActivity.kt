package com.sbro.emucorev.nativeuipreview

import android.app.NativeActivity
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import com.sbro.emucorev.nativeui.DeviceStatusSource
import com.sbro.emucorev.nativeui.BundledImages

/** Independent Android host; it loads only our native UI library. */
@Suppress("DEPRECATION")
class PreviewActivity : NativeActivity() {
    private lateinit var deviceStatus: DeviceStatusSource
    private lateinit var images: BundledImages
    private var busyPreview = false
    private external fun nativeImage(id: Int, width: Int, height: Int, rgba: ByteArray)
    private external fun nativePreviewScene(busy: Boolean)

    private external fun nativeStatus(
        battery: Int, charging: Boolean, powerSource: Int,
        wifi: Int, connected: Boolean, rssi: Int, bluetooth: Int,
        minute: Long, zone: String, clock24: Boolean,
    )

    override fun onCreate(state: Bundle?) {
        busyPreview = intent.getStringExtra("scene") == "busy"
        nativePreviewScene(busyPreview)
        super.onCreate(state)
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER
            preferredRefreshRate = 60f
        }
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        deviceStatus = DeviceStatusSource(this)
        images = BundledImages(assets, ::nativeImage)
        if (busyPreview) images.loadBusyImage() else images.loadShellImages()
    }

    override fun onResume() {
        super.onResume()
        if (busyPreview) return
        deviceStatus.start { status ->
            images.updateClock(status)
            nativeStatus(status.batteryPercent, status.charging, status.powerSource,
                status.wifiEnabled, status.wifiConnected, status.wifiRssi, status.bluetoothEnabled,
                status.epochMinute, status.timeZoneId, status.use24HourClock)
        }
    }

    override fun onPause() {
        deviceStatus.stop()
        super.onPause()
    }

    companion object {
        init { System.loadLibrary("paf_preview") }
    }
}
