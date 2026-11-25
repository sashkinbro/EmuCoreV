package com.sbro.emucorev.core.vita

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Bundle
import android.os.Looper
import android.os.Handler
import android.os.SystemClock
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.core.app.ActivityOptionsCompat
import androidx.compose.runtime.CompositionLocalProvider
import android.system.Os
import android.text.InputType
import android.content.pm.ActivityInfo
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewGroup
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Toast
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
import com.sbro.emucorev.core.CoreMaintenanceRepository
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.core.KeyboardMouseManager
import com.sbro.emucorev.core.MenuPauseQueue
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.AppAnalytics
import com.sbro.emucorev.core.PlayTimeRepository
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.core.TrophySoundPlayer
import com.sbro.emucorev.core.AUTOMATIC_STATE_LOAD_EXTRA
import com.sbro.emucorev.core.SKIP_AUTOMATIC_STATE_LOAD_EXTRA
import com.sbro.emucorev.core.autoLoadFallbackArguments
import com.sbro.emucorev.core.SaveStateBridge
import com.sbro.emucorev.core.SaveStateRepository
import com.sbro.emucorev.core.SaveStateExitCoordinator
import com.sbro.emucorev.core.resolveAutoLoadArguments
import com.sbro.emucorev.core.shouldSkipExitAutoSave
import com.sbro.emucorev.data.SaveStateAutomationPreferences
import com.sbro.emucorev.BuildConfig
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import com.sbro.emucorev.core.input.InputDeviceClassifier
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLControllerManager
import org.libsdl.app.SDLSurface
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.ProfilePlayTimeSyncer
import com.sbro.emucorev.data.RetroArchShaderSessionController
import com.sbro.emucorev.data.TrophySyncScheduler
import com.sbro.emucorev.core.GameLaunchPreparation
import com.sbro.emucorev.core.GameSessionMarker
import com.sbro.emucorev.core.InputGateWatchdog
import com.sbro.emucorev.core.LaunchGate
import com.sbro.emucorev.core.LabSession
import com.sbro.emucorev.core.LaunchIntents
import com.sbro.emucorev.data.TrophySoundRepository
import com.sbro.emucorev.data.TrophySyncSessionGate
import com.sbro.emucorev.data.drive.DriveBackupWork
import com.sbro.emucorev.discord.DiscordIntegration
import com.sbro.emucorev.ui.common.ImmersiveMode
import com.sbro.emucorev.ui.emulation.EmulationOverlayHost
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import java.io.File

open class Emulator : SDLActivity(), InputManager.InputDeviceListener {
    private var isNativeShellSession = false
    private var isNativeTrophySession = false
    private val isFirmwareSession get() = isNativeShellSession || isNativeTrophySession
    val returnsToNativeLibrary: Boolean get() = NativeLibraryMode.available(this) &&
        (isNativeShellSession || intent.getBooleanExtra(NativeLibraryMode.RETURN_TO_NATIVE_LIBRARY, false))
    private val shellLaunchPending = java.util.concurrent.atomic.AtomicBoolean(false)
    private var shellExitDialog: android.app.Dialog? = null
    private var shellRefreshHz = 60
    private var shellCatalogBootstrap = false
    private val shellBootstrapRestartPending = java.util.concurrent.atomic.AtomicBoolean(false)
    private var shellBootstrapNextCheckAt = 0L
    private var shellBootstrapStableSince = 0L
    private var shellBootstrapDatabaseModified = 0L
    @Volatile private var shellCatalogFingerprint: String? = null
    private var shellCatalogCheckJob: Job? = null
    val rearTouchInput = com.sbro.emucorev.core.input.RearTouchInput()
    // Private backing state so that Compose observes changes when the native
    // core calls setCurrentGameId(). Using a separate computed property avoids
    // a JVM signature clash between the property setter and the JNI callback method.
    private var _currentGameId by mutableStateOf("")
    val currentGameId: String get() = _currentGameId
    private lateinit var surfaceView: EmuSurface
    private lateinit var inputOverlay: InputOverlay
    private var composeOverlayAttached = false
    private var activeTouchSnapshot: MotionEvent? = null
    private var exitRequested = false
    private var rebirthRequested by mutableStateOf(false)
    val exitInProgress: Boolean get() = rebirthRequested
    private var sessionUsable = true
    private val exitScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var retroArchShaderSession: RetroArchShaderSessionController? = null
    private var retroArchShaderApplyJob: Job? = null
    private var retroArchShaderApplyGeneration = 0L
    private val exitCoordinator = SaveStateExitCoordinator()
    private val menuPauseQueue = MenuPauseQueue(exitScope) { paused -> setAppSessionMenuPaused(paused) }
    private var menuPauseCompletion: Deferred<Unit>? = null
    var autoSaveInProgress by mutableStateOf(false)
        private set
    private lateinit var composeOwners: ComposeOwners
    private val composeResultOwner = object : ActivityResultRegistryOwner {
        override val activityResultRegistry = object : ActivityResultRegistry() {
            override fun <I, O> onLaunch(
                requestCode: Int, contract: ActivityResultContract<I, O>, input: I,
                options: ActivityOptionsCompat?
            ) {
                val result = contract.getSynchronousResult(this@Emulator, input)
                if (result != null) {
                    Handler(Looper.getMainLooper()).post { dispatchResult(requestCode, result.value) }
                } else {
                    startActivityForResult(contract.createIntent(this@Emulator, input), requestCode, options?.toBundle())
                }
            }
        }
    }
    private var inputManager: InputManager? = null
    private var overlayBackHandler: (() -> Boolean)? = null
    private var overlayMenuButtonRevealHandler: (() -> Unit)? = null
    private var overlayPauseMenuOpenHandler: (() -> Unit)? = null
    private var menuPaused: Boolean = false
    private var nativeThreadPaused: Boolean = false
    private var playTimeSessionId: String? = null
    private var playTimeSessionTitleId: String = ""
    private var playTimeSessionStartedAt: Long = 0L
    private var playTimeSessionLastSeenAt: Long = 0L
    private var trophySoundPlayer: TrophySoundPlayer? = null
    var launchStateLoading by mutableStateOf(LaunchStateLoadingState())
        private set
    // Input is swallowed while a launch state loads; a load that never reports back must not
    // leave the session permanently unresponsive.
    private val inputGateWatchdog = InputGateWatchdog(clock = SystemClock::elapsedRealtime)
    private var inputGateJob: Job? = null
    private val inputGateBlocking: Boolean get() = launchStateLoading.active || stateFrameWaiting
    var stateFramePendingGeneration by mutableStateOf(0L)
        private set
    var stateFrameReadyGeneration by mutableStateOf(0L)
        private set
    val stateFrameWaiting: Boolean
        get() = stateFramePendingGeneration > stateFrameReadyGeneration
    var nativeImeState by mutableStateOf<NativeImeState?>(null)
        private set
    var nativeKeyboardRequested by mutableStateOf(false)
        private set
    var useBuiltInKeyboard by mutableStateOf(false)
        private set
    private var keyboardRequestGeneration = 0
    private var automaticLoadLogged = false
    private var analyticsRenderer = ""
    private var analyticsAudioBackend = ""

