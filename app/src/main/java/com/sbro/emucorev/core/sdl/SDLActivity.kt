@file:Suppress("DEPRECATION")

package com.sbro.emucorev.core.sdl

import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.app.UiModeManager
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.PorterDuff
import android.graphics.drawable.Drawable
import android.hardware.Sensor
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.LocaleList
import android.os.Message
import android.os.ParcelFileDescriptor
import android.util.DisplayMetrics
import android.util.Log
import android.util.SparseArray
import android.view.Display
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.PointerIcon
import android.view.Surface
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.webkit.MimeTypeMap
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RelativeLayout
import android.widget.TextView
import android.widget.Toast
import java.io.FileNotFoundException
import java.util.Hashtable
import java.util.Locale
import kotlin.math.sqrt

open class SDLActivity : Activity(), View.OnSystemUiVisibilityChangeListener {
    companion object {
        private const val TAG = "SDL"
        private const val SDL_MAJOR_VERSION = 3
        private const val SDL_MINOR_VERSION = 2
        private const val SDL_MICRO_VERSION = 28

        @JvmField
        var mIsResumedCalled = false

        @JvmField
        var mHasFocus = false

        @JvmField
        val mHasMultiWindow = Build.VERSION.SDK_INT >= Build.VERSION_CODES.N

        private const val SDL_SYSTEM_CURSOR_ARROW = 0
        private const val SDL_SYSTEM_CURSOR_IBEAM = 1
        private const val SDL_SYSTEM_CURSOR_WAIT = 2
        private const val SDL_SYSTEM_CURSOR_CROSSHAIR = 3
        private const val SDL_SYSTEM_CURSOR_WAITARROW = 4
        private const val SDL_SYSTEM_CURSOR_SIZENWSE = 5
        private const val SDL_SYSTEM_CURSOR_SIZENESW = 6
        private const val SDL_SYSTEM_CURSOR_SIZEWE = 7
        private const val SDL_SYSTEM_CURSOR_SIZENS = 8
        private const val SDL_SYSTEM_CURSOR_SIZEALL = 9
        private const val SDL_SYSTEM_CURSOR_NO = 10
        private const val SDL_SYSTEM_CURSOR_HAND = 11
        private const val SDL_SYSTEM_CURSOR_WINDOW_TOPLEFT = 12
        private const val SDL_SYSTEM_CURSOR_WINDOW_TOP = 13
        private const val SDL_SYSTEM_CURSOR_WINDOW_TOPRIGHT = 14
        private const val SDL_SYSTEM_CURSOR_WINDOW_RIGHT = 15
        private const val SDL_SYSTEM_CURSOR_WINDOW_BOTTOMRIGHT = 16
        private const val SDL_SYSTEM_CURSOR_WINDOW_BOTTOM = 17
        private const val SDL_SYSTEM_CURSOR_WINDOW_BOTTOMLEFT = 18
        private const val SDL_SYSTEM_CURSOR_WINDOW_LEFT = 19

        private const val SDL_ORIENTATION_UNKNOWN = 0
        private const val SDL_ORIENTATION_LANDSCAPE = 1
        private const val SDL_ORIENTATION_PORTRAIT = 3

        const val COMMAND_CHANGE_TITLE = 1
        const val COMMAND_CHANGE_WINDOW_STYLE = 2
        const val COMMAND_TEXTEDIT_HIDE = 3
        const val COMMAND_SET_KEEP_SCREEN_ON = 5
        const val COMMAND_USER = 0x8000

        @JvmField
        var mCurrentRotation = 0

        @JvmField
        var mCurrentLocale: Locale? = null

        @JvmField
        var mNextNativeState = NativeState.INIT

        @JvmField
        var mCurrentNativeState = NativeState.INIT

        @JvmField
        var mBrokenLibraries = true

        @JvmField
        var mSingleton: SDLActivity? = null

        @JvmField
        var mSurface: SDLSurface? = null

        @JvmField
        var mTextEdit: SDLDummyEdit? = null

        @JvmField
        var mScreenKeyboardShown = false

        @JvmField
        var mLayout: ViewGroup? = null

        @JvmField
        var mClipboardHandler: SDLClipboardHandler? = null

        @JvmField
        var mCursors = Hashtable<Int, PointerIcon>()

        @JvmField
        var mLastCursorID = 0

        @JvmField
        var mMotionListener: SDLGenericMotionListener_API14? = null

        @JvmField
        var mHIDDeviceManager: HIDDeviceManager? = null

        @JvmField
        var mSDLThread: Thread? = null

        @JvmField
        var mSDLMainFinished = false

        @JvmField
        var mActivityCreated = false

        @JvmField
        var mHasNativeShutdown = false

        private var mFileDialogState: SDLFileDialogState? = null

        @JvmField
        var mDispatchingKeyEvent = false

        @JvmField
        var mFullscreenModeActive = false

        @JvmStatic
        fun getMotionListener(): SDLGenericMotionListener_API14 {
            if (mMotionListener == null) {
                mMotionListener = when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.O -> SDLGenericMotionListener_API26()
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.N -> SDLGenericMotionListener_API24()
                    else -> SDLGenericMotionListener_API14()
                }
            }
            return mMotionListener!!
        }

        @JvmStatic
        fun initialize() {
            mSingleton = null
            mSurface = null
            mTextEdit = null
            mLayout = null
            mClipboardHandler = null
            mCursors = Hashtable()
            mLastCursorID = 0
            mSDLThread = null
            mIsResumedCalled = false
            mHasFocus = true
            mHasNativeShutdown = false
            mNextNativeState = NativeState.INIT
            mCurrentNativeState = NativeState.INIT
        }

        @JvmStatic
        fun getNaturalOrientation(): Int {
            val activity = getContext() as? Activity ?: return SDL_ORIENTATION_UNKNOWN
            val config = activity.resources.configuration
            val rotation = activity.windowManager.defaultDisplay.rotation
            return if (((rotation == Surface.ROTATION_0 || rotation == Surface.ROTATION_180) &&
                    config.orientation == Configuration.ORIENTATION_LANDSCAPE) ||
                ((rotation == Surface.ROTATION_90 || rotation == Surface.ROTATION_270) &&
                    config.orientation == Configuration.ORIENTATION_PORTRAIT)
            ) {
                SDL_ORIENTATION_LANDSCAPE
            } else {
                SDL_ORIENTATION_PORTRAIT
            }
        }

