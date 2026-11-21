package com.sbro.emucorev.nativeui.shell

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
import android.graphics.Bitmap
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

/**
 * Native PS Vita shell host. It loads only our native UI library; the
 * application subclasses it to supply the installed-game catalog and to act on
 * user intentions (launching a title, leaving the shell). Browsing never
 * initialises the emulator core.
 */
@Suppress("DEPRECATION")
open class NativeShellActivity : NativeActivity() {
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
        Thread(runnable, "native-shell-startup").apply { isDaemon = true }
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
    private external fun nativeAppPage(
        app: String, key: Int, transition: Int, background: Int, header: Int, headerRect: FloatArray,
        listRect: FloatArray, rowImages: IntArray, rowPlates: IntArray, rowGlows: IntArray,
        rowGeometry: FloatArray, rowSelectable: BooleanArray, focus: Int,
    )
    private external fun nativeAppClose()
    private external fun nativeWaveThemes(): Array<String>
    private external fun nativeSetDisplayArea(mode: Int)
    private external fun nativeSetHomeTheme(wallpapers: IntArray, barColor: Int)
    private var lastLiveAreaTarget: String? = null
    @Volatile private var catalogEntries: List<BundledImages.HomeCatalogEntry> = emptyList()
    private val requestedLiveAreas = java.util.Collections.synchronizedSet(HashSet<String>())

    /** Home bubbles, in catalog order. Runs on a worker thread. */
    protected open fun loadCatalog(): List<BundledImages.HomeCatalogSource> = emptyList()

    /** A LiveArea gate/Start or one of its live items was activated. */
    protected open fun onLiveAreaTarget(titleId: String, target: String) = Unit

    /**
     * Built-in system applications, after the games as on a fresh Vita Home.
     * Icons/LiveAreas come from the prepared pack (system-apps/<title id>).
     */
    private fun systemCatalog(resources: File): List<BundledImages.HomeCatalogSource> =
        SYSTEM_APPS.mapNotNull { (id, title) ->
            val root = File(resources, "system-apps/$id")
            val icon = File(root, "icon0.png").takeIf(File::isFile) ?: return@mapNotNull null
            BundledImages.HomeCatalogSource(id, getString(title), "", icon,
                File(root, "livearea/contents").takeIf(File::isDirectory))
        }

    /** Back reached the idle shell (nothing left to close). Default: leave. */
    protected open fun onExitRequested() = finish()

    @Suppress("unused")
    fun onNativeExitRequested(reason: String) {
        mainHandler.post { if (!closed.get() && !isFinishing) onExitRequested() }
    }

    /** Real trophy data for the Trophy Collection application (read only). */
    protected open fun trophySource(): ShellTrophySource = ShellTrophySource { emptyList() }

    /** Full-screen applications reachable from their LiveArea gate. */
    private val settingsApp by lazy { SettingsApp(settingsActions) }

    protected open fun shellApps(): List<ShellApp> =
        listOf(TrophyCollectionApp(trophySource()), settingsApp)

    protected val shellThemes by lazy { ShellThemes(this) }
    @Volatile private var lastStatus: DeviceStatusSource.Snapshot? = null

    /**
     * Applies the active theme / chosen photos: Home page backgrounds (or the
     * wave), Start screen image and date colour, information bar colour.
     * Runs on the shell worker.
     */
    private fun applyTheme() {
        val loader = if (::images.isInitialized) images else return
        val theme = shellThemes.active()
        val home = if (shellThemes.homePhoto.isFile) listOf(shellThemes.homePhoto)
            else theme?.homeBackgrounds.orEmpty()
        val ids = home.mapIndexedNotNull { i, file ->
            ShellThemes.decode(file)?.let { bitmap ->
                try { loader.uploadBitmap(THEME_IMAGE_BASE + i, bitmap) } finally { bitmap.recycle() }
                THEME_IMAGE_BASE + i
            }
        }
        val start = if (shellThemes.startPhoto.isFile) shellThemes.startPhoto else theme?.startScreen
        loader.setLockTheme(start?.let { ShellThemes.decode(it) }, theme?.dateColor)
        lastStatus?.let(loader::updateLockClock)
        synchronized(nativeBridgeLock) {
            if (!closed.get()) nativeSetHomeTheme(ids.toIntArray(), theme?.barColor ?: 0)
        }
    }