    var hasPhysicalGamepad by mutableStateOf(false)
        private set

    fun getmOverlay(): InputOverlay = inputOverlay

    fun showSaveStateLoadError(reason: String) {
        Log.e("SaveStateBridge", "Launch save state load failed: $reason")
        runOnUiThread {
            sessionUsable = false
            launchStateLoading = launchStateLoading.onFailure()
            clearLoadMarker()
            logAutomaticStateLoadOnce(false)
            Toast.makeText(this, R.string.emulation_savestate_failed_toast, Toast.LENGTH_LONG).show()
            if (intent.getBooleanExtra(AUTOMATIC_STATE_LOAD_EXTRA, false)) {
                // Header inspection cannot detect every corrupt payload. A failed
                // automatic Load gets one normal boot, retaining the original quick file.
                val fallback = Intent(this, Emulator::class.java).apply {
                    action = "LAUNCH_${currentGameIdOrIntent()}_${System.currentTimeMillis()}"
                    putExtra(APP_RESTART_PARAMETERS, autoLoadFallbackArguments(
                        intent.getStringArrayExtra(APP_RESTART_PARAMETERS).orEmpty()))
                    putExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, true)
                    putExtra(NativeLibraryMode.RETURN_TO_NATIVE_LIBRARY, returnsToNativeLibrary)
                }
                triggerRebirthAfterNativeShutdown(fallback, allowAutoSave = false)
            }
        }
    }

    private fun logAutomaticStateLoadOnce(success: Boolean) {
        if (automaticLoadLogged || !intent.getBooleanExtra(AUTOMATIC_STATE_LOAD_EXTRA, false)) return
        automaticLoadLogged = true
        AppAnalytics.logSaveStateAction("load", true, success)
    }

    fun onLaunchStateProgress(progress: Float, active: Boolean) {
        runOnUiThread {
            inputGateWatchdog.progress()
            if (!active && stateFrameWaiting) return@runOnUiThread
            launchStateLoading = launchStateLoading.onNativeProgress(progress, active)
            clearLoadMarkerIfSettled()
        }
    }

    fun onStateFramePending(generation: Long) {
        runOnUiThread {
            Log.i(TAG, "state frame pending generation=$generation ready=$stateFrameReadyGeneration")
            if (generation > stateFrameReadyGeneration)
                stateFramePendingGeneration = maxOf(stateFramePendingGeneration, generation)
            inputGateWatchdog.progress()
            startInputGateWatchdog()
        }
    }

    fun onStateFrameReady(generation: Long) {
        LabSession.onStateFrameReady(this)
        runOnUiThread {
            Log.i(TAG, "state frame ready generation=$generation pending=$stateFramePendingGeneration")
            if (generation < stateFramePendingGeneration) return@runOnUiThread
            stateFrameReadyGeneration = maxOf(stateFrameReadyGeneration, generation)
            launchStateLoading = launchStateLoading.onNativeProgress(1f, false)
            logAutomaticStateLoadOnce(true)
            clearLoadMarkerIfSettled()
        }
    }

    private fun clearLoadMarkerIfSettled() {
        if (!inputGateBlocking) clearLoadMarker()
    }

    /** The boot-time state load is over (done or failed): it no longer counts as a possible crash cause. */
    private fun clearLoadMarker() {
        if (isFirmwareSession) return
        val title = currentGameIdOrIntent()
        if (title.isBlank()) return
        exitScope.launch(Dispatchers.IO) { SaveStateRepository(this@Emulator).finishLoad(title) }
    }

    private fun startInputGateWatchdog() {
        if (isFirmwareSession || inputGateJob?.isActive == true) return
        inputGateJob = exitScope.launch {
            while (inputGateBlocking) {
                if (inputGateWatchdog.check(true)) {
                    onInputGateTimeout()
                    break
                }
                delay(INPUT_GATE_POLL_MS)
            }
            inputGateWatchdog.check(false)
        }
    }

    /**
     * No progress for [InputGateWatchdog.TIMEOUT_MS] while resumed. The load itself is not judged: a native
     * failure is reported through [showSaveStateLoadError]. Only the UI gate is opened; auto-save stays enabled
     * and the load marker is left to the normal completion path.
     */
    private fun onInputGateTimeout() {
        Log.e(TAG, "Input gate stayed closed for ${InputGateWatchdog.TIMEOUT_MS} ms without progress " +
            "(loading=${launchStateLoading.active} frameWaiting=$stateFrameWaiting); opening it")
        stateFrameReadyGeneration = maxOf(stateFrameReadyGeneration, stateFramePendingGeneration)
        launchStateLoading = launchStateLoading.onFailure()
    }

    fun setCurrentGameId(gameId: String) {
        _currentGameId = gameId
        refreshGamepadRuntimeInputSettings()
        startPlayTimeSessionIfNeeded()
        requestRetroArchShaderReapply(gameId)
    }

    fun playTrophyUnlockSound() {
        runOnUiThread {
            if (rebirthRequested || isFinishing) return@runOnUiThread
            val repository = TrophySoundRepository(applicationContext)
            val player = trophySoundPlayer ?: TrophySoundPlayer(
                context = applicationContext,
                onCustomSoundFailure = { uri ->
                    repository.markCustomUnavailable(uri)
                    Log.w("TrophySound", "Custom trophy sound became unavailable; using system notification sound")
                }
            ).also { trophySoundPlayer = it }
            player.play(repository.current())
        }
    }

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
        if (isFirmwareSession) return
        runOnUiThread {
            overlayMenuButtonRevealHandler?.invoke()
        }
    }

    fun openPauseMenuFromController() {
        if (isFirmwareSession) return
        runOnUiThread {
            overlayPauseMenuOpenHandler?.invoke()
        }
    }

    override fun getLibraries(): Array<String> = arrayOf("ECV")

    private fun isFrontCameraProbe(): Boolean = BuildConfig.DEBUG &&
        intent.getBooleanExtra("ECV_FRONT_CAMERA_PROBE", false)

    override fun getMainFunction(): String =
        if (isFrontCameraProbe()) "ECV_FrontCameraProbe" else super.getMainFunction()

    override fun shouldForceFullscreen(): Boolean = true

    override fun createSDLSurface(context: Context): SDLSurface {
        if (!::inputOverlay.isInitialized) {
            inputOverlay = InputOverlay(this)
        }
        surfaceView = EmuSurface(context)
        return surfaceView
    }

    override fun main() {
        if (isFirmwareSession) {
            com.sbro.emucorev.core.NativeLibraryLoader.ensureLoaded(this)
            // Loader pre-initialization uses the application context. Native
            // firmware callbacks must resolve this process's actual Activity.
            org.libsdl.app.SDL.setContext(this)
            if (isNativeShellSession) {
                val fingerprint = NativeLibraryCatalog.fingerprint(this)
                val sandbox = NativeLibraryMode.prepare(this)
                shellCatalogFingerprint = fingerprint
                check(prepareNativeLibraryRuntime(sandbox.absolutePath, shellRefreshHz))
            }
            super.main()
            return
        }
        if (isFrontCameraProbe()) {
            super.main()
            return
        }
        runBlocking { TrophySyncSessionGate.shared.gameStarting() }
        try {
            // SDL invokes this on SDLThread before nativeRunMain, including old pinned shortcuts.
            // Every launch, whichever process requested it, is prepared here: failed preparation
            // must abort the native entry point rather than boot with stale state.
            if (!prepareLaunch()) return
            // Native callbacks find this Activity through SDL's context (see NativeLibraryLoader).
            org.libsdl.app.SDL.setContext(this)
            super.main()
        } finally {
            TrophySyncSessionGate.shared.gameStopped()
        }
    }

    /**
     * Runs on SDLThread in the Emulator process. Returns false after reporting the error and
     * closing this Activity (back to the caller); the launch gate is always released.
     */
    private fun prepareLaunch(): Boolean {
        val gate = LaunchGate.forContext(this)
        try {
            if (launchRequestMalformed(intent)) {
                Log.w(TAG, "Rejected launch with a malformed title id")
                return abortLaunch(R.string.game_launch_failed)
            }
            val args = intent.getStringArrayExtra(APP_RESTART_PARAMETERS) ?: gameIdFromIntent(intent).let {
                if (it.isBlank()) emptyArray() else arrayOf("-r", it)
            }
            val gameId = gameIdFromArgs(args)
            if (gameId.isNotBlank()) LabSession.accepted(gameId, args)
            val repository = SaveStateRepository(this)
            // Before any save or load of this process: drop temp files of interrupted ones.
            runCatching { repository.sweepOrphans() }
                .onFailure { Log.w(TAG, "Orphan sweep failed", it) }
            if (gameId.isBlank()) {
                // Installer restarts carry no title; they only need the core maintenance.
                try {
                    CoreMaintenanceRepository(this).ensureCoreUpdated()
                } catch (error: Exception) {
                    Log.e(TAG, "Core maintenance failed before external launch", error)
                    return abortLaunch(R.string.settings_clear_cache_failed)
                }
                return true
            }
            when (GameLaunchPreparation.prepare(this, gameId)) {
                is GameLaunchPreparation.Outcome.Ready -> Unit
                GameLaunchPreparation.Outcome.MissingFirmware -> {
                    AppAnalytics.logEmulationStartFailed("game", "bios_missing")
                    return abortLaunch(R.string.game_launch_requires_firmware)
                }
                GameLaunchPreparation.Outcome.MissingFirmwareUpdate -> {
                    AppAnalytics.logEmulationStartFailed("game", "bios_missing")
                    return abortLaunch(R.string.game_launch_requires_firmware_update)
                }
                GameLaunchPreparation.Outcome.Failed -> {
                    AppAnalytics.logEmulationStartFailed("game", "unknown")
                    return abortLaunch(R.string.settings_clear_cache_failed)
                }
            }
            val explicitState = loadStatePath(args)
            if (explicitState != null) {
                // An explicit resume (for example from the native shell) names a raw path.
                // Validate it here, where the engine that will load it lives.
                if (!LabSession.active && repository.interruptedLoad(gameId) == explicitState) {
                    Log.e(TAG, "Previous load of $explicitState never finished; not retrying it")
                    repository.finishLoad(gameId)
                    return abortLaunch(R.string.emulation_savestate_failed_toast)
                }
                if (!repository.validStatePath(gameId, explicitState)) {
                    Log.e(TAG, "Rejected save state for $gameId: $explicitState")
                    return abortLaunch(R.string.emulation_savestate_failed_toast)
                }
            }
            return true
        } finally {
            gate.release()
        }
    }

    private fun abortLaunch(message: Int): Boolean {
        runOnUiThread {
            Toast.makeText(this, message, Toast.LENGTH_LONG).show()
            finishAndRemoveTask()
        }
        return false
    }

    private fun loadStatePath(args: Array<String>): String? {
        val index = args.indexOf("-loadstate")
        return args.getOrNull(index + 1)?.takeIf { index >= 0 && it.isNotBlank() }
    }

    override fun getArguments(): Array<String> {
        if (isFrontCameraProbe()) return emptyArray()
        if (launchRequestMalformed(intent)) return emptyArray()
        val baseArgs = intent.getStringArrayExtra(APP_RESTART_PARAMETERS) ?: gameIdFromIntent(intent).let {
            if (it.isBlank()) emptyArray() else arrayOf("-r", it)
        }
        val gameId = gameIdFromArgs(baseArgs)
        if (isFirmwareSession) return baseArgs
        // SDL requests these arguments on SDLThread after loading the core (prepareLaunch has
        // already synced the driver). Auto-load is decided here, from this process's fresh
        // preferences, whichever process asked for the launch.
        val repository = SaveStateRepository(this)
        val enabled = SaveStateAutomationPreferences(this, gameId).use { it.current.autoLoadOnStart }
        val interrupted = repository.interruptedLoad(gameId)
        val args = resolveAutoLoadArguments(baseArgs, enabled,
            skipAutomatic = intent.getBooleanExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, false) || LabSession.active) {
            val quick = repository.validQuickStatePath(gameId)
            if (quick != null && quick == interrupted) {
                // The last automatic load of this file killed the process: boot normally once.
                Log.e(TAG, "Previous automatic load of $quick never finished; skipping it")
                repository.finishLoad(gameId)
                runOnUiThread { Toast.makeText(this, R.string.emulation_savestate_failed_toast, Toast.LENGTH_LONG).show() }
                null
            } else quick
        }
        if (args !== baseArgs) {
            intent.putExtra(APP_RESTART_PARAMETERS, args).putExtra(AUTOMATIC_STATE_LOAD_EXTRA, true)
            runOnUiThread { launchStateLoading = LaunchStateLoadingState.fromArguments(args) }
        }
        loadStatePath(args)?.let { path ->
            repository.beginLoad(gameId, path)
            runOnUiThread { startInputGateWatchdog() }
        }
        Log.i(TAG, "Starting emulator with args=${args.joinToString(" ")} gameId=$gameId")
        return args
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (isNativeShellSession && gameIdFromIntent(intent) == NativeLibraryMode.TITLE_ID) return
        val outgoingTitle = currentGameIdOrIntent()
        setIntent(intent)
        if (isLaunchIntent(intent)) {
            triggerRebirthAfterNativeShutdown(markRebirthHandled(intent), outgoingTitle,
                allowAutoSave = !isNativeShellSession && !LabSession.isLabIntent(intent) && !shouldSkipExitAutoSave(intent.getStringArrayExtra(APP_RESTART_PARAMETERS),
                    intent.getBooleanExtra(AUTOMATIC_STATE_LOAD_EXTRA, false)))
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.i(TAG, "onCreate action=${intent?.action} flags=0x${Integer.toHexString(intent?.flags ?: 0)} " +
            "restored=${savedInstanceState != null}")
        // A task the system brings back from recents or after process death carries the original
        // launch Intent; it must not replay an old explicit state load.
        intent?.let { LaunchIntents.sanitizeRestoredLaunch(it, restoredByFramework = savedInstanceState != null) }
        LabSession.begin(intent, restored = savedInstanceState != null)
        isNativeShellSession = gameIdFromIntent(intent) == NativeLibraryMode.TITLE_ID
        isNativeTrophySession = gameIdFromIntent(intent) == "__VITA_NATIVE_TROPHIES__"
        shellCatalogBootstrap = isNativeShellSession && NativeLibraryMode.needsCatalogBootstrap(this)
        launchStateLoading = LaunchStateLoadingState.fromArguments(
            intent?.getStringArrayExtra(APP_RESTART_PARAMETERS)
        )
        if (launchStateLoading.active) startInputGateWatchdog()
        if (!::inputOverlay.isInitialized) {
            inputOverlay = InputOverlay(this)
        }
        super.onCreate(savedInstanceState)
        if (isFinishing) return
        if (shellCatalogBootstrap) {
            surfaceView.alpha = 0f
            mLayout?.addView(android.widget.TextView(this).apply {
                text = "Vita\n${getString(R.string.common_loading)}"
                textSize = 24f
                gravity = android.view.Gravity.CENTER
                setTextColor(android.graphics.Color.WHITE)
                setBackgroundColor(0xff0b294d.toInt())
            }, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        }
        if (isNativeShellSession) {
            @Suppress("DEPRECATION")
            val display = windowManager.defaultDisplay
            val current = display.mode
            val mode = display.supportedModes.filter {
                it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight &&
                    (kotlin.math.abs(it.refreshRate - 60f) < 1f ||
                        kotlin.math.abs(it.refreshRate - 90f) < 1f || kotlin.math.abs(it.refreshRate - 120f) < 1f)
            }.maxByOrNull { it.refreshRate }
            shellRefreshHz = when {
                (mode?.refreshRate ?: current.refreshRate) >= 119f -> 120
                (mode?.refreshRate ?: current.refreshRate) >= 89f -> 90
                else -> 60
            }
            if (mode != null) window.attributes = window.attributes.apply { preferredDisplayModeId = mode.modeId }
        }
        if (isFirmwareSession) inputOverlay.setTouchControlsActive(false)
        else GameSessionMarker.forContext(this).markLive(android.os.Process.myPid(), android.app.Application.getProcessName())
        KeyboardMouseManager.ensureInitialized(this)
        KeyboardMouseManager.setPointerCaptureListener { enabled ->
            runOnUiThread { updatePointerCapture(enabled) }
        }
        window.decorView.setOnCapturedPointerListener { _, event ->
            val rear = rearTouchInput.motion(event, window.decorView.width, window.decorView.height)
            if (rear.consumed) true else {
                try {
                    val mouseEvent = if (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE) event else rear.event ?: event
                    KeyboardMouseManager.handleCapturedPointerEvent(mouseEvent)
                } finally { rear.event?.recycle() }
            }
        }
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        // Application startup recovers stale play-time sessions off the main thread; a new session
        // must not start before that finished or it would be closed as stale.
        com.sbro.emucorev.core.AppStartup.awaitReady()
        EmulatorStorage.prepareRuntime(this)
        VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        refreshGamepadRuntimeInputSettings()
        startPlayTimeSessionIfNeeded()
        requestRetroArchShaderReapply(currentGameIdOrIntent())
        composeOwners = ComposeOwners().also { it.performCreate(savedInstanceState) }
        inputManager = getSystemService(InputManager::class.java)
        refreshPhysicalGamepadState()
        inputManager?.registerInputDeviceListener(this, null)
        attachComposeOverlay()
        hideSystemBars()
    }

    override fun onResume() {
        super.onResume()
        inputGateWatchdog.setPaused(false)
        if (rebirthRequested || isFinishing) return
        if (isNativeShellSession && shellLaunchPending.getAndSet(false)) {
            exitScope.launch { withContext(Dispatchers.IO) {
                notifyNativeLibraryGameStopped()
                setAppSessionMenuPaused(false)
            } }
        }
        if (isNativeShellSession && shellCatalogFingerprint != null) {
            shellCatalogCheckJob?.cancel()
            shellCatalogCheckJob = exitScope.launch {
                val current = withContext(Dispatchers.IO) { NativeLibraryCatalog.fingerprint(this@Emulator) }
                if (current != shellCatalogFingerprint && !rebirthRequested && !isFinishing) {
                    triggerRebirthAfterNativeShutdown(NativeLibraryMode.emulatedIntent(this@Emulator), allowAutoSave = false,
                        returnToLibrary = true)
                }
            }
        }
        requestRetroArchShaderReapply(currentGameIdOrIntent())
        setPeripheralsSuspended(false)
        KeyboardMouseManager.refreshConnectionState()
        KeyboardMouseManager.refreshPointerCapture()
        DiscordIntegration.setPaused(false)
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
        inputGateWatchdog.setPaused(true)
        shellCatalogCheckJob?.cancel()
        rearTouchInput.setActive(false)
        setPeripheralsSuspended(true)
        KeyboardMouseManager.releaseAll()
        updatePointerCapture(false)
        cancelActiveTouches()
        keyboardRequestGeneration++ // Invalidate delayed show/fallback work while backgrounded.
        DiscordIntegration.setPaused(true)
        composeOwners.handlePause()
        super.onPause()
    }

    override fun onStart() {
        super.onStart()
        startPlayTimeSessionIfNeeded()
    }

    override fun onStop() {
        super.onStop()
        finishPlayTimeSessionIfNeeded()
    }

    override fun quitAfterShutdown() {
        if (isFrontCameraProbe() || isFirmwareSession) {
            super.quitAfterShutdown()
            return
        }
        try {
            super.quitAfterShutdown()
        } finally {
            stopRetroArchShaderSession()
            TrophySyncSessionGate.shared.gameStopped()
            // The exit path (rebirth or process kill) must not wait on the network: WorkManager
            // runs the sync with its own network constraint and survives this process.
            runCatching { TrophySyncScheduler.schedule(applicationContext) }
        }
    }

    override fun onDestroy() {
        LabSession.onDestroy()
        shellExitDialog?.dismiss()
        shellExitDialog = null
        KeyboardMouseManager.setEmulationInputEnabled(false)
        KeyboardMouseManager.setPointerCaptureListener(null)
        KeyboardMouseManager.releaseAll()
        menuPauseQueue.close()
        cancelActiveTouches()
        keyboardRequestGeneration++
        finishPlayTimeSessionIfNeeded()
        stopRetroArchShaderSession()
        inputGateJob?.cancel()
        if (!isFirmwareSession) GameSessionMarker.forContext(this).clear(android.os.Process.myPid())
        inputManager?.unregisterInputDeviceListener(this)
        inputManager = null
        if (::inputOverlay.isInitialized) {
            inputOverlay.dispose()
        }
        if (::composeOwners.isInitialized) {
            composeOwners.handleDestroy()
        }
        trophySoundPlayer?.close()
        trophySoundPlayer = null
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        if (!hasFocus) {
            cancelActiveTouches()
            KeyboardMouseManager.releaseAll()
            updatePointerCapture(false)
        }
        super.onWindowFocusChanged(hasFocus)
        if (rebirthRequested || isFinishing) return
        if (hasFocus) {
            KeyboardMouseManager.refreshConnectionState()
            KeyboardMouseManager.refreshPointerCapture()
        }
        if (hasFocus && menuPaused) {
            applyMenuPauseState(true)
        }
        if (hasFocus) {
            attachComposeOverlay()
            hideSystemBars()
            if (nativeKeyboardRequested && !useBuiltInKeyboard) requestSystemKeyboard()
        }
    }

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

    /** Native library requests use the same driver/settings/automatic-state resolver as the library UI. */
    private external fun prepareNativeLibraryRuntime(sandbox: String, refreshHz: Int): Boolean
    private external fun notifyNativeLibraryGameStopped()

    /** The firmware creates its schema on first boot. Filter it only after shutdown. */
    fun onNativeLibraryCatalogReady(): Boolean {
        if (!shellCatalogBootstrap) return true
        if (shellBootstrapRestartPending.get()) return false
        val now = android.os.SystemClock.uptimeMillis()
        if (now < shellBootstrapNextCheckAt) return false
        shellBootstrapNextCheckAt = now + 500
        val database = File(cacheDir, "native-shell-runtime/vita/ur0/shell/db/app.db")
        if (!database.isFile || File(database.path + "-journal").exists()) return false
        val ready = runCatching {
            android.database.sqlite.SQLiteDatabase.openDatabase(database.path, null,
                android.database.sqlite.SQLiteDatabase.OPEN_READONLY).use { db ->
                db.rawQuery("SELECT 1 FROM tbl_appinfo_icon WHERE titleId='NPXS10008' LIMIT 1", null).use { it.moveToFirst() }
            }
        }.getOrDefault(false)
        val modified = database.lastModified()
        if (!ready || modified != shellBootstrapDatabaseModified) {
            shellBootstrapDatabaseModified = modified
            shellBootstrapStableSince = now
            return false
        }
        if (now - shellBootstrapStableSince >= 1_000 && shellBootstrapRestartPending.compareAndSet(false, true)) {
            triggerRebirthAfterNativeShutdown(NativeLibraryMode.emulatedIntent(this), allowAutoSave = false,
                returnToLibrary = true)
        }
        return false
    }

    fun onNativeLibraryLaunchRequested(titleId: String): Boolean {
        if (!isNativeShellSession || rebirthRequested || !titleId.matches(Regex("[A-Za-z0-9_]{1,64}"))) return false
        if (!shellLaunchPending.compareAndSet(false, true)) return false
        runOnUiThread {
            exitScope.launch {
                var launched = false
                try {
                    check(withContext(Dispatchers.IO) {
                        (titleId == "NPXS10008" || InstalledGameRepository().findByTitleId(this@Emulator, titleId) != null) &&
                            setAppSessionMenuPaused(true)
                    })
                    launched = if (titleId == "NPXS10008") {
                        startActivity(Intent(this@Emulator, NativeLibraryTrophiesActivity::class.java).apply {
                            putExtra(APP_RESTART_PARAMETERS, arrayOf("-r", "__VITA_NATIVE_TROPHIES__"))
                            putExtra(NativeLibraryMode.RETURN_TO_NATIVE_LIBRARY, true)
                        })
                        true
                    } else com.sbro.emucorev.core.VitaLaunchBridge.launchInstalledTitle(this@Emulator, titleId) ==
                        com.sbro.emucorev.core.VitaLaunchBridge.LaunchResult.Success
                    check(launched)
                } catch (error: Exception) {
                    Log.e(TAG, "Native library could not hand off installed title $titleId", error)
                    Toast.makeText(this@Emulator, R.string.game_launch_failed, Toast.LENGTH_LONG).show()
                } finally {
                    if (!launched) {
                        withContext(Dispatchers.IO) {
                            notifyNativeLibraryGameStopped()
                            setAppSessionMenuPaused(false)
                        }
                        shellLaunchPending.set(false)
                    }
                }
            }
        }
        return true
    }

    fun setStoragePermission() {
        // Kept for the native JNI contract. Storage access is handled exclusively
        // by Android's system document pickers and app-specific directories.
    }

    fun showFileDialog() {
        val pickerIntent = Intent()
            .setType("*/*")
            .setAction(Intent.ACTION_GET_CONTENT)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)

        @Suppress("DEPRECATION")
        startActivityForResult(Intent.createChooser(pickerIntent, getString(R.string.emulator_choose_file)), FILE_DIALOG_CODE)
    }

    fun isStorageManagerEnabled(): Boolean = true

    fun showFolderDialog() {
        val pickerIntent = Intent()
            .setAction(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)

        @Suppress("DEPRECATION")
        startActivityForResult(Intent.createChooser(pickerIntent, getString(R.string.emulator_choose_folder)), FOLDER_DIALOG_CODE)
    }

    private var cheatImportHandler: ((Uri) -> Unit)? = null

    fun setCheatImportHandler(handler: ((Uri) -> Unit)?) {
        cheatImportHandler = handler
    }

    fun requestCheatImport() {
        val pickerIntent = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")

        @Suppress("DEPRECATION")
        startActivityForResult(
            Intent.createChooser(pickerIntent, getString(R.string.emulation_cheats_import)),
            CHEAT_IMPORT_CODE
        )
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

    private fun showShellExitDialog() {
        if (isFinishing || shellExitDialog?.isShowing == true) return
        shellExitDialog = NativeLibraryExitDialog(this) { exitEmulation(autoSave = false) }
            .also { dialog ->
                dialog.setOnDismissListener { shellExitDialog = null; hideSystemBars() }
                dialog.show()
            }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (composeResultOwner.activityResultRegistry.dispatchResult(requestCode, resultCode, data)) return
        super.onActivityResult(requestCode, resultCode, data)
        when (requestCode) {
            CHEAT_IMPORT_CODE -> {
                if (resultCode == RESULT_OK) {
                    data?.data?.let { uri -> cheatImportHandler?.invoke(uri) }
                }
            }
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
    fun setKeyboardActive(active: Boolean) {
        runOnUiThread {
            if (rebirthRequested) return@runOnUiThread
            nativeKeyboardRequested = active
            useBuiltInKeyboard = false
            keyboardRequestGeneration++
            if (active) requestSystemKeyboard(resetEditor = true) else hideSystemKeyboard()
        }
    }

    /** Both sceIme and sceImeDialog need a visible editor, even without a game UI. */
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
            if (rebirthRequested) return@runOnUiThread
            if (snapshot.active) nativeImeState = snapshot else clearNativeImeState()
        }
    }

    /** Invalidates pending retries so a closed dialog cannot reopen the keyboard. */
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
        if (rebirthRequested || !nativeKeyboardRequested || isFinishing || isDestroyed) return
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

    fun setControllerOverlayScale(scale: Float) {
        runOnUiThread {
            getmOverlay().setScale(scale)
        }
    }

    fun setControllerOverlayOpacity(opacity: Int) {
        runOnUiThread {
            getmOverlay().setOpacity(opacity)
        }
    }


    /** Push UI-driven audio volume (0..100) into the running emulator state. */
    external fun setAudioVolume(volume: Int)

    private val recordAudioGate = RecordAudioPermissionGate()

    /** Called from the guest thread (sceAudioInOpenPort): cheap, never blocks. */
    @Suppress("unused") // JNI
    fun hasRecordAudioPermission(): Boolean =
        androidx.core.content.ContextCompat.checkSelfPermission(
            this, android.Manifest.permission.RECORD_AUDIO
        ) == android.content.pm.PackageManager.PERMISSION_GRANTED

    /**
     * Called from the guest thread. Shows the runtime prompt once per session; the prompt is
     * issued on the UI thread and the call returns immediately. The result arrives in
     * [onRequestPermissionsResult].
     */
    @Suppress("unused") // JNI
    fun requestRecordAudioPermission() {
        if (!recordAudioGate.shouldRequest(hasRecordAudioPermission())) return
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            androidx.core.app.ActivityCompat.requestPermissions(
                this,
                arrayOf(android.Manifest.permission.RECORD_AUDIO),
                RecordAudioPermissionGate.REQUEST_CODE
            )
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        // SDLActivity forwards every result to SDL's own pending-permission callbacks.
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val granted = grantResults.isNotEmpty() &&
            grantResults[0] == android.content.pm.PackageManager.PERMISSION_GRANTED
        if (recordAudioGate.shouldReopenCapture(requestCode, granted)) {
            notifyRecordAudioGranted()
        }
    }

    private external fun notifyRecordAudioGranted()

    /** Apply runtime-safe menu settings to the active ECV session. */
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

    /** Largest internal resolution multiplier the running session can switch to live; 0 if it cannot. */
    external fun getLiveResolutionLimit(): Float

    /** Internal resolution multiplier the running session renders at right now; 0 if none is running. */
    external fun getLiveResolutionMultiplier(): Float

    /**
     * Switch the internal resolution of the running, menu-paused session. Blocks until the render
     * thread has applied it (or refused); true only if it is in effect afterwards.
     */
    external fun setLiveResolutionMultiplier(multiplier: Float): Boolean

    /** Apply a screen filter name (as stored in the profile) to the running session. */
    external fun setLiveScreenFilter(filter: String)

    /**
     * Ask the core to capture and save a screenshot using its configured format. Blocking
     * (frame readback and encode): never call it on the UI thread, use [ScreenshotPublisher].
     * Returns the written file path, or null when nothing was saved (no session, screenshots
     * disabled, readback or encode failure).
     */
    external fun requestScreenshot(): String?

    /** Pause/resume the active ECV app session through the upstream session controller. */
    external fun setAppSessionMenuPaused(paused: Boolean): Boolean

    /** Native IME state used to route the Android keyboard Back action. */
    external fun isNativeImeActive(): Boolean

    /** Requests the Vita IME's Close/Cancel action. */
    external fun dismissNativeIme(): Boolean

    /** Best-effort runtime title for the active ECV app session. */
    external fun getRunningGameTitle(): String

    fun setMenuPaused(paused: Boolean) {
        if (menuPaused == paused) return
        menuPaused = paused
        applyMenuPauseState(paused)
        if (!paused) {
            hideSystemBars()
        }
    }

    suspend fun setMenuPausedAndAwait(paused: Boolean) {
        setMenuPaused(paused)
        menuPauseCompletion?.await()
    }

    /**
     * Pause the guest session without sending Android background/foreground events to SDL.
     * SDL lifecycle changes belong to Activity lifecycle callbacks, not an in-game panel.
     * Kernel and audio locks are acquired by the ordered worker, never the input thread.
     */
    private fun applyMenuPauseState(paused: Boolean) {
        if (nativeThreadPaused == paused) return
        nativeThreadPaused = paused
        DiscordIntegration.setPaused(paused)
        menuPauseCompletion = menuPauseQueue.request(paused)
    }

    fun exitEmulation(autoSave: Boolean = true) {
        if (exitRequested) return
        exitRequested = true
        finishPlayTimeSessionIfNeeded()
        runOnUiThread {
            overlayBackHandler = null
            overlayMenuButtonRevealHandler = null
            overlayPauseMenuOpenHandler = null
            val returnToNative = !isNativeShellSession && returnsToNativeLibrary
            val homeIntent = if (returnToNative) NativeLibraryMode.intent(this) else Intent(this, MainActivity::class.java).apply {
                action = Intent.ACTION_MAIN
                addCategory(Intent.CATEGORY_LAUNCHER)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP)
                if (isNativeShellSession) putExtra(NativeLibraryMode.EXIT_NATIVE_LIBRARY, true)
            }
            triggerRebirthAfterNativeShutdown(homeIntent, allowAutoSave = autoSave && !isFirmwareSession,
                returnToLibrary = true)
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

    fun heartbeatPlayTimeSession() {
        val sessionId = playTimeSessionId ?: return
        val now = System.currentTimeMillis()
        if (now - playTimeSessionLastSeenAt < PLAY_TIME_HEARTBEAT_INTERVAL_MS) return
        playTimeSessionLastSeenAt = now
        PlayTimeRepository(this).touchSession(sessionId, now)
    }

    fun updateGamepadRuntimeInputSettings(config: VitaCoreConfig) {
        SDLControllerManager.updateRuntimeInputSettings(config)
        applyMicrophoneSettings(config)
    }

    /**
     * Menu pause / background: releases the host camera and microphone (the OS privacy indicators go off)
     * and lets the guest threads re-open them lazily after the resume. Safe before the session exists.
     */
    fun setPeripheralsSuspended(suspended: Boolean) {
        com.sbro.emucorev.core.input.RearTouchNative.setCamerasSuspended(suspended)
        runCatching { setMicrophoneSuspended(suspended) }
        if (suspended) com.sbro.emucorev.core.MicrophoneSupport.clearRouting(this)
    }

    @Volatile
    private var micRouteDeviceId = 0

    /** Pushes the effective (global or per-game) microphone settings into the core. */
    private fun applyMicrophoneSettings(config: VitaCoreConfig) {
        val devices = com.sbro.emucorev.core.MicrophoneSupport.listInputDevices(this)
        val deviceId = com.sbro.emucorev.core.resolveMicDeviceId(config.micInputDevice, devices)
        micRouteDeviceId = deviceId
        runCatching { setMicrophoneSettings(config.micEnabled, config.micGainPercent, deviceId) }
            .onFailure { Log.w(TAG, "Microphone settings not applied yet", it) }
    }

    /** JNI (guest thread, just before the capture stream opens): route a Bluetooth headset microphone. */
    @Suppress("unused")
    fun prepareMicrophoneRouting() {
        val id = micRouteDeviceId
        com.sbro.emucorev.core.MicrophoneSupport.applyRouting(
            this, if (id > 0) com.sbro.emucorev.core.MicrophoneSupport.deviceInfo(this, id) else null
        )
    }

    /** JNI (guest thread): the capture port was released. */
    @Suppress("unused")
    fun releaseMicrophoneRouting() {
        com.sbro.emucorev.core.MicrophoneSupport.clearRouting(this)
    }

    private external fun setMicrophoneSettings(enabled: Boolean, gainPercent: Int, deviceId: Int)

    private external fun setMicrophoneSuspended(suspended: Boolean)

    @Deprecated("Deprecated in Java")
    @SuppressLint("GestureBackNavigation")
    override fun onBackPressed() {
        if (isNativeTrophySession) { exitEmulation(autoSave = false); return }
        if (isNativeShellSession) { showShellExitDialog(); return }
        if (launchStateLoading.active || stateFrameWaiting) return
        if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return }
        if (overlayBackHandler?.invoke() == true) return
        super.onBackPressed()
    }

    override fun superOnBackPressed() {
        if (isNativeTrophySession) { exitEmulation(autoSave = false); return }
        if (isNativeShellSession) { showShellExitDialog(); return }
        if (launchStateLoading.active || stateFrameWaiting) return
        if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return }
        if (overlayBackHandler?.invoke() == true) return
        super.superOnBackPressed()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (isNativeTrophySession && event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) exitEmulation(autoSave = false)
            return true
        }
        if (isNativeShellSession && event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) showShellExitDialog()
            return true
        }
        if (rebirthRequested) return true
        if (launchStateLoading.active || stateFrameWaiting) {
            return when (event.keyCode) {
                KeyEvent.KEYCODE_VOLUME_UP,
                KeyEvent.KEYCODE_VOLUME_DOWN,
                KeyEvent.KEYCODE_VOLUME_MUTE,
                KeyEvent.KEYCODE_POWER -> super.dispatchKeyEvent(event)
                else -> true
            }
        }
        if (rearTouchInput.key(event)) return true
        if (KeyboardMouseManager.handleKeyBindingCapture(event)) return true
        if (!nativeKeyboardRequested && KeyboardMouseManager.handleKeyEvent(event)) return true
        if (event.keyCode == KeyEvent.KEYCODE_BACK &&
            event.action == KeyEvent.ACTION_UP &&
            !event.isCanceled
        ) {
            if (nativeKeyboardRequested) { onScreenKeyboardFocusLost(); return true }
            if (overlayBackHandler?.invoke() == true) return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (launchStateLoading.active || stateFrameWaiting) return true
        val rear = rearTouchInput.motion(event, window.decorView.width, window.decorView.height)
        if (rear.consumed) return true
        val filtered = rear.event ?: event
        try {
            if (KeyboardMouseManager.handleMouseBindingCapture(filtered)) return true
            if (!nativeKeyboardRequested) {
                // Preserve actionButton for unrelated mouse button mappings; SDL receives
                // the masked copy when the keyboard/mouse path does not handle it.
                val buttonEvent = if (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE) event else filtered
                val keyboardMouseHandled = KeyboardMouseManager.handleMouseButtonEvent(buttonEvent) ||
                    KeyboardMouseManager.handleMouseWheelEvent(filtered) ||
                    KeyboardMouseManager.handleMouseMotionEvent(filtered)
                if (keyboardMouseHandled) return true
            }
            return super.dispatchGenericMotionEvent(filtered)
        } finally { rear.event?.recycle() }
    }

    private fun updatePointerCapture(enabled: Boolean) {
        val decorView = window.decorView
        if (enabled && hasWindowFocus()) {
            decorView.isFocusable = true
            decorView.isFocusableInTouchMode = true
            if (!decorView.hasFocus()) decorView.requestFocus()
            if (!decorView.hasPointerCapture()) {
                decorView.post {
                    if (!isFinishing && hasWindowFocus() && !decorView.hasPointerCapture()) {
                        decorView.requestPointerCapture()
                    }
                }
            }
        } else {
            if (decorView.hasPointerCapture()) {
                decorView.releasePointerCapture()
            }
            decorView.isFocusable = false
            decorView.isFocusableInTouchMode = false
            decorView.clearFocus()
        }
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
        rearTouchInput.removeDevice(deviceId)
        InputDeviceClassifier.invalidateDevice(deviceId)
        refreshPhysicalGamepadState()
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        InputDeviceClassifier.invalidateDevice(deviceId)
        refreshPhysicalGamepadState()
    }

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

    fun getNativeDisplayRotation(): Int {
        @Suppress("DEPRECATION")
        return windowManager.defaultDisplay.rotation
    }

    external fun filedialogReturn(resultPath: String)

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (rebirthRequested) return true
        if (launchStateLoading.active || stateFrameWaiting) return true
        val rear = rearTouchInput.motion(event, window.decorView.width, window.decorView.height)
        if (rear.consumed) return true
        val filtered = rear.event ?: event
        try {
          if (KeyboardMouseManager.handleMouseBindingCapture(filtered)) return true
          if (!nativeKeyboardRequested) {
            val buttonEvent = if (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE) event else filtered
            val keyboardMouseHandled = KeyboardMouseManager.handleMouseButtonEvent(buttonEvent) ||
                KeyboardMouseManager.handleMouseMotionEvent(filtered)
            if (keyboardMouseHandled) return true
          }
          when (filtered.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_POINTER_UP -> {
                activeTouchSnapshot?.recycle()
                activeTouchSnapshot = MotionEvent.obtainNoHistory(filtered)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                activeTouchSnapshot?.recycle()
                activeTouchSnapshot = null
            }
        }
          return super.dispatchTouchEvent(filtered)
        } finally { rear.event?.recycle() }
    }

    private fun cancelActiveTouches() {
        rearTouchInput.releaseAll()
        val event = activeTouchSnapshot ?: return
        activeTouchSnapshot = null
        try {
            // Cancel the whole pointer stream in both Compose and SDL immediately.
            // Waiting for recomposition on pause can leave held controls stuck.
            event.action = MotionEvent.ACTION_CANCEL
            super.dispatchTouchEvent(event)
        } finally {
            event.recycle()
        }
    }

    private fun attachComposeOverlay() {
        if (isFirmwareSession) return
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
                val themePackage = com.sbro.emucorev.ui.theme.rememberAppliedThemePackage(preferences)
                EmuCoreVTheme(themeMode = themeMode, themePackage = themePackage, enableCrtOverlay = false) {
                    CompositionLocalProvider(LocalActivityResultRegistryOwner provides composeResultOwner) {
                        EmulationOverlayHost(activity = this@Emulator)
                    }
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
        if (isFirmwareSession) return
        val gameId = currentGameIdOrIntent().trim()
        if (gameId.isBlank()) return
        if (playTimeSessionId != null && playTimeSessionTitleId.equals(gameId, ignoreCase = true)) return
        finishPlayTimeSessionIfNeeded(scheduleBackup = false)

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
        playTimeSessionLastSeenAt = session.lastSeenAt
        BackupSessionGate.gameStarted()
        DiscordIntegration.setPlaying(title, gameId)
        runCatching {
            val config = VitaGameSettingsRepository(this).loadEffective(gameId)
            analyticsRenderer = config.backendRenderer
            analyticsAudioBackend = config.audioBackend
            AppAnalytics.logEmulationStarted(
                launchType = "game",
                renderer = config.backendRenderer,
                audioBackend = config.audioBackend,
                upscaleMultiplier = config.resolutionMultiplier,
                saveStateLoad = intent.getBooleanExtra(AUTOMATIC_STATE_LOAD_EXTRA, false) ||
                    loadStatePath(intent.getStringArrayExtra(APP_RESTART_PARAMETERS) ?: emptyArray()) != null
            )
        }
    }

    private fun requestRetroArchShaderReapply(titleId: String) {
        if (isFirmwareSession) return
        runOnUiThread {
            if (rebirthRequested || isFinishing || isDestroyed) return@runOnUiThread
            val normalizedTitleId = titleId.trim()
            if (normalizedTitleId.isBlank()) return@runOnUiThread
            retroArchShaderApplyJob?.cancel()
            val generation = ++retroArchShaderApplyGeneration
            val controller = retroArchShaderSession ?: RetroArchShaderSessionController(applicationContext)
                .also { retroArchShaderSession = it }
            retroArchShaderApplyJob = exitScope.launch {
                try {
                    controller.applyForTitle(normalizedTitleId) {
                        generation == retroArchShaderApplyGeneration &&
                            normalizedTitleId.equals(currentGameIdOrIntent().trim(), ignoreCase = true) &&
                            !rebirthRequested && !isFinishing && !isDestroyed
                    }
                } catch (cancelled: kotlinx.coroutines.CancellationException) {
                    throw cancelled
                } catch (error: Exception) {
                    Log.w(TAG, "Unable to reapply the RetroArch shader selection", error)
                }
            }
        }
    }

    private fun stopRetroArchShaderSession() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            runOnUiThread { stopRetroArchShaderSession() }
            return
        }
        retroArchShaderApplyGeneration++
        retroArchShaderApplyJob?.cancel()
        retroArchShaderApplyJob = null
        retroArchShaderSession?.stop()
        retroArchShaderSession = null
    }

    private fun finishPlayTimeSessionIfNeeded(scheduleBackup: Boolean = true) {
        val sessionId = playTimeSessionId ?: return
        PlayTimeRepository(this).finishSession(sessionId)
        val durationMs = (System.currentTimeMillis() - playTimeSessionStartedAt).coerceAtLeast(0L)
        AppAnalytics.logEmulationEnded(durationMs, analyticsRenderer, analyticsAudioBackend)
        val titleId = playTimeSessionTitleId
        if (titleId.isNotBlank() && durationMs > 0L) {
            val title = runCatching {
                InstalledGameRepository().findByTitleId(this, titleId)?.title
            }.getOrNull().orEmpty().ifBlank { titleId }
            ProfilePlayTimeSyncer.recordAndSync(applicationContext, titleId, title, durationMs)
        }
        playTimeSessionId = null
        playTimeSessionTitleId = ""
        playTimeSessionStartedAt = 0L
        playTimeSessionLastSeenAt = 0L
        if (scheduleBackup) {
            BackupSessionGate.stopped()
            DriveBackupWork.afterGame(applicationContext)
        }
        DiscordIntegration.clearGame()
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

    private fun triggerRebirthAfterNativeShutdown(targetIntent: Intent,
                                                outgoingTitle: String = currentGameIdOrIntent(),
                                                allowAutoSave: Boolean = true,
                                                returnToLibrary: Boolean = false) {
        runOnUiThread {
            if (rebirthRequested) return@runOnUiThread
            rebirthRequested = true
            cancelActiveTouches()
            keyboardRequestGeneration++
            if (::inputOverlay.isInitialized) inputOverlay.setTouchControlsActive(false)
            val saveEnabled = allowAutoSave && !LabSession.active && sessionUsable &&
                SaveStateAutomationPreferences(this, outgoingTitle).use { it.current.autoSaveOnExit }
            exitScope.launch {
                exitCoordinator.run(outgoingTitle, saveEnabled, save = { title ->
                    autoSaveInProgress = true
                    try {
                        setMenuPausedAndAwait(true)
                        val result = withContext(Dispatchers.IO) {
                            // A failed/destructive Load or already stopped core must never overwrite quick0.
                            if (SaveStateBridge.runningTitleId() != title) null else
                                SaveStateRepository(this@Emulator).save(title, SaveStateRepository.QUICK_SLOT, BuildConfig.VERSION_NAME)
                        }
                        if (result != null) AppAnalytics.logSaveStateAction("save", true, result.isOk)
                        if (result != null && !result.isOk) {
                            Toast.makeText(this@Emulator, R.string.emulation_savestate_failed_toast, Toast.LENGTH_LONG).show()
                        }
                    } catch (error: Exception) {
                        Log.e(TAG, "Automatic exit save failed", error)
                        AppAnalytics.logSaveStateAction("save", true, false)
                        Toast.makeText(this@Emulator, R.string.emulation_savestate_failed_toast, Toast.LENGTH_LONG).show()
                    } finally {
                        autoSaveInProgress = false
                    }
                }, shutdown = {
                    performNativeShutdown {
                        if (SDLActivity.mSDLThread?.isAlive == true) {
                            Log.w(TAG, "Proceeding with process rebirth while SDLThread is still alive; shader cache may not be fully flushed")
                        }
                        if (returnToLibrary && returnsToNativeLibrary && !isNativeShellSession) {
                            // The firmware library has its own process and task.
                            // Bring that resident Activity forward without Phoenix's
                            // CLEAR_TASK, then retire only this completed game process.
                            targetIntent.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                            startActivity(targetIntent)
                            // Remove the retained task so it cannot re-deliver this launch Intent.
                            finishAndRemoveTask()
                            android.os.Process.killProcess(android.os.Process.myPid())
                        } else if (returnToLibrary && canReuseNativeSession()) {
                            // Preserve the library Activity, ViewModel and decoded covers.
                            targetIntent.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                            startActivity(targetIntent)
                            finish()
                            @Suppress("DEPRECATION")
                            overridePendingTransition(0, 0)
                        } else {
                            // A timed-out native session cannot safely be reused.
                            ProcessPhoenix.triggerRebirth(applicationContext, targetIntent)
                        }
                    }
                })
            }
        }
    }

    private fun isLaunchIntent(intent: Intent?): Boolean {
        val action = intent?.action.orEmpty()
        return action == ACTION_EXTERNAL_LAUNCH || action.startsWith("LAUNCH_")
    }

    /** Validated title id of a launch intent; "" when absent or malformed (the activity is exported). */
    private fun gameIdFromIntent(intent: Intent?): String =
        com.sbro.emucorev.core.TitleIds.sanitizeOrNull(rawGameIdFromIntent(intent)).orEmpty()

    /** True when the intent names a title id that is not a safe id; such a launch is rejected. */
    private fun launchRequestMalformed(intent: Intent?): Boolean {
        if (intent == null) return false
        val args = intent.getStringArrayExtra(APP_RESTART_PARAMETERS)
        val restartIndex = args?.indexOf("-r") ?: -1
        val argId = if (args != null && restartIndex >= 0) args.getOrNull(restartIndex + 1).orEmpty() else ""
        return listOf(rawGameIdFromIntent(intent), argId).any {
            it.isNotBlank() && !com.sbro.emucorev.core.TitleIds.isValid(it.trim())
        }
    }

    private fun rawGameIdFromIntent(intent: Intent?): String {
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
        val raw = if (restartIndex >= 0 && restartIndex + 1 < args.size) args[restartIndex + 1] else ""
        return com.sbro.emucorev.core.TitleIds.sanitizeOrNull(raw).orEmpty()
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
        const val CHEAT_IMPORT_CODE = 547
        const val EXTRA_REBIRTH_HANDLED = "emu_rebirth_handled"
        const val EMULATION_OVERLAY_ELEVATION = 64f
        const val PLAY_TIME_HEARTBEAT_INTERVAL_MS = 60_000L
        const val INPUT_GATE_POLL_MS = 1_000L
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
