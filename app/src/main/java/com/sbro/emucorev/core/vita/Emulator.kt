package com.sbro.emucorev.core.vita

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Bundle
import android.system.Os
import android.text.InputType
import android.content.pm.ActivityInfo
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.ViewGroup
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Toast
import androidx.annotation.Keep
import androidx.compose.runtime.getValue
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.runtime.setValue
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.core.view.ViewCompat
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
import com.sbro.emucorev.core.PlayTimeRepository
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.core.input.InputDeviceClassifier
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLControllerManager
import org.libsdl.app.SDLSurface
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.ui.common.ImmersiveMode
import com.sbro.emucorev.ui.emulation.EmulationOverlayHost
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import java.io.File

class Emulator : SDLActivity(), InputManager.InputDeviceListener {
    // Private backing state so that Compose observes changes when the native
    // core calls setCurrentGameId(). Using a separate computed property avoids
    // a JVM signature clash between the property setter and the @Keep JNI method.
    private var _currentGameId by mutableStateOf("")
    val currentGameId: String get() = _currentGameId
    private lateinit var surfaceView: EmuSurface
    private lateinit var inputOverlay: InputOverlay
    private var composeOverlayAttached = false
    private var exitRequested = false
    private lateinit var composeOwners: ComposeOwners
    private var inputManager: InputManager? = null
    private var overlayBackHandler: (() -> Boolean)? = null
    private var overlayMenuButtonRevealHandler: (() -> Unit)? = null
    private var overlayPauseMenuOpenHandler: (() -> Unit)? = null
    private var menuPaused: Boolean = false
    private var nativeThreadPaused: Boolean = false
    private var playTimeSessionId: String? = null
    private var playTimeSessionTitleId: String = ""
    private var playTimeSessionStartedAt: Long = 0L
    var nativeImeState by mutableStateOf<NativeImeState?>(null)
        private set
    var nativeKeyboardRequested by mutableStateOf(false)
        private set
    var useBuiltInKeyboard by mutableStateOf(false)
        private set
    private var keyboardRequestGeneration = 0

    var hasPhysicalGamepad by mutableStateOf(false)
        private set

    fun getmOverlay(): InputOverlay = inputOverlay

    @Keep
    fun setCurrentGameId(gameId: String) {
        _currentGameId = gameId
        refreshGamepadRuntimeInputSettings()
        startPlayTimeSessionIfNeeded()
    }

    @Keep
    fun getBaseStoragePath(): String {
        return EmulatorStorage.runtimeRoot(this).absolutePath
    }

    fun currentGameIdOrIntent(): String {
        if (currentGameId.isNotBlank()) return currentGameId
        return gameIdFromIntent(intent)
    }

    fun setOverlayBackHandler(handler: (() -> Boolean)?) {
        overlayBackHandler = handler
    }

    fun setOverlayMenuButtonRevealHandler(handler: (() -> Unit)?) {
        overlayMenuButtonRevealHandler = handler
    }

    fun setOverlayPauseMenuOpenHandler(handler: (() -> Unit)?) {
        overlayPauseMenuOpenHandler = handler
    }

    fun requestOverlayMenuButtonReveal() {
        runOnUiThread {
            overlayMenuButtonRevealHandler?.invoke()
        }
    }

    @Keep
    fun openPauseMenuFromController() {
        runOnUiThread {
            overlayPauseMenuOpenHandler?.invoke()
        }
    }

    override fun getLibraries(): Array<String> = arrayOf("Vita3K")

    override fun shouldForceFullscreen(): Boolean = true

    override fun createSDLSurface(context: Context): SDLSurface {
        if (!::inputOverlay.isInitialized) {
            inputOverlay = InputOverlay(this)
        }
        surfaceView = EmuSurface(context)
        return surfaceView
    }