        @JvmStatic
        fun getCurrentRotation(): Int {
            val activity = getContext() as? Activity ?: return 0
            return when (activity.windowManager.defaultDisplay.rotation) {
                Surface.ROTATION_90 -> 90
                Surface.ROTATION_180 -> 180
                Surface.ROTATION_270 -> 270
                else -> 0
            }
        }

        @JvmStatic
        fun manualBackButton() {
            mSingleton?.pressBackButton()
        }

        @JvmStatic
        fun dispatchingKeyEvent(): Boolean = mDispatchingKeyEvent

        @JvmStatic
        fun handleNativeState() {
            if (mHasNativeShutdown) {
                return
            }
            if (mNextNativeState == mCurrentNativeState) {
                return
            }

            if (mNextNativeState == NativeState.INIT) {
                mCurrentNativeState = mNextNativeState
                return
            }

            if (mNextNativeState == NativeState.PAUSED) {
                if (mSDLThread != null) {
                    nativePause()
                }
                mSurface?.handlePause()
                mCurrentNativeState = mNextNativeState
                return
            }

            if (mNextNativeState == NativeState.RESUMED &&
                mSurface?.mIsSurfaceReady == true &&
                (mHasFocus || mHasMultiWindow) &&
                mIsResumedCalled
            ) {
                if (mSDLThread == null) {
                    mSDLThread = Thread(SDLMain(), "SDLThread")
                    mSurface?.enableSensor(Sensor.TYPE_ACCELEROMETER, true)
                    mSDLThread?.start()
                } else {
                    nativeResume()
                }
                mSurface?.handleResume()
                mCurrentNativeState = mNextNativeState
            }
        }

        @JvmStatic
        external fun nativeGetVersion(): String

        @JvmStatic
        external fun nativeSetupJNI(): Int

        @JvmStatic
        external fun nativeInitMainThread()

        @JvmStatic
        external fun nativeCleanupMainThread()

        @JvmStatic
        external fun nativeRunMain(library: String, function: String, arguments: Any): Int

        @JvmStatic
        external fun nativeLowMemory()

        @JvmStatic
        external fun nativeSendQuit()

        @JvmStatic
        external fun nativeQuit()

        @JvmStatic
        external fun nativePause()

        @JvmStatic
        external fun nativeResume()

        @JvmStatic
        external fun nativeFocusChanged(hasFocus: Boolean)

        @JvmStatic
        external fun onNativeDropFile(filename: String)

        @JvmStatic
        external fun nativeSetScreenResolution(
            surfaceWidth: Int,
            surfaceHeight: Int,
            deviceWidth: Int,
            deviceHeight: Int,
            density: Float,
            rate: Float,
        )

        @JvmStatic
        external fun onNativeResize()

        @JvmStatic
        external fun onNativeKeyDown(keycode: Int)

        @JvmStatic
        external fun onNativeKeyUp(keycode: Int)

        @JvmStatic
        external fun onNativeSoftReturnKey(): Boolean

        @JvmStatic
        external fun onNativeKeyboardFocusLost()

        @JvmStatic
        external fun onNativeMouse(button: Int, action: Int, x: Float, y: Float, relative: Boolean)

        @JvmStatic
        external fun onNativeTouch(
            touchDevId: Int,
            pointerFingerId: Int,
            action: Int,
            x: Float,
            y: Float,
            p: Float,
        )

        @JvmStatic
        external fun onNativePen(penId: Int, button: Int, action: Int, x: Float, y: Float, p: Float)

        @JvmStatic
        external fun onNativeAccel(x: Float, y: Float, z: Float)

        @JvmStatic
        external fun onNativeClipboardChanged()

        @JvmStatic
        external fun onNativeSurfaceCreated()

        @JvmStatic
        external fun onNativeSurfaceChanged()

        @JvmStatic
        external fun onNativeSurfaceDestroyed()

        @JvmStatic
        external fun nativeGetHint(name: String): String

        @JvmStatic
        external fun nativeGetHintBoolean(name: String, defaultValue: Boolean): Boolean

        @JvmStatic
        external fun nativeSetenv(name: String, value: String)

        @JvmStatic
        external fun nativeSetNaturalOrientation(orientation: Int)

        @JvmStatic
        external fun onNativeRotationChanged(rotation: Int)

        @JvmStatic
        external fun onNativeInsetsChanged(left: Int, right: Int, top: Int, bottom: Int)

        @JvmStatic
        external fun nativeAddTouch(touchId: Int, name: String)

        @JvmStatic
        external fun nativePermissionResult(requestCode: Int, result: Boolean)

        @JvmStatic
        external fun onNativeLocaleChanged()

        @JvmStatic
        external fun onNativeDarkModeChanged(enabled: Boolean)

        @JvmStatic
        external fun nativeAllowRecreateActivity(): Boolean

        @JvmStatic
        external fun nativeCheckSDLThreadCounter(): Int

        @JvmStatic
        external fun onNativeFileDialog(requestCode: Int, filelist: Array<String>, filter: Int)

        @JvmStatic
        fun setActivityTitle(title: String): Boolean = mSingleton?.sendCommand(COMMAND_CHANGE_TITLE, title) == true

        @JvmStatic
        fun setWindowStyle(fullscreen: Boolean) {
            mSingleton?.sendCommand(COMMAND_CHANGE_WINDOW_STYLE, if (fullscreen) 1 else 0)
        }

        @JvmStatic
        fun setOrientation(w: Int, h: Int, resizable: Boolean, hint: String) {
            mSingleton?.setOrientationBis(w, h, resizable, hint)
        }

        @JvmStatic
        fun minimizeWindow() {
            val singleton = mSingleton ?: return
            val startMain = Intent(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_HOME)
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            singleton.startActivity(startMain)
        }

        @JvmStatic
        fun shouldMinimizeOnFocusLoss(): Boolean = false

