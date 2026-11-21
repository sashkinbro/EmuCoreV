package com.sbro.emucorev.nativeuipreview

import android.annotation.SuppressLint
import android.app.NativeActivity
import android.os.Bundle
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.RoundedCorner
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import com.sbro.emucorev.nativeui.DeviceStatusSource
import com.sbro.emucorev.nativeui.BundledImages
import com.sbro.emucorev.nativeui.BundledResources
import com.sbro.emucorev.nativeui.NativeUiSoundBank
import android.graphics.BitmapFactory
import android.text.format.DateFormat
import android.util.Log
import android.widget.Toast
import java.io.File
import java.util.TimeZone
import java.util.concurrent.CancellationException
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.atomic.AtomicBoolean

/** Independent Android host; it loads only our native UI library. */
@Suppress("DEPRECATION")
class PreviewActivity : NativeActivity() {
    private lateinit var deviceStatus: DeviceStatusSource
    @Volatile private lateinit var images: BundledImages
    private lateinit var startupImages: BundledImages
    @Volatile private var soundBank: NativeUiSoundBank? = null
    private var requestedScene = 2
    @Volatile private var ready = false
    private val closed = AtomicBoolean(false)
    private var resumed = false
    private var startupTask: Future<*>? = null
    private val startupExecutor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "native-ui-preview-startup").apply { isDaemon = true }
    }
    private val mainHandler = Handler(Looper.getMainLooper())
    private val nativeBridgeLock = Any()
    private external fun nativeImage(id: Int, width: Int, height: Int, rgba: ByteArray)
    private external fun nativePreviewScene(scene: Int)
    private external fun nativeSetWaveTheme(theme: String)
    private external fun nativePreviewLoading(loading: Boolean)
    private external fun nativePreviewReady(scene: Int)
    private external fun nativePreviewFailed()
    private external fun nativeResourceRoot(path: String)
    private external fun nativeHomeCatalog(
        ids: Array<String>, titles: Array<String>, contentIds: Array<String>,
        iconImageIds: IntArray, labelImageIds: IntArray,
    )
    private external fun nativeHomeLiveArea(
        titleId: String, backgroundId: Int, backgroundRect: FloatArray,
        gateId: Int, gateRect: FloatArray, gateStartLabelImageId: Int, gateControlImageId: Int,
        frameImageIds: IntArray, frameRects: FloatArray,
        textImageIds: IntArray, textRects: FloatArray, textColors: IntArray,
        targetIds: Array<String>, targetRects: FloatArray,
    )
    private external fun nativeReleaseHomeCallback()
    private external fun nativeBackRequested()
    private external fun nativeSafeArea(area: FloatArray)
    private external fun nativeLoadHomeLayout(layout: String)
    private var lastLiveAreaTarget: String? = null
    private var backCallback: OnBackInvokedCallback? = null

    private external fun nativeStatus(
        battery: Int, charging: Boolean, powerSource: Int,
        wifi: Int, connected: Boolean, rssi: Int, bluetooth: Int,
        minute: Long, zone: String, clock24: Boolean,
    )

    override fun onCreate(state: Bundle?) {
        val sceneArgument = intent.extras?.get("scene")
        val sceneExtra = sceneArgument as? String
        val sceneInt = sceneArgument as? Int ?: -1
        requestedScene = when {
            sceneExtra == "busy" || sceneInt == 1 -> 1
            sceneExtra == "shell" || sceneInt == 0 -> 0
            sceneExtra == "lock" || sceneInt == 2 -> 2
            sceneExtra == "home" || sceneInt == 3 -> 3
            sceneExtra == "wave" || sceneInt == 4 -> 4
            else -> 2 // Cold launch settles on the Lock scene with Home ready behind it.
        }
        val waveTheme = intent.getStringExtra("wave_theme") ?: "effects_default_0512_01"
        // Show the original Shell topmenu busy animation while the pack is prepared.
        val startupRoot = File(noBackupFilesDir, "native-ui-startup").apply {
            check(isDirectory || mkdirs()) { "Cannot create startup resource directory" }
        }
        assets.open("startup-busy.png").use { input ->
            File(startupRoot, "startup-busy.png").outputStream().use { output -> input.copyTo(output) }
        }
        nativeSetWaveTheme(waveTheme)
        nativeResourceRoot(startupRoot.absolutePath)
        nativePreviewScene(if (requestedScene == 4) 4 else 1)
        nativePreviewLoading(requestedScene != 1 && requestedScene != 4)
        super.onCreate(state)
        window.attributes = window.attributes.apply {
            // Draw edge to edge; the native canvas keeps chrome inside the
            // reported cutout/rounded-corner safe area itself.
            layoutInDisplayCutoutMode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            preferredRefreshRate = 60f
        }
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        window.decorView.setOnApplyWindowInsetsListener { view, insets ->
            publishSafeArea(insets)
            view.onApplyWindowInsets(insets)
        }
        deviceStatus = DeviceStatusSource(this)
        startupImages = BundledImages(startupRoot, ::uploadImage)
        val busy = File(startupRoot, "startup-busy.png").inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid startup busy image" }
        }
        try { startupImages.loadBusyImage(busy) } finally { busy.recycle() }
        if (requestedScene == 1 || requestedScene == 4) {
            images = startupImages
            ready = true
        } else {
            startupTask = startupExecutor.submit { preparePreview() }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = OnBackInvokedCallback {
                if (!closed.get() && ::images.isInitialized) nativeBackRequested()
            }
            backCallback = callback
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback,
            )
        }
    }

    private fun preparePreview() {
        var loader: BundledImages? = null
        var handedOff = false
        try {
            val resources = BundledResources.prepare(this)
            if (Thread.currentThread().isInterrupted || closed.get()) return
            synchronized(nativeBridgeLock) {
                if (closed.get()) return
                nativeResourceRoot(resources.absolutePath)
            }
            val localLoader = BundledImages(resources, ::uploadImage)
            loader = localLoader
            when (requestedScene) {
                2 -> localLoader.loadLockImages { publishHomeCatalog(it, localLoader) }
                3 -> {
                    localLoader.loadShellImages()
                    localLoader.loadHomeImages { publishHomeCatalog(it, localLoader) }
                }
                else -> localLoader.loadShellImages()
            }
            val seedClock = DeviceStatusSource.Snapshot(
                batteryPercent = 100,
                charging = false,
                powerSource = 0,
                wifiEnabled = DeviceStatusSource.UNKNOWN,
                wifiConnected = false,
                wifiRssi = Int.MIN_VALUE,
                bluetoothEnabled = DeviceStatusSource.UNKNOWN,
                epochMinute = System.currentTimeMillis() / 60000L,
                timeZoneId = TimeZone.getDefault().id,
                use24HourClock = DateFormat.is24HourFormat(this),
            )
            localLoader.updateClock(seedClock)
            if (requestedScene == 2) {
                localLoader.updateLockClock(seedClock)
            }
            val readySounds = NativeUiSoundBank(resources)
            if (Thread.currentThread().isInterrupted || closed.get()) {
                readySounds.release()
                return
            }
            val readyLoader = localLoader
            handedOff = true
            val posted = mainHandler.post {
                if (closed.get() || isFinishing || isDestroyed) {
                    readyLoader.release()
                    readySounds.release()
                    return@post
                }
                val published = synchronized(nativeBridgeLock) {
                    if (closed.get()) {
                        readyLoader.release()
                        readySounds.release()
                        false
                    } else {
                        images = readyLoader
                        soundBank?.release()
                        soundBank = readySounds
                        nativePreviewReady(requestedScene)
                        ready = true
                        true
                    }
                }
                if (!published) return@post
                startStatusIfResumed()
            }
            if (!posted) {
                readyLoader.release()
                readySounds.release()
            }
        } catch (_: CancellationException) {
            // Expected when the Activity is closed while decoding the pack.
        } catch (error: Exception) {
            Log.e("NativeUiPreview", "Cannot prepare offline native UI preview", error)
            mainHandler.post {
                if (!closed.get() && !isFinishing && !isDestroyed) {
                    synchronized(nativeBridgeLock) {
                        if (!closed.get()) {
                            nativePreviewLoading(false)
                            nativePreviewFailed()
                        }
                    }
                    // This Activity is a local preview/debug host; keep the
                    // failure diagnostic scoped to the development surface.
                    Toast.makeText(this, "Native UI preview failed; see logs", Toast.LENGTH_LONG).show()
                }
            }
        } finally {
            if (!handedOff) loader?.release()
        }
    }

    private fun uploadImage(id: Int, width: Int, height: Int, rgba: ByteArray) {
        synchronized(nativeBridgeLock) {
            if (closed.get()) throw CancellationException("Preview Activity was destroyed")
            nativeImage(id, width, height, rgba)
        }
    }

    private fun publishHomeCatalog(
        entries: List<BundledImages.HomeCatalogEntry>,
        loader: BundledImages,
    ) {
        val ids = entries.map { it.id }.toTypedArray()
        val titles = entries.map { it.title }.toTypedArray()
        val contentIds = entries.map { it.contentId }.toTypedArray()
        val iconIds = entries.map { it.iconImageId }.toIntArray()
        val labelIds = entries.map { it.labelImageId }.toIntArray()
        synchronized(nativeBridgeLock) {
            if (closed.get()) throw CancellationException("Preview Activity was destroyed")
            nativeHomeCatalog(ids, titles, contentIds, iconIds, labelIds)
            val layoutFile = File(noBackupFilesDir, "home-layout.txt")
            if (layoutFile.isFile) {
                nativeLoadHomeLayout(layoutFile.readText())
            }
        }
        loader.loadNotificationText(getString(R.string.native_ui_no_notifications))
        loader.loadHomeLiveAreas(entries, getString(R.string.native_ui_start)) { snapshot ->
            synchronized(nativeBridgeLock) {
                if (closed.get()) throw CancellationException("Preview Activity was destroyed")
                nativeHomeLiveArea(
                    snapshot.titleId,
                    snapshot.backgroundImageId,
                    snapshot.backgroundRect,
                    snapshot.gateImageId,
                    snapshot.gateRect,
                    snapshot.gateStartLabelImageId,
                    snapshot.gateControlImageId,
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
    }

    fun onNativeLiveAreaTarget(target: String) {
        lastLiveAreaTarget = target
    }

    @Suppress("unused")
    fun onNativeUiSound(soundId: Int) {
        soundBank?.play(soundId)
    }

    @Suppress("unused")
    fun onNativeSaveHomeLayout(layout: String) {
        try {
            val dest = File(noBackupFilesDir, "home-layout.txt")
            val tmp = File(noBackupFilesDir, "home-layout.txt.tmp")
            tmp.writeText(layout)
            tmp.renameTo(dest)
        } catch (error: Exception) {
            Log.w("NativeUiPreview", "Could not persist home layout", error)
        }
    }

    private fun publishSafeArea(insets: WindowInsets) {
        val area = FloatArray(8)
        insets.displayCutout?.let {
            area[0] = it.safeInsetLeft.toFloat(); area[1] = it.safeInsetTop.toFloat()
            area[2] = it.safeInsetRight.toFloat(); area[3] = it.safeInsetBottom.toFloat()
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val corners = intArrayOf(
                RoundedCorner.POSITION_TOP_LEFT, RoundedCorner.POSITION_TOP_RIGHT,
                RoundedCorner.POSITION_BOTTOM_RIGHT, RoundedCorner.POSITION_BOTTOM_LEFT,
            )
            corners.forEachIndexed { index, position ->
                area[4 + index] = insets.getRoundedCorner(position)?.radius?.toFloat() ?: 0f
            }
        }
        nativeSafeArea(area)
    }

    @SuppressLint("GestureBackNavigation")
    override fun onBackPressed() {
        // NativeActivity otherwise finishes the Activity after the native input
        // callback returns, even when it handled Back to close LiveArea.
        if (!closed.get() && ::images.isInitialized) nativeBackRequested()
        else super.onBackPressed()
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        soundBank?.onResume()
        startStatusIfResumed()
    }

    private fun startStatusIfResumed() {
        if (!resumed || !ready || (requestedScene != 0 && requestedScene != 2 && requestedScene != 3)) return
        deviceStatus.start { status ->
            images.updateClock(status)
            if (requestedScene == 2) {
                images.updateLockClock(status)
            }
            nativeStatus(status.batteryPercent, status.charging, status.powerSource,
                status.wifiEnabled, status.wifiConnected, status.wifiRssi, status.bluetoothEnabled,
                status.epochMinute, status.timeZoneId, status.use24HourClock)
        }
    }

    override fun onPause() {
        resumed = false
        soundBank?.onPause()
        deviceStatus.stop()
        super.onPause()
    }

    override fun onDestroy() {
        synchronized(nativeBridgeLock) { closed.set(true) }
        startupTask?.cancel(true)
        startupExecutor.shutdownNow()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            backCallback?.let(onBackInvokedDispatcher::unregisterOnBackInvokedCallback)
            backCallback = null
        }
        soundBank?.release()
        soundBank = null
        if (::images.isInitialized) images.release()
        if (::startupImages.isInitialized && (!::images.isInitialized || images !== startupImages)) {
            startupImages.release()
        }
        synchronized(nativeBridgeLock) { nativeReleaseHomeCallback() }
        super.onDestroy()
    }

    companion object {
        init { System.loadLibrary("paf_preview") }
    }
}