    override fun getArguments(): Array<String> {
        val args = intent.getStringArrayExtra(APP_RESTART_PARAMETERS)
        if (args != null) {
            val gameId = gameIdFromArgs(args)
            Log.i(TAG, "Starting emulator with args=${args.joinToString(" ")} gameId=$gameId")
            syncEffectiveDriverForLaunch(gameId)
            return args
        }
        val gameId = gameIdFromIntent(intent)
        Log.i(TAG, "Starting emulator from intent action=${intent?.action.orEmpty()} gameId=$gameId")
        syncEffectiveDriverForLaunch(gameId)
        return if (gameId.isBlank()) emptyArray() else arrayOf("-r", gameId)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (isLaunchIntent(intent)) {
            val gameId = gameIdFromIntent(intent)
            if (gameId.isNotBlank()) {
                _currentGameId = gameId
            }
            triggerRebirthAfterNativeShutdown(markRebirthHandled(intent))
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
        refreshGamepadRuntimeInputSettings()
        startPlayTimeSessionIfNeeded()
        composeOwners = ComposeOwners().also { it.performCreate(savedInstanceState) }
        inputManager = getSystemService(InputManager::class.java)
        refreshPhysicalGamepadState()
        inputManager?.registerInputDeviceListener(this, null)
        attachComposeOverlay()
        hideSystemBars()
    }

    override fun onResume() {
        super.onResume()
        composeOwners.handleResume()
        attachComposeOverlay()
        refreshGamepadRuntimeInputSettings()
        refreshPhysicalGamepadState()
        hideSystemBars()
        if (nativeKeyboardRequested && !useBuiltInKeyboard) requestSystemKeyboard()
        if (menuPaused) {
            applyMenuPauseState(true)
        }
    }

    override fun onPause() {
        keyboardRequestGeneration++ // Invalidate delayed show/fallback work while backgrounded.
        composeOwners.handlePause()
        super.onPause()
    }

    override fun onDestroy() {
        keyboardRequestGeneration++
        finishPlayTimeSessionIfNeeded()
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
        if (hasFocus && menuPaused) {
            applyMenuPauseState(true)
        }
        if (hasFocus) {
            attachComposeOverlay()
            hideSystemBars()
            if (nativeKeyboardRequested && !useBuiltInKeyboard) requestSystemKeyboard()
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
        triggerRebirthAfterNativeShutdown(restartIntent)
    }

    @Keep
    fun setStoragePermission() {
        // Kept for the native JNI contract. Storage access is handled exclusively
        // by Android's system document pickers and app-specific directories.
    }

    @Keep
    fun showFileDialog() {
        val pickerIntent = Intent()
            .setType("*/*")
            .setAction(Intent.ACTION_GET_CONTENT)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)

        @Suppress("DEPRECATION")
        startActivityForResult(Intent.createChooser(pickerIntent, getString(R.string.emulator_choose_file)), FILE_DIALOG_CODE)
    }

    @Keep
    fun isStorageManagerEnabled(): Boolean = true

    @Keep
    fun showFolderDialog() {
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

    /** SDL starts text input; Android owns retries, visibility and the fallback UI. */
    @Keep
    fun setKeyboardActive(active: Boolean) {
        runOnUiThread {
            nativeKeyboardRequested = active
            useBuiltInKeyboard = false
            keyboardRequestGeneration++
            if (active) requestSystemKeyboard(resetEditor = true) else hideSystemKeyboard()
        }
    }

    /** Both sceIme and sceImeDialog need a visible editor, even without a game UI. */
    @Keep
    fun updateNativeImeState(
        sceImeActive: Boolean,
        dialogActive: Boolean,
        text: String,
        preeditStart: Int,
        preeditLength: Int,
        caretIndex: Int,
        multiline: Boolean,
        enterLabel: String
    ) {
        val snapshot = NativeImeState(sceImeActive, dialogActive, text, preeditStart, preeditLength, caretIndex, multiline, enterLabel)
        runOnUiThread {
            if (snapshot.active) nativeImeState = snapshot else clearNativeImeState()
        }
    }

    /** Invalidates pending retries so a closed dialog cannot reopen the keyboard. */
    @Keep
    fun clearNativeImeState() {
        runOnUiThread {
            nativeImeState = null
            nativeKeyboardRequested = false
            useBuiltInKeyboard = false
            keyboardRequestGeneration++
            hideSystemKeyboard()
        }
    }

    fun requestSystemKeyboard(resetEditor: Boolean = false) {
        if (!nativeKeyboardRequested || isFinishing || isDestroyed) return
        useBuiltInKeyboard = false
        val generation = ++keyboardRequestGeneration
        val decor = window.decorView
        // Focus and attachment can arrive after SDL's first showSoftInput call.
        listOf(0L, 200L, 650L).forEach { delay ->
            decor.postDelayed({
                if (generation != keyboardRequestGeneration || !nativeKeyboardRequested ||
                    isFinishing || isDestroyed || !hasWindowFocus()) return@postDelayed
                val editor = mTextEdit
                if (editor == null || !editor.isAttachedToWindow ||
                    editor.layoutParams.width <= 0 || editor.layoutParams.height <= 0) {
                    val inputType = InputType.TYPE_CLASS_TEXT or
                        if (nativeImeState?.multiline == true) InputType.TYPE_TEXT_FLAG_MULTI_LINE else 0
                    SDLActivity.showTextInput(inputType, 0, 0, 1, 1)
                } else {
                    editor.setInputType(InputType.TYPE_CLASS_TEXT or
                        if (nativeImeState?.multiline == true) InputType.TYPE_TEXT_FLAG_MULTI_LINE else 0)
                    editor.visibility = View.VISIBLE
                    editor.requestFocus()
                    if (resetEditor && delay == 0L) {
                        (getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)?.restartInput(editor)
                    }
                    WindowInsetsControllerCompat(window, editor).show(WindowInsetsCompat.Type.ime())
                }
            }, delay)
        }
        decor.postDelayed({
            if (generation != keyboardRequestGeneration || !nativeKeyboardRequested ||
                isFinishing || isDestroyed || !hasWindowFocus()) return@postDelayed
            val visible = ViewCompat.getRootWindowInsets(decor)?.isVisible(WindowInsetsCompat.Type.ime()) == true
            if (!visible) showBuiltInKeyboard()
        }, 1_100L)
    }

    fun showBuiltInKeyboard() {
        if (!nativeKeyboardRequested) return
        keyboardRequestGeneration++
        useBuiltInKeyboard = true
        hideSystemKeyboard()
    }

    private fun hideSystemKeyboard() {
        (getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)
            ?.hideSoftInputFromWindow(window.decorView.windowToken, 0)
        WindowInsetsControllerCompat(window, window.decorView).hide(WindowInsetsCompat.Type.ime())
    }

    fun completeNativeIme(cancel: Boolean = false): Boolean {
        val completed = if (cancel) dismissNativeIme() else submitNativeIme()
        if (completed) clearNativeImeState()
        return completed
    }

    override fun onScreenKeyboardSubmit(): Boolean =
        nativeKeyboardRequested && completeNativeIme()

    override fun onScreenKeyboardReturn(): Boolean =
        if (nativeKeyboardRequested && nativeImeState?.multiline == true) editNativeIme("\n", 0)
        else onScreenKeyboardSubmit()

    /** action: 0 insert text, 1 backspace, 2 left, 3 right. */
    external fun editNativeIme(text: String, action: Int): Boolean
    external fun submitNativeIme(): Boolean

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

    /** Push UI-driven audio volume (0..100) into the running emulator state. */
    external fun setAudioVolume(volume: Int)

    /** Apply runtime-safe menu settings to the active Vita3K session. */
    external fun applyRuntimeCoreSettings(
        vSync: Boolean,
        stretchDisplayArea: Boolean,
        disableSurfaceSync: Boolean,
        fpsHack: Boolean,
        frameLimit: Int,
        turboMode: Boolean,
        showCompileShaders: Boolean,
        pstvMode: Boolean
    )

    /**
     * Ask the core to capture and save a screenshot using its configured format.
     * Returns false if no emulator session is currently active.
     */
    external fun requestScreenshot(): Boolean

    /** Pause/resume the active Vita3K app session through the upstream session controller. */
    external fun setAppSessionMenuPaused(paused: Boolean): Boolean

    /** Native IME state used to route the Android keyboard Back action. */
    external fun isNativeImeActive(): Boolean

    /** Requests the Vita IME's Close/Cancel action. */
    external fun dismissNativeIme(): Boolean

    /** Best-effort runtime title for the active Vita3K app session. */
    external fun getRunningGameTitle(): String

    fun setMenuPaused(paused: Boolean) {
        if (menuPaused == paused) return
        menuPaused = paused
        applyMenuPauseState(paused)
        if (!paused) {
            hideSystemBars()
        }
    }

    /**
     * Drives the native pause state for the menu.
     *
     * [pauseNativeThread] and [resumeNativeThread] are not reference counted, so
     * every pause must be matched by exactly one resume. Re-applying a pause that
     * is already active would desynchronise the SDL thread from the core session
     * and leave input dropped after the menu closes.
     */
    private fun applyMenuPauseState(paused: Boolean) {
        if (nativeThreadPaused == paused) return
        nativeThreadPaused = paused
        setAppSessionMenuPaused(paused)
        if (paused) {
            pauseNativeThread()
        } else {
            resumeNativeThread()
        }
    }

    fun exitEmulation() {
        if (exitRequested) return
        exitRequested = true
        finishPlayTimeSessionIfNeeded()
        runOnUiThread {
            overlayBackHandler = null
            overlayMenuButtonRevealHandler = null
            overlayPauseMenuOpenHandler = null
            val homeIntent = Intent(this, MainActivity::class.java).apply {
                action = Intent.ACTION_MAIN
                addCategory(Intent.CATEGORY_LAUNCHER)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            }
            triggerRebirthAfterNativeShutdown(homeIntent)
        }
    }

    fun currentPlayTimeElapsedMs(): Long {
        val startedAt = playTimeSessionStartedAt
        return if (startedAt > 0L) {
            maxOf(0L, System.currentTimeMillis() - startedAt)
        } else {
            0L
        }
    }

    fun updateGamepadRuntimeInputSettings(config: VitaCoreConfig) {
        SDLControllerManager.updateRuntimeInputSettings(config)
    }

    @Deprecated("Deprecated in Java")
    @SuppressLint("GestureBackNavigation")
    override fun onBackPressed() {
        if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return }
        if (overlayBackHandler?.invoke() == true) return
        super.onBackPressed()
    }

    override fun superOnBackPressed() {
        if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return }
        if (overlayBackHandler?.invoke() == true) return
        super.superOnBackPressed()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK &&
            event.action == KeyEvent.ACTION_UP &&
            !event.isCanceled
        ) {
            if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return true }
            if (overlayBackHandler?.invoke() == true) return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onScreenKeyboardFocusLost(): Boolean {
        if (!isNativeImeActive()) return false

        if (completeNativeIme(cancel = true)) {
            SDLActivity.onNativeKeyboardFocusLost()
        } else if (!useBuiltInKeyboard) {
            requestSystemKeyboard()
        }
        // A non-cancelable Vita dialog must keep the Android IME open. Either
        // way, SDL must not independently consume the Back press afterward.
        return true
    }