        @JvmStatic
        fun isScreenKeyboardShown(): Boolean {
            val textEdit = mTextEdit ?: return false
            if (!mScreenKeyboardShown) {
                return false
            }
            val imm = SDL.getContext().getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
            return imm?.isAcceptingText == true && textEdit.visibility == View.VISIBLE
        }

        @JvmStatic
        fun supportsRelativeMouse(): Boolean {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O_MR1 && isDeXMode()) {
                return false
            }
            return getMotionListener().supportsRelativeMouse()
        }

        @JvmStatic
        fun setRelativeMouseEnabled(enabled: Boolean): Boolean {
            if (enabled && !supportsRelativeMouse()) {
                return false
            }
            return getMotionListener().setRelativeMouseEnabled(enabled)
        }

        @JvmStatic
        fun sendMessage(command: Int, param: Int): Boolean = mSingleton?.sendCommand(command, param) == true

        @JvmStatic
        fun getContext(): Context = SDL.getContext()

        @JvmStatic
        fun isAndroidTV(): Boolean {
            val uiModeManager = getContext().getSystemService(UI_MODE_SERVICE) as? UiModeManager ?: return false
            if (uiModeManager.currentModeType == Configuration.UI_MODE_TYPE_TELEVISION) {
                return true
            }
            if (Build.MANUFACTURER == "MINIX" && Build.MODEL == "NEO-U1") {
                return true
            }
            if (Build.MANUFACTURER == "Amlogic" && Build.MODEL == "X96-W") {
                return true
            }
            return Build.MANUFACTURER == "Amlogic" && Build.MODEL.startsWith("TV")
        }

        @JvmStatic
        fun isVRHeadset(): Boolean {
            if (Build.MANUFACTURER == "Oculus" && Build.MODEL.startsWith("Quest")) {
                return true
            }
            return Build.MANUFACTURER == "Pico"
        }

        @JvmStatic
        fun getDiagonal(): Double {
            val activity = getContext() as? Activity ?: return 0.0
            val metrics = DisplayMetrics()
            activity.windowManager.defaultDisplay.getMetrics(metrics)
            val widthInches = metrics.widthPixels / metrics.xdpi.toDouble()
            val heightInches = metrics.heightPixels / metrics.ydpi.toDouble()
            return sqrt(widthInches * widthInches + heightInches * heightInches)
        }

        @JvmStatic
        fun isTablet(): Boolean = getDiagonal() >= 7.0

        @JvmStatic
        fun isChromebook(): Boolean =
            getContext().packageManager.hasSystemFeature("org.chromium.arc.device_management")

        @JvmStatic
        fun isDeXMode(): Boolean {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                return false
            }
            return try {
                val config = getContext().resources.configuration
                val configClass = config.javaClass
                configClass.getField("SEM_DESKTOP_MODE_ENABLED").getInt(configClass) ==
                    configClass.getField("semDesktopModeEnabled").getInt(config)
            } catch (_: Exception) {
                false
            }
        }

        @JvmStatic
        fun getManifestEnvironmentVariables(): Boolean {
            return try {
                val context = getContext()
                val applicationInfo: ApplicationInfo =
                    context.packageManager.getApplicationInfo(context.packageName, PackageManager.GET_META_DATA)
                val bundle = applicationInfo.metaData ?: return false
                val prefix = "SDL_ENV."
                val trimLength = prefix.length
                for (key in bundle.keySet()) {
                    if (key.startsWith(prefix)) {
                        val name = key.substring(trimLength)
                        val value = bundle.get(key)?.toString() ?: continue
                        nativeSetenv(name, value)
                    }
                }
                true
            } catch (e: Exception) {
                Log.v(TAG, "exception $e")
                false
            }
        }

        @JvmStatic
        fun getContentView(): View = requireNotNull(mLayout)

        @JvmStatic
        fun showTextInput(inputType: Int, x: Int, y: Int, w: Int, h: Int): Boolean {
            val singleton = mSingleton ?: return false
            return singleton.commandHandler.post(ShowTextInputTask(inputType, x, y, w, h))
        }

        @JvmStatic
        fun isTextInputEvent(event: KeyEvent): Boolean {
            if (event.isCtrlPressed) {
                return false
            }
            return event.isPrintingKey || event.keyCode == KeyEvent.KEYCODE_SPACE
        }

        @JvmStatic
        fun handleKeyEvent(v: View, keyCode: Int, event: KeyEvent, ic: InputConnection?): Boolean {
            val deviceId = event.deviceId
            var source = event.source

            if (source == InputDevice.SOURCE_UNKNOWN) {
                val device = InputDevice.getDevice(deviceId)
                if (device != null) {
                    source = device.sources
                }
            }

            if (SDLControllerManager.isDeviceSDLJoystick(deviceId)) {
                when (event.action) {
                    KeyEvent.ACTION_DOWN -> if (SDLControllerManager.onNativePadDown(deviceId, keyCode)) return true
                    KeyEvent.ACTION_UP -> if (SDLControllerManager.onNativePadUp(deviceId, keyCode)) return true
                }
            }

            if ((source and InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
                if (!isVRHeadset() && (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_FORWARD)) {
                    if (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP) {
                        return true
                    }
                }
            }

            return when (event.action) {
                KeyEvent.ACTION_DOWN -> {
                    onNativeKeyDown(keyCode)
                    if (isTextInputEvent(event)) {
                        val text = event.unicodeChar.toChar().toString()
                        if (ic != null) {
                            ic.commitText(text, 1)
                        } else {
                            SDLInputConnection.nativeCommitText(text, 1)
                        }
                    }
                    true
                }
                KeyEvent.ACTION_UP -> {
                    onNativeKeyUp(keyCode)
                    true
                }
                else -> false
            }
        }

        @JvmStatic
        fun getNativeSurface(): Surface? = mSurface?.getNativeSurface()

        @JvmStatic
        fun initTouch() {
            for (id in InputDevice.getDeviceIds()) {
                val device = InputDevice.getDevice(id)
                if (device != null &&
                    (((device.sources and InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) ||
                        device.isVirtual)
                ) {
                    nativeAddTouch(device.id, device.name)
                }
            }
        }

        @JvmStatic
        fun clipboardHasText(): Boolean = mClipboardHandler?.clipboardHasText() == true

        @JvmStatic
        fun clipboardGetText(): String? = mClipboardHandler?.clipboardGetText()

        @JvmStatic
        fun clipboardSetText(string: String) {
            mClipboardHandler?.clipboardSetText(string)
        }

        @JvmStatic
        fun createCustomCursor(colors: IntArray, width: Int, height: Int, hotSpotX: Int, hotSpotY: Int): Int {
            val bitmap = Bitmap.createBitmap(colors, width, height, Bitmap.Config.ARGB_8888)
            mLastCursorID += 1
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                try {
                    mCursors[mLastCursorID] = PointerIcon.create(bitmap, hotSpotX.toFloat(), hotSpotY.toFloat())
                } catch (_: Exception) {
                    return 0
                }
            } else {
                return 0
            }
            return mLastCursorID
        }

        @JvmStatic
        fun destroyCustomCursor(cursorID: Int) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                try {
                    mCursors.remove(cursorID)
                } catch (_: Exception) {
                }
            }
        }

        @JvmStatic
        fun setCustomCursor(cursorID: Int): Boolean {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                return try {
                    mSurface?.pointerIcon = mCursors[cursorID]
                    true
                } catch (_: Exception) {
                    false
                }
            }
            return false
        }

        @JvmStatic
        fun setSystemCursor(cursorID: Int): Boolean {
            val cursorType = when (cursorID) {
                SDL_SYSTEM_CURSOR_ARROW -> 1000
                SDL_SYSTEM_CURSOR_IBEAM -> 1008
                SDL_SYSTEM_CURSOR_WAIT, SDL_SYSTEM_CURSOR_WAITARROW -> 1004
                SDL_SYSTEM_CURSOR_CROSSHAIR -> 1007
                SDL_SYSTEM_CURSOR_SIZENWSE, SDL_SYSTEM_CURSOR_WINDOW_TOPLEFT, SDL_SYSTEM_CURSOR_WINDOW_BOTTOMRIGHT -> 1017
                SDL_SYSTEM_CURSOR_SIZENESW, SDL_SYSTEM_CURSOR_WINDOW_TOPRIGHT, SDL_SYSTEM_CURSOR_WINDOW_BOTTOMLEFT -> 1016
                SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_WINDOW_RIGHT, SDL_SYSTEM_CURSOR_WINDOW_LEFT -> 1014
                SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_WINDOW_TOP, SDL_SYSTEM_CURSOR_WINDOW_BOTTOM -> 1015
                SDL_SYSTEM_CURSOR_SIZEALL -> 1020
                SDL_SYSTEM_CURSOR_NO -> 1012
                SDL_SYSTEM_CURSOR_HAND -> 1002
                else -> 0
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                return try {
                    mSurface?.pointerIcon = PointerIcon.getSystemIcon(SDL.getContext(), cursorType)
                    true
                } catch (_: Exception) {
                    false
                }
            }
            return true
        }

        @JvmStatic
        fun requestPermission(permission: String, requestCode: Int) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                nativePermissionResult(requestCode, true)
                return
            }

            val activity = getContext() as? Activity ?: run {
                nativePermissionResult(requestCode, false)
                return
            }

            if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                activity.requestPermissions(arrayOf(permission), requestCode)
            } else {
                nativePermissionResult(requestCode, true)
            }
        }

        @JvmStatic
        fun openURL(url: String): Boolean {
            val singleton = mSingleton ?: return false
            return try {
                val intent = Intent(Intent.ACTION_VIEW).apply {
                    data = Uri.parse(url)
                    var intentFlags = Intent.FLAG_ACTIVITY_NO_HISTORY or Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                    intentFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                        intentFlags or Intent.FLAG_ACTIVITY_NEW_DOCUMENT
                    } else {
                        intentFlags or Intent.FLAG_ACTIVITY_CLEAR_WHEN_TASK_RESET
                    }
                    addFlags(intentFlags)
                }
                singleton.startActivity(intent)
                true
            } catch (_: Exception) {
                false
            }
        }

        @JvmStatic
        fun showToast(message: String, duration: Int, gravity: Int, xOffset: Int, yOffset: Int): Boolean {
            val singleton = mSingleton ?: return false
            return try {
                singleton.runOnUiThread {
                    try {
                        val toast = Toast.makeText(singleton, message, duration)
                        if (gravity >= 0) {
                            toast.setGravity(gravity, xOffset, yOffset)
                        }
                        toast.show()
                    } catch (ex: Exception) {
                        Log.e(TAG, ex.message ?: "Toast error")
                    }
                }
                true
            } catch (_: Exception) {
                false
            }
        }

        @JvmStatic
        fun openFileDescriptor(uri: String, mode: String): Int {
            val singleton = mSingleton ?: return -1
            return try {
                val pfd: ParcelFileDescriptor? = singleton.contentResolver.openFileDescriptor(Uri.parse(uri), mode)
                pfd?.detachFd() ?: -1
            } catch (e: FileNotFoundException) {
                e.printStackTrace()
                -1
            }
        }

        @JvmStatic
        fun showFileDialog(filters: Array<String>?, allowMultiple: Boolean, forWrite: Boolean, requestCode: Int): Boolean {
            val singleton = mSingleton ?: return false
            val resolvedAllowMultiple = if (forWrite) false else allowMultiple
            val mimes = mutableListOf<String>()
            val mimeTypeMap = MimeTypeMap.getSingleton()
            filters?.forEach { pattern ->
                val extensions = pattern.split(";")
                if (extensions.size == 1 && extensions[0] == "*") {
                    mimes.add("*/*")
                } else {
                    extensions.forEach { ext ->
                        mimeTypeMap.getMimeTypeFromExtension(ext)?.let(mimes::add)
                    }
                }
            }

            val intent = Intent(if (forWrite) Intent.ACTION_CREATE_DOCUMENT else Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                putExtra(Intent.EXTRA_ALLOW_MULTIPLE, resolvedAllowMultiple)
                when (mimes.size) {
                    0 -> type = "*/*"
                    1 -> type = mimes[0]
                    else -> {
                        type = "*/*"
                        putExtra(Intent.EXTRA_MIME_TYPES, mimes.toTypedArray())
                    }
                }
            }

            try {
                singleton.startActivityForResult(intent, requestCode)
            } catch (e: ActivityNotFoundException) {
                Log.e(TAG, "Unable to open file dialog.", e)
                return false
            }

            mFileDialogState = SDLFileDialogState(requestCode, resolvedAllowMultiple)
            return true
        }

        @JvmStatic
        fun getPreferredLocales(): String {
            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val locales = LocaleList.getAdjustedDefault()
                buildString {
                    for (i in 0 until locales.size()) {
                        if (i != 0) append(',')
                        append(formatLocale(locales[i]))
                    }
                }
            } else {
                mCurrentLocale?.let(::formatLocale).orEmpty()
            }
        }

        @JvmStatic
        fun formatLocale(locale: Locale): String {
            val language = when (locale.language) {
                "in" -> "id"
                "" -> "und"
                else -> locale.language
            }
            return if (locale.country.isEmpty()) {
                language
            } else {
                "${language}_${locale.country}"
            }
        }
    }

    enum class NativeState {
        INIT,
        RESUMED,
        PAUSED,
    }

    protected open fun main() {
        val singleton = checkNotNull(mSingleton)
        val library = singleton.getMainSharedObject()
        val function = singleton.getMainFunction()
        val arguments = singleton.getArguments()
        Log.v(TAG, "Running main function $function from library $library")
        nativeRunMain(library, function, arguments)
        Log.v(TAG, "Finished main function")
    }

    protected open fun getMainSharedObject(): String {
        val libraries = checkNotNull(mSingleton).getLibraries()
        val library = if (libraries.isNotEmpty()) {
            "lib${libraries.last()}.so"
        } else {
            "libmain.so"
        }
        return "${getContext().applicationInfo.nativeLibraryDir}/$library"
    }

    protected open fun getMainFunction(): String = "SDL_main"

    protected open fun getLibraries(): Array<String> = arrayOf("SDL3", "main")

    fun loadLibraries() {
        for (lib in getLibraries()) {
            SDL.loadLibrary(lib, this)
        }
    }

    protected open fun getArguments(): Array<String> = emptyArray()

    protected open fun createSDLSurface(context: Context): SDLSurface = SDLSurface(context)

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.v(TAG, "Manufacturer: ${Build.MANUFACTURER}")
        Log.v(TAG, "Device: ${Build.DEVICE}")
        Log.v(TAG, "Model: ${Build.MODEL}")
        Log.v(TAG, "onCreate()")
        super.onCreate(savedInstanceState)

        if (mSDLMainFinished || mActivityCreated) {
            val allowRecreate = nativeAllowRecreateActivity()
            if (mSDLMainFinished) {
                Log.v(TAG, "SDL main() finished")
            }
            if (allowRecreate) {
                Log.v(TAG, "activity re-created")
            } else {
                Log.v(TAG, "activity finished")
                System.exit(0)
                return
            }
        }

        mActivityCreated = true

        try {
            Thread.currentThread().name = "SDLActivity"
        } catch (e: Exception) {
            Log.v(TAG, "modify thread properties failed $e")
        }

        var errorMsgBrokenLib = ""
        try {
            loadLibraries()
            mBrokenLibraries = false
        } catch (e: UnsatisfiedLinkError) {
            System.err.println(e.message)
            mBrokenLibraries = true
            errorMsgBrokenLib = e.message.orEmpty()
        } catch (e: Exception) {
            System.err.println(e.message)
            mBrokenLibraries = true
            errorMsgBrokenLib = e.message.orEmpty()
        }

        if (!mBrokenLibraries) {
            val expectedVersion = "$SDL_MAJOR_VERSION.$SDL_MINOR_VERSION.$SDL_MICRO_VERSION"
            val version = nativeGetVersion()
            if (version != expectedVersion) {
                mBrokenLibraries = true
                errorMsgBrokenLib = "SDL C/Java version mismatch (expected $expectedVersion, got $version)"
            }
        }

        if (mBrokenLibraries) {
            mSingleton = this
            AlertDialog.Builder(this)
                .setMessage(
                    "An error occurred while trying to start the application. Please try again and/or reinstall." +
                        System.lineSeparator() + System.lineSeparator() +
                        "Error: $errorMsgBrokenLib",
                )
                .setTitle("SDL Error")
                .setPositiveButton("Exit") { _, _ -> mSingleton?.finish() }
                .setCancelable(false)
                .create()
                .show()
            return
        }

        val runCount = nativeCheckSDLThreadCounter()
        if (runCount != 0) {
            val allowRecreate = nativeAllowRecreateActivity()
            if (allowRecreate) {
                Log.v(TAG, "activity re-created // run_count: $runCount")
            } else {
                Log.v(TAG, "activity finished // run_count: $runCount")
                System.exit(0)
                return
            }
        }

        SDL.setupJNI()
        SDL.initialize()

        mSingleton = this
        SDL.setContext(this)

        mClipboardHandler = SDLClipboardHandler()
        mHIDDeviceManager = HIDDeviceManager.acquire(this)

        mSurface = createSDLSurface(this)
        mLayout = RelativeLayout(this).apply {
            addView(mSurface)
        }

        nativeSetNaturalOrientation(getNaturalOrientation())
        mCurrentRotation = getCurrentRotation()
        onNativeRotationChanged(mCurrentRotation)

        mCurrentLocale = configurationLocale(resources.configuration)
        when (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) {
            Configuration.UI_MODE_NIGHT_NO -> onNativeDarkModeChanged(false)
            Configuration.UI_MODE_NIGHT_YES -> onNativeDarkModeChanged(true)
        }

        setContentView(mLayout)
        setWindowStyle(false)
        window.decorView.setOnSystemUiVisibilityChangeListener(this)

        val intent = intent
        val filename = intent?.data?.path
        if (filename != null) {
            Log.v(TAG, "Got filename: $filename")
            onNativeDropFile(filename)
        }
    }

    protected fun pauseNativeThread() {
        mNextNativeState = NativeState.PAUSED
        mIsResumedCalled = false
        if (mBrokenLibraries) {
            return
        }
        handleNativeState()
    }

    protected fun resumeNativeThread() {
        mNextNativeState = NativeState.RESUMED
        mIsResumedCalled = true
        if (mBrokenLibraries) {
            return
        }
        handleNativeState()
    }

    override fun onPause() {
        Log.v(TAG, "onPause()")
        super.onPause()
        mHIDDeviceManager?.setFrozen(true)
        if (!mHasMultiWindow) {
            pauseNativeThread()
        }
    }

    override fun onResume() {
        Log.v(TAG, "onResume()")
        super.onResume()
        mHIDDeviceManager?.setFrozen(false)
        if (!mHasMultiWindow) {
            resumeNativeThread()
        }
    }

    override fun onStop() {
        Log.v(TAG, "onStop()")
        super.onStop()
        if (mHasMultiWindow) {
            pauseNativeThread()
        }
    }

    override fun onStart() {
        Log.v(TAG, "onStart()")
        super.onStart()
        if (mHasMultiWindow) {
            resumeNativeThread()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        Log.v(TAG, "onWindowFocusChanged(): $hasFocus")
        if (mBrokenLibraries || mHasNativeShutdown) {
            return
        }
        mHasFocus = hasFocus
        if (hasFocus) {
            mNextNativeState = NativeState.RESUMED
            getMotionListener().reclaimRelativeMouseModeIfNeeded()
            handleNativeState()
            nativeFocusChanged(true)
        } else {
            nativeFocusChanged(false)
            if (!mHasMultiWindow) {
                mNextNativeState = NativeState.PAUSED
                handleNativeState()
            }
        }
    }

    override fun onTrimMemory(level: Int) {
        Log.v(TAG, "onTrimMemory()")
        super.onTrimMemory(level)
        if (mBrokenLibraries || mHasNativeShutdown) {
            return
        }
        nativeLowMemory()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        Log.v(TAG, "onConfigurationChanged()")
        super.onConfigurationChanged(newConfig)
        if (mBrokenLibraries || mHasNativeShutdown) {
            return
        }

        val locale = configurationLocale(newConfig)
        if (mCurrentLocale == null || mCurrentLocale != locale) {
            mCurrentLocale = locale
            onNativeLocaleChanged()
        }

        when (newConfig.uiMode and Configuration.UI_MODE_NIGHT_MASK) {
            Configuration.UI_MODE_NIGHT_NO -> onNativeDarkModeChanged(false)
            Configuration.UI_MODE_NIGHT_YES -> onNativeDarkModeChanged(true)
        }
    }

    override fun onDestroy() {
        Log.v(TAG, "onDestroy()")
        performNativeShutdown()
        super.onDestroy()
    }

    protected fun performNativeShutdown() {
        if (mHasNativeShutdown) {
            return
        }
        mHasNativeShutdown = true

        mHIDDeviceManager?.let {
            HIDDeviceManager.release(it)
            mHIDDeviceManager = null
        }
        SDLAudioManager.release(this)

        if (mBrokenLibraries) {
            return
        }

        if (mSDLThread != null) {
            nativeSendQuit()
            try {
                mSDLThread?.join(1000)
            } catch (e: Exception) {
                Log.v(TAG, "Problem stopping SDLThread: $e")
            } finally {
                mSDLThread = null
            }
        }

        nativeQuit()
    }

    override fun onBackPressed() {
        val trapBack = nativeGetHintBoolean("SDL_ANDROID_TRAP_BACK_BUTTON", false)
        if (trapBack) {
            return
        }
        if (!isFinishing) {
            super.onBackPressed()
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        val fileDialogState = mFileDialogState
        if (fileDialogState != null && fileDialogState.requestCode == requestCode) {
            val filelist = if (data != null) {
                val singleFileUri = data.data
                if (singleFileUri == null) {
                    val clipData = data.clipData
                    if (clipData != null) {
                        Array(clipData.itemCount) { index -> clipData.getItemAt(index).uri.toString() }
                    } else {
                        emptyArray()
                    }
                } else {
                    arrayOf(singleFileUri.toString())
                }
            } else {
                emptyArray()
            }
            onNativeFileDialog(requestCode, filelist, -1)
            mFileDialogState = null
        }
    }

    fun pressBackButton() {
        runOnUiThread {
            if (!isFinishing) {
                superOnBackPressed()
            }
        }
    }

    open fun superOnBackPressed() {
        super.onBackPressed()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (mBrokenLibraries) {
            return false
        }

        val keyCode = event.keyCode
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
            keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            keyCode == KeyEvent.KEYCODE_CAMERA ||
            keyCode == KeyEvent.KEYCODE_ZOOM_IN ||
            keyCode == KeyEvent.KEYCODE_ZOOM_OUT
        ) {
            return false
        }
        mDispatchingKeyEvent = true
        val result = super.dispatchKeyEvent(event)
        mDispatchingKeyEvent = false
        return result
    }

    protected open fun onUnhandledMessage(command: Int, param: Any?): Boolean = false

    protected open fun sendCommand(command: Int, data: Any?): Boolean {
        val msg = commandHandler.obtainMessage().apply {
            arg1 = command
            obj = data
        }
        val result = commandHandler.sendMessage(msg)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT && command == COMMAND_CHANGE_WINDOW_STYLE) {
            var shouldWait = false

            if (data is Int) {
                val display = (getSystemService(Context.WINDOW_SERVICE) as WindowManager).defaultDisplay
                val realMetrics = DisplayMetrics()
                display.getRealMetrics(realMetrics)

                val surface = mSurface
                val fullscreenLayout = surface != null &&
                    realMetrics.widthPixels == surface.width &&
                    realMetrics.heightPixels == surface.height

                shouldWait = if (data == 1) {
                    !fullscreenLayout
                } else {
                    fullscreenLayout
                }
            }

            val context = getContext()
            if (shouldWait) {
                synchronized(context) {
                    try {
                        (context as java.lang.Object).wait(500)
                    } catch (ie: InterruptedException) {
                        ie.printStackTrace()
                    }
                }
            }
        }

        return result
    }

    var commandHandler: Handler = SDLCommandHandler()

    fun setOrientationBis(w: Int, h: Int, resizable: Boolean, hint: String) {
        var orientationLandscape = -1
        var orientationPortrait = -1

        if (w <= 1 || h <= 1) {
            return
        }

        if (hint.contains("LandscapeRight") && hint.contains("LandscapeLeft")) {
            orientationLandscape = ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
        } else if (hint.contains("LandscapeLeft")) {
            orientationLandscape = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else if (hint.contains("LandscapeRight")) {
            orientationLandscape = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
        }

        val containsPortrait = hint.contains("Portrait ") || hint.endsWith("Portrait")
        if (containsPortrait && hint.contains("PortraitUpsideDown")) {
            orientationPortrait = ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
        } else if (containsPortrait) {
            orientationPortrait = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        } else if (hint.contains("PortraitUpsideDown")) {
            orientationPortrait = ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT
        }

        val isLandscapeAllowed = orientationLandscape != -1
        val isPortraitAllowed = orientationPortrait != -1

        val requestedOrientation = if (!isPortraitAllowed && !isLandscapeAllowed) {
            if (resizable) {
                ActivityInfo.SCREEN_ORIENTATION_FULL_USER
            } else if (w > h) {
                ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            } else {
                ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
            }
        } else if (resizable) {
            if (isPortraitAllowed && isLandscapeAllowed) {
                ActivityInfo.SCREEN_ORIENTATION_FULL_USER
            } else {
                if (isLandscapeAllowed) orientationLandscape else orientationPortrait
            }
        } else if (isPortraitAllowed && isLandscapeAllowed) {
            if (w > h) orientationLandscape else orientationPortrait
        } else {
            if (isLandscapeAllowed) orientationLandscape else orientationPortrait
        }

        Log.v(TAG, "setOrientation() requestedOrientation=$requestedOrientation width=$w height=$h resizable=$resizable hint=$hint")
        setRequestedOrientation(requestedOrientation)
    }

    protected val messageboxSelection = intArrayOf(0)

    fun messageboxShowMessageBox(
        flags: Int,
        title: String,
        message: String,
        buttonFlags: IntArray,
        buttonIds: IntArray,
        buttonTexts: Array<String>,
        colors: IntArray?,
    ): Int {
        messageboxSelection[0] = -1

        if (buttonFlags.size != buttonIds.size || buttonIds.size != buttonTexts.size) {
            return -1
        }

        val args = Bundle().apply {
            putInt("flags", flags)
            putString("title", title)
            putString("message", message)
            putIntArray("buttonFlags", buttonFlags)
            putIntArray("buttonIds", buttonIds)
            putStringArray("buttonTexts", buttonTexts)
            putIntArray("colors", colors)
        }

        runOnUiThread { messageboxCreateAndShow(args) }

        synchronized(messageboxSelection) {
            try {
                (messageboxSelection as java.lang.Object).wait()
            } catch (ex: InterruptedException) {
                ex.printStackTrace()
                return -1
            }
        }
        return messageboxSelection[0]
    }

    protected fun messageboxCreateAndShow(args: Bundle) {
        val colors = args.getIntArray("colors")
        val backgroundColor: Int
        val textColor: Int
        val buttonBorderColor: Int
        val buttonBackgroundColor: Int
        val buttonSelectedColor: Int

        if (colors != null) {
            var i = -1
            backgroundColor = colors[++i]
            textColor = colors[++i]
            buttonBorderColor = colors[++i]
            buttonBackgroundColor = colors[++i]
            buttonSelectedColor = colors[++i]
        } else {
            backgroundColor = Color.TRANSPARENT
            textColor = Color.TRANSPARENT
            buttonBorderColor = Color.TRANSPARENT
            buttonBackgroundColor = Color.TRANSPARENT
            buttonSelectedColor = Color.TRANSPARENT
        }

        val dialog = AlertDialog.Builder(this).create()
        dialog.setTitle(args.getString("title"))
        dialog.setCancelable(false)
        dialog.setOnDismissListener {
            synchronized(messageboxSelection) {
                (messageboxSelection as java.lang.Object).notify()
            }
        }

        val messageView = TextView(this).apply {
            gravity = Gravity.CENTER
            text = args.getString("message")
            if (textColor != Color.TRANSPARENT) {
                setTextColor(textColor)
            }
        }

        val buttonFlags = args.getIntArray("buttonFlags") ?: IntArray(0)
        val buttonIds = args.getIntArray("buttonIds") ?: IntArray(0)
        val buttonTexts = args.getStringArray("buttonTexts") ?: emptyArray()
        val mapping = SparseArray<Button>()

        val buttons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        for (i in buttonTexts.indices) {
            val id = buttonIds[i]
            val button = Button(this).apply {
                setOnClickListener {
                    messageboxSelection[0] = id
                    dialog.dismiss()
                }
                if (buttonFlags[i] != 0) {
                    if ((buttonFlags[i] and 0x00000001) != 0) {
                        mapping.put(KeyEvent.KEYCODE_ENTER, this)
                    }
                    if ((buttonFlags[i] and 0x00000002) != 0) {
                        mapping.put(KeyEvent.KEYCODE_ESCAPE, this)
                    }
                }
                text = buttonTexts[i]
                if (textColor != Color.TRANSPARENT) {
                    setTextColor(textColor)
                }
                if (buttonBackgroundColor != Color.TRANSPARENT) {
                    val drawable: Drawable? = background
                    if (drawable == null) {
                        setBackgroundColor(buttonBackgroundColor)
                    } else {
                        drawable.setColorFilter(buttonBackgroundColor, PorterDuff.Mode.MULTIPLY)
                    }
                }
            }

            if (buttonBorderColor != Color.TRANSPARENT) {

            }
            if (buttonSelectedColor != Color.TRANSPARENT) {
            }
            buttons.addView(button)
        }

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(messageView)
            addView(buttons)
            if (backgroundColor != Color.TRANSPARENT) {
                setBackgroundColor(backgroundColor)
            }
        }

        dialog.setView(content)
        dialog.setOnKeyListener { _: DialogInterface, keyCode: Int, event: KeyEvent ->
            val button = mapping[keyCode]
            if (button != null) {
                if (event.action == KeyEvent.ACTION_UP) {
                    button.performClick()
                }
                true
            } else {
                false
            }
        }
        dialog.show()
    }

    private val rehideSystemUi = Runnable {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            val flags = View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.INVISIBLE
            window.decorView.systemUiVisibility = flags
        }
    }

    override fun onSystemUiVisibilityChange(visibility: Int) {
        if (mFullscreenModeActive &&
            ((visibility and View.SYSTEM_UI_FLAG_FULLSCREEN) == 0 ||
                (visibility and View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0)
        ) {
            val handler = window.decorView.handler
            if (handler != null) {
                handler.removeCallbacks(rehideSystemUi)
                handler.postDelayed(rehideSystemUi, 2000)
            }
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
        val result = grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
        nativePermissionResult(requestCode, result)
    }

    private fun configurationLocale(configuration: Configuration): Locale? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            if (configuration.locales.isEmpty) null else configuration.locales[0]
        } else {
            configuration.locale
        }
    }

    data class SDLFileDialogState(
        var requestCode: Int,
        var multipleChoice: Boolean,
    )

    class SDLCommandHandler : Handler() {
        override fun handleMessage(msg: Message) {
            val context = SDL.getContext()
            if (context == null) {
                Log.e(TAG, "error handling message, getContext() returned null")
                return
            }

            when (msg.arg1) {
                COMMAND_CHANGE_TITLE -> {
                    if (context is Activity) {
                        context.title = msg.obj as String
                    } else {
                        Log.e(TAG, "error handling message, getContext() returned no Activity")
                    }
                }
                COMMAND_CHANGE_WINDOW_STYLE -> {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                        if (context is Activity) {
                            val window: Window? = context.window
                            if (window != null) {
                                if (msg.obj is Int && msg.obj as Int != 0) {
                                    val flags = View.SYSTEM_UI_FLAG_FULLSCREEN or
                                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                                        View.INVISIBLE
                                    window.decorView.systemUiVisibility = flags
                                    window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
                                    window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
                                    mFullscreenModeActive = true
                                } else {
                                    val flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_VISIBLE
                                    window.decorView.systemUiVisibility = flags
                                    window.addFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
                                    window.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
                                    mFullscreenModeActive = false
                                }
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                                    val attributes = window.attributes
                                    attributes.layoutInDisplayCutoutMode =
                                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                                    window.attributes = attributes
                                }
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
                                    Build.VERSION.SDK_INT < 35
                                ) {
                                    onNativeInsetsChanged(0, 0, 0, 0)
                                }
                            }
                        } else {
                            Log.e(TAG, "error handling message, getContext() returned no Activity")
                        }
                    }
                }
                COMMAND_TEXTEDIT_HIDE -> {
                    mTextEdit?.let { textEdit ->
                        textEdit.layoutParams = RelativeLayout.LayoutParams(0, 0)
                        val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
                        imm?.hideSoftInputFromWindow(textEdit.windowToken, 0)
                        mScreenKeyboardShown = false
                        mSurface?.requestFocus()
                    }
                }
                COMMAND_SET_KEEP_SCREEN_ON -> {
                    if (context is Activity) {
                        val window: Window? = context.window
                        if (window != null) {
                            if (msg.obj is Int && msg.obj as Int != 0) {
                                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                            } else {
                                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                            }
                        }
                    }
                }
                else -> {
                    if (context is SDLActivity && !context.onUnhandledMessage(msg.arg1, msg.obj)) {
                        Log.e(TAG, "error handling message, command is ${msg.arg1}")
                    }
                }
            }
        }
    }

    class ShowTextInputTask(
        var inputType: Int,
        var x: Int,
        var y: Int,
        w: Int,
        h: Int,
    ) : Runnable {
        companion object {
            private const val HEIGHT_PADDING = 15
        }

        var w: Int = if (w <= 0) 1 else w
        var h: Int = if (h + HEIGHT_PADDING <= 0) 1 - HEIGHT_PADDING else h

        override fun run() {
            val params = RelativeLayout.LayoutParams(w, h + HEIGHT_PADDING).apply {
                leftMargin = x
                topMargin = y
            }

            val textEdit = if (mTextEdit == null) {
                SDLDummyEdit(SDL.getContext()).also {
                    mTextEdit = it
                    mLayout?.addView(it, params)
                }
            } else {
                mTextEdit!!.apply {
                    layoutParams = params
                }
            }

            textEdit.setInputType(inputType)
            textEdit.visibility = View.VISIBLE
            textEdit.requestFocus()

            val imm = SDL.getContext().getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
            imm?.showSoftInput(textEdit, 0)
            mScreenKeyboardShown = true
        }
    }

    class SDLMain : Runnable {
        override fun run() {
            try {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY)
            } catch (e: Exception) {
                Log.v(TAG, "modify thread properties failed $e")
            }

            nativeInitMainThread()
            mSingleton?.main()
            nativeCleanupMainThread()

            val singleton = mSingleton
            if (singleton != null && !singleton.isFinishing) {
                mSDLThread = null
                mSDLMainFinished = true
                singleton.finish()
            }
        }
    }

    class SDLClipboardHandler : ClipboardManager.OnPrimaryClipChangedListener {
        protected val mClipMgr: ClipboardManager =
            SDL.getContext().getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager

        init {
            mClipMgr.addPrimaryClipChangedListener(this)
        }

        fun clipboardHasText(): Boolean {
            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                mClipMgr.hasPrimaryClip()
            } else {
                mClipMgr.hasText()
            }
        }

        fun clipboardGetText(): String? {
            val clip = mClipMgr.primaryClip
            if (clip != null) {
                val item = clip.getItemAt(0)
                val text = item?.text
                if (text != null) {
                    return text.toString()
                }
            }
            return null
        }

        fun clipboardSetText(string: String) {
            mClipMgr.removePrimaryClipChangedListener(this)
            if (string.isEmpty()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    mClipMgr.clearPrimaryClip()
                } else {
                    mClipMgr.setPrimaryClip(ClipData.newPlainText(null, ""))
                }
            } else {
                mClipMgr.setPrimaryClip(ClipData.newPlainText(null, string))
            }
            mClipMgr.addPrimaryClipChangedListener(this)
        }

        override fun onPrimaryClipChanged() {
            onNativeClipboardChanged()
        }
    }
}