    private fun pick(request: Int, mime: String) { mainHandler.post {
        if (closed.get() || isFinishing) return@post
        runCatching {
            startActivityForResult(android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE)
                type = mime
            }, request)
        }.onFailure { Log.w(TAG, "No document picker", it) }
    } }

    @Deprecated("NativeActivity has no activity result registry")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: android.content.Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        runApp {
            when (requestCode) {
                REQUEST_THEME -> shellThemes.import(uri).also { shellThemes.activeTheme = it.id }
                REQUEST_HOME_PHOTO -> shellThemes.savePhoto(uri, shellThemes.homePhoto)
                REQUEST_START_PHOTO -> shellThemes.savePhoto(uri, shellThemes.startPhoto)
            }
            applyTheme()
            settingsApp.refresh()
        }
    }

    /** Version shown in Settings > System > System Information. */
    protected open val applicationVersion: String
        get() = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull().orEmpty()

    protected val shellSettings by lazy { ShellSettings(this) }

    private val settingsActions = object : SettingsActions {
        override val settings get() = shellSettings
        override val waveThemes: List<String> get() = nativeWaveThemes().toList()
        override val applicationVersion get() = this@NativeShellActivity.applicationVersion
        override fun applyWaveTheme(name: String) = nativeSetWaveTheme(name)
        override fun applyDisplayArea(mode: Int) = nativeSetDisplayArea(mode)
        override fun applyClock() { mainHandler.post { deviceStatus.stop(); startStatusIfResumed() } }
        override fun exitMode() { mainHandler.post { if (!closed.get() && !isFinishing) onExitRequested() } }
        override val customThemes get() = shellThemes.installed().map { it.id to it.title }
        override val activeCustomTheme get() = shellThemes.activeTheme
        override fun selectWaveTheme(name: String) {
            shellSettings.waveTheme = name
            shellThemes.activeTheme = null
            nativeSetWaveTheme(name)
            applyTheme()
        }
        override fun selectCustomTheme(id: String) {
            shellThemes.activeTheme = id
            applyTheme()
        }
        override fun installTheme() = pick(REQUEST_THEME, "application/zip")
        override val hasHomePhoto get() = shellThemes.homePhoto.isFile
        override val hasStartPhoto get() = shellThemes.startPhoto.isFile
        override fun pickHomePhoto() = pick(REQUEST_HOME_PHOTO, "image/*")
        override fun pickStartPhoto() = pick(REQUEST_START_PHOTO, "image/*")
        override fun clearHomePhoto() { shellThemes.homePhoto.delete(); applyTheme() }
        override fun clearStartPhoto() { shellThemes.startPhoto.delete(); applyTheme() }
    }

    private val apps by lazy { shellApps().associateBy { it.titleId } }
    @Volatile private var resourceRoot: File? = null
    private val textureCache = HashMap<Int, Bitmap>()
    private val uploadedAppTextures = HashSet<Int>()
    private var pageSerial = 0

    private val appHost = object : ShellAppHost {
        override val resources: File get() = requireNotNull(resourceRoot) { "Shell resources are not ready" }
        override fun string(id: Int): String = getString(id)
        override fun playSound(id: Int) { soundBank?.play(id) }
        override fun texture(id: Int): Bitmap? = synchronized(textureCache) {
            textureCache[id] ?: appTexturePath(id)?.let { File(resources, it) }?.let(::decodeFile)
                ?.also { textureCache[id] = it }
        }
        override fun close() {
            synchronized(nativeBridgeLock) { if (!closed.get()) nativeAppClose() }
        }
        override fun show(page: ShellPage, transition: ShellTransition) = publishAppPage(page, transition)
    }

    private fun appTexturePath(id: Int): String? = TrophyCollectionApp.TEXTURES[id] ?: SettingsApp.TEXTURES[id]

    /** Uploads a page's rasters into a rotating id bank and hands it to native. */
    private fun publishAppPage(page: ShellPage, transition: ShellTransition) {
        val loader = if (::images.isInitialized) images else return
        // Three banks: the outgoing page keeps its images during the slide.
        val base = APP_IMAGE_BASE + (pageSerial++ % 3) * APP_IMAGE_BANK
        val textures = listOf(page.background) + page.rows.flatMap { listOf(it.plate, it.glow) }
        textures.forEach { id ->
            if (id != 0 && uploadedAppTextures.add(id)) appTexturePath(id)?.let { loader.loadTexture(id, it) }
        }
        val headerId = page.header?.let { loader.uploadBitmap(base, it); base } ?: 0
        val n = page.rows.size.coerceAtMost(APP_IMAGE_BANK - 1)
        val imageIds = IntArray(n)
        val geometry = FloatArray(n * 5)
        page.rows.take(n).forEachIndexed { i, row ->
            row.bitmap?.let { loader.uploadBitmap(base + 1 + i, it); imageIds[i] = base + 1 + i }
            geometry[i * 5] = row.x; geometry[i * 5 + 1] = row.width; geometry[i * 5 + 2] = row.height
            geometry[i * 5 + 3] = row.bitmap?.width?.toFloat() ?: 0f
            geometry[i * 5 + 4] = row.bitmap?.height?.toFloat() ?: 0f
        }
        page.header?.recycle()
        page.rows.forEach { it.bitmap?.recycle() }
        synchronized(nativeBridgeLock) {
            if (closed.get()) return
            nativeAppPage(page.app, page.key, transition.code, page.background, headerId, page.headerRect,
                page.listRect, imageIds, IntArray(n) { page.rows[it].plate }, IntArray(n) { page.rows[it].glow },
                geometry, BooleanArray(n) { page.rows[it].selectable }, page.focus)
        }
    }

    private fun runApp(block: () -> Unit) {
        startupExecutor.execute {
            runCatching(block).onFailure { Log.w(TAG, "Shell application failed", it) }
        }
    }

    @Suppress("unused")
    fun onNativeAppRow(app: String, key: Int, row: Int) {
        val target = apps[app] ?: return
        runApp { target.onRow(appHost, key, row) }
    }

    @Suppress("unused")
    fun onNativeAppBack(app: String, key: Int) {
        val target = apps[app] ?: return
        runApp { target.onBack(appHost, key) }
    }

    /** Scene to settle on when the Activity starts without an explicit request. */
    protected open val defaultScene: Int get() = 2
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
            else -> defaultScene // Cold launch settles on the Lock scene with Home ready behind it.
        }
        val waveTheme = intent.getStringExtra("wave_theme") ?: "effects_default_0512_01"
        // Show the original Shell topmenu busy animation while the pack is prepared.
        val startupRoot = File(noBackupFilesDir, "native-ui-startup").apply {
            check(isDirectory || mkdirs()) { "Cannot create startup resource directory" }
        }
        assets.open("startup-busy.png").use { input ->
            File(startupRoot, "startup-busy.png").outputStream().use { output -> input.copyTo(output) }
        }
        nativeSetWaveTheme(intent.getStringExtra("wave_theme") ?: shellSettings.waveTheme)
        nativeSetDisplayArea(shellSettings.displayArea)
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
            resourceRoot = resources
            val localLoader = BundledImages(resources, ::uploadImage)
            localLoader.liveAreaFallbackTexts = mapOf("User's Guide" to getString(R.string.native_ui_users_guide))
            loader = localLoader
            when (requestedScene) {
                2 -> localLoader.loadLockImages(loadCatalog() + systemCatalog(resources)) {
                    publishHomeCatalog(it, localLoader)
                }
                3 -> {
                    localLoader.loadShellImages()
                    localLoader.loadHomeImages(loadCatalog() + systemCatalog(resources)) {
                        publishHomeCatalog(it, localLoader)
                    }
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
                runApp { applyTheme() }
            }
            if (!posted) {
                readyLoader.release()
                readySounds.release()
            }
        } catch (_: CancellationException) {
            // Expected when the Activity is closed while decoding the pack.
        } catch (error: Exception) {
            Log.e(TAG, "Cannot prepare the native shell", error)
            mainHandler.post {
                if (!closed.get() && !isFinishing && !isDestroyed) {
                    synchronized(nativeBridgeLock) {
                        if (!closed.get()) {
                            nativePreviewLoading(false)
                            nativePreviewFailed()
                        }
                    }
                    onShellFailed(error)
                }
            }
        } finally {
            if (!handedOff) loader?.release()
        }
    }

    private fun uploadImage(id: Int, width: Int, height: Int, rgba: ByteArray) {
        synchronized(nativeBridgeLock) {
            if (closed.get()) throw CancellationException("Shell Activity was destroyed")
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
        catalogEntries = entries
        loader.loadNotificationText(getString(R.string.native_ui_no_notifications))
    }

    /** Native asks for a LiveArea right before it opens; decode it off the render thread. */
    @Suppress("unused")
    fun onNativeRequestLiveArea(titleId: String) {
        if (!requestedLiveAreas.add(titleId)) return
        val index = catalogEntries.indexOfFirst { it.id == titleId }
        if (index < 0) return
        val entry = catalogEntries[index]
        val startLabel = getString(R.string.native_ui_start)
        startupExecutor.execute {
            val loader = if (::images.isInitialized) images else return@execute
            val snapshot = runCatching { loader.loadHomeLiveArea(entry, index, startLabel) }
                .onFailure { Log.w(TAG, "Cannot load LiveArea for $titleId", it) }
                .getOrNull() ?: return@execute
            synchronized(nativeBridgeLock) {
                if (closed.get()) return@execute
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

    protected open fun onShellFailed(error: Throwable) {
        Toast.makeText(this, "Native shell failed; see logs", Toast.LENGTH_LONG).show()
    }

    @Suppress("unused")
    fun onNativeLiveAreaTarget(titleId: String, target: String) {
        lastLiveAreaTarget = target
        val app = apps[titleId]
        if (app != null && target == titleId) {
            runApp { app.open(appHost) }
            return
        }
        mainHandler.post { if (!closed.get() && !isFinishing) onLiveAreaTarget(titleId, target) }
    }

    @Suppress("unused")
    fun onNativeUiSound(soundId: Int) {
        if (shellSettings.systemSounds) soundBank?.play(soundId)
    }

    @Suppress("unused")
    fun onNativeSaveHomeLayout(layout: String) {
        try {
            val dest = File(noBackupFilesDir, "home-layout.txt")
            val tmp = File(noBackupFilesDir, "home-layout.txt.tmp")
            tmp.writeText(layout)
            tmp.renameTo(dest)
        } catch (error: Exception) {
            Log.w(TAG, "Could not persist home layout", error)
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
        deviceStatus.start { raw ->
            val status = when (shellSettings.clock) {
                1 -> raw.copy(use24HourClock = false)
                2 -> raw.copy(use24HourClock = true)
                else -> raw
            }
            lastStatus = status
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
        synchronized(textureCache) { textureCache.values.forEach(Bitmap::recycle); textureCache.clear() }
        if (::startupImages.isInitialized && (!::images.isInitialized || images !== startupImages)) {
            startupImages.release()
        }
        synchronized(nativeBridgeLock) { nativeReleaseHomeCallback() }
        super.onDestroy()
    }

    companion object {
        private const val TAG = "NativeShell"
        private const val APP_IMAGE_BASE = 60000
        private const val THEME_IMAGE_BASE = 70000
        private const val REQUEST_THEME = 7101
        private const val REQUEST_HOME_PHOTO = 7102
        private const val REQUEST_START_PHOTO = 7103
        private const val APP_IMAGE_BANK = 3000
        const val TROPHIES_TITLE_ID = "NPXS10008"
        const val SETTINGS_TITLE_ID = "NPXS10015"
        private val SYSTEM_APPS = listOf(
            TROPHIES_TITLE_ID to R.string.native_ui_trophies,
            SETTINGS_TITLE_ID to R.string.native_ui_settings,
        )
        init { System.loadLibrary("emucorev_shell") }
    }
}