    override fun onInputDeviceAdded(deviceId: Int) {
        InputDeviceClassifier.invalidateDevice(deviceId)
        refreshPhysicalGamepadState()
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        InputDeviceClassifier.invalidateDevice(deviceId)
        refreshPhysicalGamepadState()
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        InputDeviceClassifier.invalidateDevice(deviceId)
        refreshPhysicalGamepadState()
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
            runCatching {
                startActivity(
                    Intent(
                        Intent.ACTION_VIEW,
                        Uri.parse("https://github.com/sashkinbro/EmuCoreV/releases")
                    )
                )
            }.onFailure { error ->
                Log.e(TAG, "Could not open the releases page", error)
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
            val preferences = AppPreferences(this@Emulator)
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
            setViewTreeLifecycleOwner(composeOwners)
            setViewTreeSavedStateRegistryOwner(composeOwners)
            setViewTreeViewModelStoreOwner(composeOwners)
            elevation = EMULATION_OVERLAY_ELEVATION
            translationZ = EMULATION_OVERLAY_ELEVATION
            setContent {
                val themeMode by preferences.themeModeFlow.collectAsState(initial = preferences.themeMode)
                EmuCoreVTheme(themeMode = themeMode) {
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
        composeView.bringToFront()
        layout.post {
            composeView.bringToFront()
            composeView.elevation = EMULATION_OVERLAY_ELEVATION
            composeView.translationZ = EMULATION_OVERLAY_ELEVATION
        }
    }

    private fun startPlayTimeSessionIfNeeded() {
        val gameId = currentGameIdOrIntent().trim()
        if (gameId.isBlank()) return
        if (playTimeSessionId != null && playTimeSessionTitleId.equals(gameId, ignoreCase = true)) return
        finishPlayTimeSessionIfNeeded()

        val title = InstalledGameRepository().findByTitleId(this, gameId)
            ?.title
            ?.takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
            ?: runCatching { getRunningGameTitle() }
                .getOrDefault("")
                .takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
            ?: gameId

        val session = PlayTimeRepository(this).startSession(gameId, title) ?: return
        playTimeSessionId = session.id
        playTimeSessionTitleId = gameId
        playTimeSessionStartedAt = session.startedAt
    }

    private fun finishPlayTimeSessionIfNeeded() {
        val sessionId = playTimeSessionId ?: return
        PlayTimeRepository(this).finishSession(sessionId)
        playTimeSessionId = null
        playTimeSessionTitleId = ""
        playTimeSessionStartedAt = 0L
    }

    private fun hideSystemBars() {
        setWindowStyle(true)
        ImmersiveMode.apply(window)
    }

    private fun detectPhysicalGamepadConnected(): Boolean {
        return InputDevice.getDeviceIds().any { deviceId ->
            InputDeviceClassifier.isPhysicalGameController(deviceId)
        }
    }

    private fun refreshPhysicalGamepadState() {
        // Do NOT call SDLControllerManager.pollInputDevices() or pollHapticDevices() here.
        // SDL already calls both from its own native thread via JNI (SDL_PollEvent loop).
        // Calling them from the UI thread concurrently causes a race condition on the
        // unsynchronized mJoysticks ArrayList inside SDLJoystickHandler, leading to
        // input glitches and high latency on low-end devices under CPU load.
        hasPhysicalGamepad = detectPhysicalGamepadConnected()
    }

    private fun refreshGamepadRuntimeInputSettings() {
        val gameId = currentGameIdOrIntent()
        val config = if (gameId.isNotBlank()) {
            VitaGameSettingsRepository(this).loadEffective(gameId)
        } else {
            VitaCoreConfigRepository(this).load()
        }
        updateGamepadRuntimeInputSettings(config)
    }

    private fun markRebirthHandled(sourceIntent: Intent): Intent {
        return Intent(sourceIntent).putExtra(EXTRA_REBIRTH_HANDLED, true)
    }

    private fun triggerRebirthAfterNativeShutdown(targetIntent: Intent) {
        performNativeShutdown()
        val sdlThread = SDLActivity.mSDLThread
        if (sdlThread?.isAlive == true) {
            Log.w(TAG, "Proceeding with process rebirth while SDLThread is still alive; shader cache may not be fully flushed")
        }
        ProcessPhoenix.triggerRebirth(applicationContext, targetIntent)
    }

    private fun isLaunchIntent(intent: Intent?): Boolean {
        val action = intent?.action.orEmpty()
        return action == ACTION_EXTERNAL_LAUNCH || action.startsWith("LAUNCH_")
    }

    private fun gameIdFromIntent(intent: Intent?): String {
        if (intent == null) return ""
        intent.getStringExtra(EXTRA_TITLE_ID)?.takeIf { it.isNotBlank() }?.let { return it }
        intent.getStringExtra(EXTRA_GAME_ID)?.takeIf { it.isNotBlank() }?.let { return it }

        val args = intent.getStringArrayExtra(APP_RESTART_PARAMETERS)
        val restartIndex = args?.indexOf("-r") ?: -1
        if (args != null && restartIndex >= 0 && restartIndex + 1 < args.size) {
            return args[restartIndex + 1]
        }

        val action = intent.action.orEmpty()
        if (!action.startsWith("LAUNCH_")) return ""
        val payload = action.removePrefix("LAUNCH_")
        return payload.substringBeforeLast('_', payload)
    }

    private fun gameIdFromArgs(args: Array<String>): String {
        val restartIndex = args.indexOf("-r")
        return if (restartIndex >= 0 && restartIndex + 1 < args.size) args[restartIndex + 1] else ""
    }

    private fun syncEffectiveDriverForLaunch(gameId: String) {
        if (gameId.isBlank()) return
        runCatching { VitaGameSettingsRepository(this).syncEffectiveDriverForLaunch(gameId) }
    }

    private fun isSpecialLaunchAction(action: String): Boolean {
        return action == ACTION_EXTERNAL_LAUNCH ||
            action.startsWith("LAUNCH_") ||
            action.startsWith("INSTALL_FIRMWARE_") ||
            action.startsWith("INSTALL_CONTENT_") ||
            action.startsWith("INSTALL_PKG_")
    }

    private companion object {
        const val TAG = "EmuCoreVEmulator"
        const val ACTION_EXTERNAL_LAUNCH = "com.sbro.emucorev.action.LAUNCH"
        const val APP_RESTART_PARAMETERS = "AppStartParameters"
        const val EXTRA_TITLE_ID = "titleId"
        const val EXTRA_GAME_ID = "gameId"
        const val FILE_DIALOG_CODE = 545
        const val FOLDER_DIALOG_CODE = 546
        const val EXTRA_REBIRTH_HANDLED = "emu_rebirth_handled"
        const val EMULATION_OVERLAY_ELEVATION = 64f
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
