package com.sbro.emucorev.core.vita

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.system.Os
import android.content.pm.ActivityInfo
import android.view.InputDevice
import android.view.KeyEvent
import android.view.ViewGroup
import android.widget.Toast
import androidx.annotation.Keep
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.documentfile.provider.DocumentFile
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import com.sbro.emucorev.MainActivity
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.sdl.SDLActivity
import com.sbro.emucorev.core.sdl.SDLSurface
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.emulation.EmulationOverlayHost
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import java.io.File

class Emulator : SDLActivity(), InputManager.InputDeviceListener {
    private var currentGameId = ""
    private lateinit var surfaceView: EmuSurface
    private lateinit var inputOverlay: InputOverlay
    private var composeOverlayAttached = false
    private var exitRequested = false
    private lateinit var composeOwners: ComposeOwners
    private var inputManager: InputManager? = null
    private var overlayBackHandler: (() -> Boolean)? = null
    private var overlayMenuButtonRevealHandler: (() -> Unit)? = null
    private var menuPaused: Boolean = false

    var hasPhysicalGamepad by mutableStateOf(false)
        private set

    fun getmOverlay(): InputOverlay = inputOverlay

    @Keep
    fun setCurrentGameId(gameId: String) {
        currentGameId = gameId
    }

    @Keep
    fun getBaseStoragePath(): String {
        return (getExternalFilesDir(null) ?: filesDir).absolutePath
    }

    fun currentGameIdOrIntent(): String {
        if (currentGameId.isNotBlank()) return currentGameId
        val action = intent?.action.orEmpty()
        if (!action.startsWith("LAUNCH_")) return ""
        val payload = action.removePrefix("LAUNCH_")
        return payload.substringBeforeLast('_', payload)
    }

    fun setOverlayBackHandler(handler: (() -> Boolean)?) {
        overlayBackHandler = handler
    }

    fun setOverlayMenuButtonRevealHandler(handler: (() -> Unit)?) {
        overlayMenuButtonRevealHandler = handler
    }

    fun requestOverlayMenuButtonReveal() {
        runOnUiThread {
            overlayMenuButtonRevealHandler?.invoke()
        }
    }

    override fun getLibraries(): Array<String> = arrayOf("Vita3K")

    override fun createSDLSurface(context: Context): SDLSurface {
        if (!::inputOverlay.isInitialized) {
            inputOverlay = InputOverlay(this)
        }
        surfaceView = EmuSurface(context)
        return surfaceView
    }

    override fun getArguments(): Array<String> {
        return intent.getStringArrayExtra(APP_RESTART_PARAMETERS) ?: emptyArray()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        val action = intent.action ?: return
        if (action.startsWith("LAUNCH_")) {
            val payload = action.removePrefix("LAUNCH_")
            val gameId = payload.substringBeforeLast('_', payload)
            if (gameId.isNotBlank()) {
                currentGameId = gameId
            }
            ProcessPhoenix.triggerRebirth(this, markRebirthHandled(intent))
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        if (!::inputOverlay.isInitialized) {
            inputOverlay = InputOverlay(this)
        }
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        EmulatorStorage.prepareRuntime(this)
        VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        composeOwners = ComposeOwners().also { it.performCreate(savedInstanceState) }
        inputManager = getSystemService(InputManager::class.java)
        hasPhysicalGamepad = detectPhysicalGamepadConnected()
        inputManager?.registerInputDeviceListener(this, null)
        attachComposeOverlay()
        hideSystemBars()
    }

    override fun onResume() {
        super.onResume()
        composeOwners.handleResume()
        hideSystemBars()
        if (menuPaused) {
            pauseNativeThread()
        }
    }

    override fun onPause() {
        composeOwners.handlePause()
        super.onPause()
    }

    override fun onDestroy() {
        inputManager?.unregisterInputDeviceListener(this)
        inputManager = null
        if (::inputOverlay.isInitialized) {
            inputOverlay.dispose()
        }
        if (::composeOwners.isInitialized) {
            composeOwners.handleDestroy()
        }
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
        }
    }

    @Keep
    fun restartApp(appPath: String, execPath: String, execArgs: String) {
        val args = arrayListOf("-a", "true")
        if (appPath.isNotEmpty()) {
            args += "-r"
            args += appPath
            if (execPath.isNotEmpty()) {
                args += "--self"
                args += execPath
                if (execArgs.isNotEmpty()) {
                    args += "--app-args"
                    args += execArgs
                }
            }
        }

        val restartIntent = Intent(this, Emulator::class.java).apply {
            putExtra(APP_RESTART_PARAMETERS, args.toTypedArray())
        }
        ProcessPhoenix.triggerRebirth(this, restartIntent)
    }

    @Keep
    fun setStoragePermission() {
        startActivity(
            Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                .setData(Uri.parse("package:${BuildConfig.APPLICATION_ID}"))
        )
    }

