package com.sbro.emucorev.core

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.view.Surface
import android.view.WindowManager
import kotlin.math.PI

/**
 * Converts Android motion sensors into normalized analog-stick input.
 *
 * Aim uses angular velocity and targets the right stick. Steering uses the
 * game-rotation vector, centers itself at activation, and targets left-stick X.
 */
class AndroidGyroscopeInput(
    context: Context,
    private val onAnalog: (mode: Int, x: Float, y: Float) -> Unit
) : SensorEventListener {
    companion object {
        private const val INPUT_DEADZONE = 0.035f

        fun isModeAvailable(context: Context, mode: Int): Boolean {
            val manager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
            return when (mode) {
                VitaCoreConfig.GYRO_MODE_AIM ->
                    manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null
                VitaCoreConfig.GYRO_MODE_STEERING ->
                    manager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR) != null
                else -> true
            }
        }
    }

    private val appContext = context.applicationContext
    private val sensorManager =
        appContext.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val windowManager =
        appContext.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private var activeSensor: Sensor? = null
    private var mode = VitaCoreConfig.GYRO_MODE_OFF
    private var sensitivity = 1f
    private var smoothing = 0.45f
    private var invertX = false
    private var invertY = false
    private var filteredX = 0f
    private var filteredY = 0f
    private var lastSentX = 0f
    private var lastSentY = 0f
    private var wasActive = false
    private var steeringCenter = 0f
    private var hasSteeringCenter = false
    private var sampleX = 0f
    private var sampleY = 0f
    private val rotationMatrix = FloatArray(9)
    private val remappedRotationMatrix = FloatArray(9)
    private val orientationValues = FloatArray(3)

    fun start(
        mode: Int,
        sensitivityPercent: Int,
        smoothingPercent: Int,
        invertX: Boolean,
        invertY: Boolean
    ): Boolean {
        stop()
        this.mode = mode
        sensitivity = sensitivityPercent.coerceIn(25, 300) / 100f
        smoothing = smoothingPercent.coerceIn(0, 90) / 100f
        this.invertX = invertX
        this.invertY = invertY
        filteredX = 0f
        filteredY = 0f
        lastSentX = 0f
        lastSentY = 0f
        wasActive = false
        hasSteeringCenter = false
        activeSensor = when (mode) {
            VitaCoreConfig.GYRO_MODE_AIM ->
                sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
            VitaCoreConfig.GYRO_MODE_STEERING ->
                sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
            else -> null
        }
        val sensor = activeSensor ?: return false
        return sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
    }

    fun stop() {
        activeSensor?.let { sensorManager.unregisterListener(this, it) }
        activeSensor = null
        hasSteeringCenter = false
        filteredX = 0f
        filteredY = 0f
        wasActive = false
        onAnalog(mode, 0f, 0f)
    }

    override fun onSensorChanged(event: SensorEvent) {
        val validSample = when (mode) {
            VitaCoreConfig.GYRO_MODE_AIM -> readAimValues(event)
            VitaCoreConfig.GYRO_MODE_STEERING -> readSteeringValues(event)
            else -> false
        }
        if (!validSample) return
        var x = applyDeadzone(sampleX.coerceIn(-1f, 1f))
        var y = applyDeadzone(sampleY.coerceIn(-1f, 1f))
        if (invertX) x = -x
        if (invertY) y = -y
        val alpha = (1f - smoothing * 0.86f).coerceIn(0.16f, 1f)
        filteredX += (x - filteredX) * alpha
        filteredY += (y - filteredY) * alpha
        val outputX = filteredX.coerceIn(-1f, 1f)
        val outputY = filteredY.coerceIn(-1f, 1f)
        val active = kotlin.math.abs(outputX) > 0.004f ||
            kotlin.math.abs(outputY) > 0.004f
        if (!active && !wasActive) return
        if (active &&
            kotlin.math.abs(outputX - lastSentX) < 0.004f &&
            kotlin.math.abs(outputY - lastSentY) < 0.004f
        ) return
        val sentX = if (active) outputX else 0f
        val sentY = if (active) outputY else 0f
        lastSentX = sentX
        lastSentY = sentY
        wasActive = active
        onAnalog(mode, sentX, sentY)
    }

    private fun readAimValues(event: SensorEvent): Boolean {
        if (event.sensor.type != Sensor.TYPE_GYROSCOPE) return false
        val rotation = @Suppress("DEPRECATION") windowManager.defaultDisplay.rotation
        val gx = event.values[0]
        val gy = event.values[1]
        when (rotation) {
            Surface.ROTATION_90 -> {
                sampleX = gx
                sampleY = gy
            }
            Surface.ROTATION_270 -> {
                sampleX = -gx
                sampleY = -gy
            }
            Surface.ROTATION_180 -> {
                sampleX = gy
                sampleY = -gx
            }
            else -> {
                sampleX = -gy
                sampleY = -gx
            }
        }
        sampleX *= 0.72f * sensitivity
        sampleY *= 0.72f * sensitivity
        return true
    }

    private fun readSteeringValues(event: SensorEvent): Boolean {
        if (event.sensor.type != Sensor.TYPE_GAME_ROTATION_VECTOR) return false
        SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)
        val rotation = @Suppress("DEPRECATION") windowManager.defaultDisplay.rotation
        val axisX: Int
        val axisY: Int
        when (rotation) {
            Surface.ROTATION_90 -> {
                axisX = SensorManager.AXIS_Y
                axisY = SensorManager.AXIS_MINUS_X
            }
            Surface.ROTATION_180 -> {
                axisX = SensorManager.AXIS_MINUS_X
                axisY = SensorManager.AXIS_MINUS_Y
            }
            Surface.ROTATION_270 -> {
                axisX = SensorManager.AXIS_MINUS_Y
                axisY = SensorManager.AXIS_X
            }
            else -> {
                axisX = SensorManager.AXIS_X
                axisY = SensorManager.AXIS_Y
            }
        }
        SensorManager.remapCoordinateSystem(
            rotationMatrix,
            axisX,
            axisY,
            remappedRotationMatrix
        )
        val roll = SensorManager.getOrientation(
            remappedRotationMatrix,
            orientationValues
        )[2]
        if (!hasSteeringCenter) {
            steeringCenter = roll
            hasSteeringCenter = true
        }
        var delta = roll - steeringCenter
        while (delta > PI) delta -= (2 * PI).toFloat()
        while (delta < -PI) delta += (2 * PI).toFloat()
        val steeringRange = Math.toRadians(32.0).toFloat()
        sampleX = (delta / steeringRange * sensitivity).coerceIn(-1f, 1f)
        sampleY = 0f
        return true
    }

    private fun applyDeadzone(value: Float): Float {
        val magnitude = kotlin.math.abs(value)
        if (magnitude <= INPUT_DEADZONE) return 0f
        return kotlin.math.sign(value) *
            ((magnitude - INPUT_DEADZONE) / (1f - INPUT_DEADZONE))
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
