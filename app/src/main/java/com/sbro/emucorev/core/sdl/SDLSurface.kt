package com.sbro.emucorev.core.sdl

import android.content.Context
import android.content.pm.ActivityInfo
import android.graphics.Insets
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.util.DisplayMetrics
import android.util.Log
import android.view.Display
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.PointerIcon
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager

open class SDLSurface(context: Context) : SurfaceView(context),
    SurfaceHolder.Callback,
    View.OnApplyWindowInsetsListener,
    View.OnKeyListener,
    View.OnTouchListener,
    SensorEventListener {

    protected val mSensorManager: SensorManager =
        context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    protected val mDisplay: Display =
        (context.getSystemService(Context.WINDOW_SERVICE) as WindowManager).defaultDisplay

    protected var mWidth = 1.0f
    protected var mHeight = 1.0f
    @JvmField
    var mIsSurfaceReady = false

    init {
        holder.addCallback(this)
        isFocusable = true
        isFocusableInTouchMode = true
        requestFocus()
        setOnApplyWindowInsetsListener(this)
        setOnKeyListener(this)
        setOnTouchListener(this)
        setOnGenericMotionListener(SDLActivity.getMotionListener())
    }

    fun handlePause() {
        enableSensor(Sensor.TYPE_ACCELEROMETER, false)
    }

    fun handleResume() {
        isFocusable = true
        isFocusableInTouchMode = true
        requestFocus()
        setOnApplyWindowInsetsListener(this)
        setOnKeyListener(this)
        setOnTouchListener(this)
        enableSensor(Sensor.TYPE_ACCELEROMETER, true)
    }

    fun getNativeSurface(): Surface = holder.surface

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.v("SDL", "surfaceCreated()")
        if (SDLActivity.mHasNativeShutdown) {
            return
        }
        SDLActivity.onNativeSurfaceCreated()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.v("SDL", "surfaceDestroyed()")
        SDLActivity.mNextNativeState = SDLActivity.NativeState.PAUSED
        SDLActivity.handleNativeState()
        mIsSurfaceReady = false
        if (SDLActivity.mHasNativeShutdown) {
            return
        }
        SDLActivity.onNativeSurfaceDestroyed()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.v("SDL", "surfaceChanged()")

        if (SDLActivity.mSingleton == null || SDLActivity.mHasNativeShutdown) {
            return
        }

        mWidth = width.toFloat()
        mHeight = height.toFloat()
        var deviceWidth = width
        var deviceHeight = height
        var density = 1.0f
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
                val realMetrics = DisplayMetrics()
                mDisplay.getRealMetrics(realMetrics)
                deviceWidth = realMetrics.widthPixels
                deviceHeight = realMetrics.heightPixels
                density = realMetrics.densityDpi.toFloat() / 160.0f
            }
        } catch (_: Exception) {
        }

        val sdlContext = SDLActivity.getContext()
        synchronized(sdlContext) {
            (sdlContext as java.lang.Object).notifyAll()
        }

        Log.v("SDL", "Window size: ${width}x$height")
        Log.v("SDL", "Device size: ${deviceWidth}x$deviceHeight")
        SDLActivity.nativeSetScreenResolution(
            width,
            height,
            deviceWidth,
            deviceHeight,
            density,
            mDisplay.refreshRate
        )
        SDLActivity.onNativeResize()

        var skip = false
        val requestedOrientation = SDLActivity.mSingleton?.requestedOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT ||
            requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        ) {
            if (mWidth > mHeight) {
                skip = true
            }
        } else if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE ||
            requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        ) {
            if (mWidth < mHeight) {
                skip = true
            }
        }

        if (skip) {
            val min = minOf(mWidth, mHeight).toDouble()
            val max = maxOf(mWidth, mHeight).toDouble()
            if (max / min < 1.20) {
                Log.v("SDL", "Don't skip on such aspect-ratio. Could be a square resolution.")
                skip = false
            }
        }

        if (skip && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            skip = false
        }

        if (skip) {
            Log.v("SDL", "Skip .. Surface is not ready.")
            mIsSurfaceReady = false
            return
        }

        SDLActivity.onNativeSurfaceChanged()
        mIsSurfaceReady = true
        SDLActivity.mNextNativeState = SDLActivity.NativeState.RESUMED
        SDLActivity.handleNativeState()
    }

    override fun onApplyWindowInsets(v: View, insets: WindowInsets): WindowInsets {
        if (SDLActivity.mHasNativeShutdown) {
            return insets
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val combined = insets.getInsets(
                WindowInsets.Type.systemBars() or
                    WindowInsets.Type.systemGestures() or
                    WindowInsets.Type.mandatorySystemGestures() or
                    WindowInsets.Type.tappableElement() or
                    WindowInsets.Type.displayCutout()
            )
            SDLActivity.onNativeInsetsChanged(combined.left, combined.right, combined.top, combined.bottom)
        }
        return insets
    }

    override fun onKey(v: View, keyCode: Int, event: KeyEvent): Boolean {
        return SDLActivity.handleKeyEvent(v, keyCode, event, null)
    }

    private fun getNormalizedX(x: Float): Float {
        return if (mWidth <= 1f) 0.5f else x / (mWidth - 1f)
    }

    private fun getNormalizedY(y: Float): Float {
        return if (mHeight <= 1f) 0.5f else y / (mHeight - 1f)
    }

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        val touchDevId = event.deviceId
        val pointerCount = event.pointerCount
        val action = event.actionMasked
        var index = 0

        if (action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_POINTER_DOWN) {
            index = event.actionIndex
        }

        do {
            val toolType = event.getToolType(index)
            if (toolType == MotionEvent.TOOL_TYPE_MOUSE) {
                val buttonState = event.buttonState
                val motionListener = SDLActivity.getMotionListener()
                val x = motionListener.getEventX(event, index)
                val y = motionListener.getEventY(event, index)
                val relative = motionListener.inRelativeMode()
                SDLActivity.onNativeMouse(buttonState, action, x, y, relative)
            } else if (toolType == MotionEvent.TOOL_TYPE_STYLUS || toolType == MotionEvent.TOOL_TYPE_ERASER) {
                val pointerId = event.getPointerId(index)
                val x = event.getX(index)
                val y = event.getY(index)
                var pressure = event.getPressure(index)
                if (pressure > 1.0f) {
                    pressure = 1.0f
                }
                val buttonState = (event.buttonState shr 4) or
                    (1 shl if (toolType == MotionEvent.TOOL_TYPE_STYLUS) 0 else 30)
                SDLActivity.onNativePen(pointerId, buttonState, action, x, y, pressure)
            } else {
                val pointerId = event.getPointerId(index)
                val x = getNormalizedX(event.getX(index))
                val y = getNormalizedY(event.getY(index))
                var pressure = event.getPressure(index)
                if (pressure > 1.0f) {
                    pressure = 1.0f
                }
                SDLActivity.onNativeTouch(touchDevId, pointerId, action, x, y, pressure)
            }

            if (action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_POINTER_DOWN) {
                break
            }
        } while (++index < pointerCount)

        return true
    }

    fun enableSensor(sensorType: Int, enabled: Boolean) {
        if (enabled) {
            mSensorManager.registerListener(
                this,
                mSensorManager.getDefaultSensor(sensorType),
                SensorManager.SENSOR_DELAY_GAME,
                null
            )
        } else {
            mSensorManager.unregisterListener(this, mSensorManager.getDefaultSensor(sensorType))
        }
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type != Sensor.TYPE_ACCELEROMETER) {
            return
        }

        val x: Float
        val y: Float
        val newRotation: Int
        when (mDisplay.rotation) {
            Surface.ROTATION_90 -> {
                x = -event.values[1]
                y = event.values[0]
                newRotation = 90
            }
            Surface.ROTATION_180 -> {
                x = -event.values[0]
                y = -event.values[1]
                newRotation = 180
            }
            Surface.ROTATION_270 -> {
                x = event.values[1]
                y = -event.values[0]
                newRotation = 270
            }
            else -> {
                x = event.values[0]
                y = event.values[1]
                newRotation = 0
            }
        }

        if (newRotation != SDLActivity.mCurrentRotation) {
            SDLActivity.mCurrentRotation = newRotation
            SDLActivity.onNativeRotationChanged(newRotation)
        }

        SDLActivity.onNativeAccel(
            -x / SensorManager.GRAVITY_EARTH,
            y / SensorManager.GRAVITY_EARTH,
            event.values[2] / SensorManager.GRAVITY_EARTH
        )
    }

    override fun onResolvePointerIcon(event: MotionEvent, pointerIndex: Int): PointerIcon? {
        return try {
            super.onResolvePointerIcon(event, pointerIndex)
        } catch (_: NullPointerException) {
            null
        }
    }

    override fun onCapturedPointerEvent(event: MotionEvent): Boolean {
        var action = event.actionMasked
        val pointerCount = event.pointerCount

        repeat(pointerCount) { index ->
            when (action) {
                MotionEvent.ACTION_SCROLL -> {
                    val x = event.getAxisValue(MotionEvent.AXIS_HSCROLL, index)
                    val y = event.getAxisValue(MotionEvent.AXIS_VSCROLL, index)
                    SDLActivity.onNativeMouse(0, action, x, y, false)
                    return true
                }
                MotionEvent.ACTION_HOVER_MOVE, MotionEvent.ACTION_MOVE -> {
                    val x = event.getX(index)
                    val y = event.getY(index)
                    SDLActivity.onNativeMouse(0, action, x, y, true)
                    return true
                }
                MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_BUTTON_RELEASE -> {
                    action = if (action == MotionEvent.ACTION_BUTTON_PRESS) {
                        MotionEvent.ACTION_DOWN
                    } else {
                        MotionEvent.ACTION_UP
                    }
                    val x = event.getX(index)
                    val y = event.getY(index)
                    val button = event.buttonState
                    SDLActivity.onNativeMouse(button, action, x, y, true)
                    return true
                }
            }
        }

        return false
    }
}