    @Keep
    fun showFileDialog() {
        if (!isStorageManagerEnabled()) {
            setStoragePermission()
            return
        }

        val pickerIntent = Intent()
            .setType("*/*")
            .setAction(Intent.ACTION_GET_CONTENT)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)

        @Suppress("DEPRECATION")
        startActivityForResult(Intent.createChooser(pickerIntent, getString(R.string.emulator_choose_file)), FILE_DIALOG_CODE)
    }

    @Keep
    fun isStorageManagerEnabled(): Boolean {
        return if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            true
        } else {
            Environment.isExternalStorageManager()
        }
    }

    @Keep
    fun showFolderDialog() {
        if (!isStorageManagerEnabled()) {
            setStoragePermission()
            return
        }

        val pickerIntent = Intent()
            .setAction(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)

        @Suppress("DEPRECATION")
        startActivityForResult(Intent.createChooser(pickerIntent, getString(R.string.emulator_choose_folder)), FOLDER_DIALOG_CODE)
    }

    private fun resolveUriToPath(resultUri: Uri): String {
        return try {
            contentResolver.openFileDescriptor(resultUri, "r")?.use { descriptor ->
                var resultPath = Os.readlink("/proc/self/fd/${descriptor.fd}")
                if (resultPath.startsWith("/mnt/user/")) {
                    resultPath = resultPath.substring("/mnt/user/".length)
                    resultPath = "/storage" + resultPath.substring(resultPath.indexOf('/'))
                }
                resultPath
            }.orEmpty()
        } catch (_: Exception) {
            ""
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        when (requestCode) {
            FILE_DIALOG_CODE, FOLDER_DIALOG_CODE -> {
                if (resultCode != RESULT_OK) {
                    filedialogReturn("")
                    return
                }
                var resultUri = data?.data
                if (resultUri == null) {
                    filedialogReturn("")
                    return
                }
                if (requestCode == FOLDER_DIALOG_CODE) {
                    resultUri = DocumentFile.fromTreeUri(applicationContext, resultUri)?.uri ?: resultUri
                }
                filedialogReturn(resolveUriToPath(resultUri))
            }
        }
    }

    @Keep
    fun setControllerOverlayState(overlayMask: Int, edit: Boolean, reset: Boolean) {
        runOnUiThread {
            getmOverlay().setState(overlayMask)
            getmOverlay().setIsInEditMode(edit)
            if (reset) {
                getmOverlay().resetButtonPlacement()
            }
        }
    }

    @Keep
    fun setControllerOverlayScale(scale: Float) {
        runOnUiThread {
            getmOverlay().setScale(scale)
        }
    }

    @Keep
    fun setControllerOverlayOpacity(opacity: Int) {
        runOnUiThread {
            getmOverlay().setOpacity(opacity)
        }
    }

    external fun setPerformanceOverlayState(enabled: Boolean, detail: Int, position: Int)

    fun setMenuPaused(paused: Boolean) {
        if (menuPaused == paused) return
        menuPaused = paused
        if (paused) {
            pauseNativeThread()
        } else {
            resumeNativeThread()
            hideSystemBars()
        }
    }

    fun exitEmulation() {
        if (exitRequested) return
        exitRequested = true
        runOnUiThread {
            runCatching { resumeNativeThread() }
            runCatching { performNativeShutdown() }
            runCatching {
                startActivity(
                    Intent(this, MainActivity::class.java).apply {
                        addFlags(
                            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                                Intent.FLAG_ACTIVITY_SINGLE_TOP or
                                Intent.FLAG_ACTIVITY_NEW_TASK
                        )
                    }
                )
            }
            if (!isFinishing && !isDestroyed) {
                finish()
            }
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (overlayBackHandler?.invoke() == true) return
        super.onBackPressed()
    }

    override fun superOnBackPressed() {
        if (overlayBackHandler?.invoke() == true) return
        super.superOnBackPressed()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK &&
            event.action == KeyEvent.ACTION_UP &&
            !event.isCanceled
        ) {
            if (overlayBackHandler?.invoke() == true) return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onInputDeviceAdded(deviceId: Int) {
        hasPhysicalGamepad = detectPhysicalGamepadConnected()
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        hasPhysicalGamepad = detectPhysicalGamepadConnected()
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        hasPhysicalGamepad = detectPhysicalGamepadConnected()
    }

    @Keep
    fun createShortcut(gameId: String, gameName: String): Boolean {
        if (!ShortcutManagerCompat.isRequestPinShortcutSupported(this)) {
            return false
        }

        val iconFile = File(getExternalFilesDir(null), "cache/icons/$gameId.png")
        val icon: Bitmap = if (iconFile.exists()) {
            BitmapFactory.decodeFile(iconFile.path)
        } else {
            BitmapFactory.decodeResource(resources, R.mipmap.ic_launcher)
        }

        val launchIntent = Intent(this, Emulator::class.java).apply {
            putExtra(APP_RESTART_PARAMETERS, arrayOf("-r", gameId))
            action = "LAUNCH_$gameId"
        }

        val shortcut = ShortcutInfoCompat.Builder(this, gameId)
            .setShortLabel(gameName)
            .setLongLabel(gameName)
            .setIcon(IconCompat.createWithBitmap(icon))
            .setIntent(launchIntent)
            .build()
        ShortcutManagerCompat.requestPinShortcut(this, shortcut, null)
        return true
    }

    @Keep
    fun requestInstallUpdate() {
        runOnUiThread {
            val apkFile = File(getExternalFilesDir(null), "vita3k-latest.apk")
            if (!apkFile.exists()) {
                Toast.makeText(this, getString(R.string.emulator_installer_apk_missing), Toast.LENGTH_LONG).show()
                return@runOnUiThread
            }

            try {
                val apkUri = FileProvider.getUriForFile(
                    this,
                    "${BuildConfig.APPLICATION_ID}.fileprovider",
                    apkFile
                )
                startActivity(
                    Intent(Intent.ACTION_VIEW).apply {
                        setDataAndType(apkUri, "application/vnd.android.package-archive")
                        flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK
                    }
                )
            } catch (error: Exception) {
                error.printStackTrace()
                Toast.makeText(this, getString(R.string.emulator_installer_failed), Toast.LENGTH_LONG).show()
            }
        }
    }

    @Keep
    fun getNativeDisplayRotation(): Int {
        @Suppress("DEPRECATION")
        return windowManager.defaultDisplay.rotation
    }

    external fun filedialogReturn(resultPath: String)

    private fun attachComposeOverlay() {
        val layout = mLayout ?: return
        if (composeOverlayAttached) return
        composeOverlayAttached = true
        layout.setViewTreeLifecycleOwner(composeOwners)
        layout.setViewTreeSavedStateRegistryOwner(composeOwners)
        layout.setViewTreeViewModelStoreOwner(composeOwners)
        val composeView = ComposeView(this).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
            setViewTreeLifecycleOwner(composeOwners)
            setViewTreeSavedStateRegistryOwner(composeOwners)
            setViewTreeViewModelStoreOwner(composeOwners)
            setContent {
                EmuCoreVTheme {
                    EmulationOverlayHost(activity = this@Emulator)
                }
            }
        }
        layout.addView(
            composeView,
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    private fun detectPhysicalGamepadConnected(): Boolean {
        return InputDevice.getDeviceIds().any { deviceId ->
            InputDevice.getDevice(deviceId)?.let { device ->
                !device.isVirtual && isGamepadSource(device.sources)
            } == true
        }
    }

    private fun isGamepadSource(sources: Int): Boolean {
        return (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
            (sources and InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD
    }

    private fun markRebirthHandled(sourceIntent: Intent): Intent {
        return Intent(sourceIntent).putExtra(EXTRA_REBIRTH_HANDLED, true)
    }

    private fun isSpecialLaunchAction(action: String): Boolean {
        return action.startsWith("LAUNCH_") ||
            action.startsWith("INSTALL_FIRMWARE_") ||
            action.startsWith("INSTALL_CONTENT_") ||
            action.startsWith("INSTALL_PKG_")
    }

    private companion object {
        const val APP_RESTART_PARAMETERS = "AppStartParameters"
        const val FILE_DIALOG_CODE = 545
        const val FOLDER_DIALOG_CODE = 546
        const val EXTRA_REBIRTH_HANDLED = "emu_rebirth_handled"
    }
}

private class ComposeOwners : LifecycleOwner, SavedStateRegistryOwner, ViewModelStoreOwner {
    private val lifecycleRegistry = LifecycleRegistry(this)
    private val savedStateController = SavedStateRegistryController.create(this)
    private val internalViewModelStore = ViewModelStore()

    override val lifecycle: Lifecycle
        get() = lifecycleRegistry

    override val savedStateRegistry: SavedStateRegistry
        get() = savedStateController.savedStateRegistry

    override val viewModelStore: ViewModelStore
        get() = internalViewModelStore

    fun performCreate(savedInstanceState: Bundle?) {
        savedStateController.performAttach()
        savedStateController.performRestore(savedInstanceState)
        lifecycleRegistry.currentState = Lifecycle.State.CREATED
        lifecycleRegistry.currentState = Lifecycle.State.STARTED
    }

    fun handleResume() {
        lifecycleRegistry.currentState = Lifecycle.State.RESUMED
    }

    fun handlePause() {
        lifecycleRegistry.currentState = Lifecycle.State.STARTED
    }

    fun handleDestroy() {
        lifecycleRegistry.currentState = Lifecycle.State.DESTROYED
        internalViewModelStore.clear()
    }
}
