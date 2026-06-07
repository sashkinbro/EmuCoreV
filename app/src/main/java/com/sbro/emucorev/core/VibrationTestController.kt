@file:Suppress("DEPRECATION")

package com.sbro.emucorev.core

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.InputDevice
import com.sbro.emucorev.core.input.InputDeviceClassifier
import kotlin.math.roundToInt

object VibrationTestController {
    private const val TEST_PULSE_MS = 160

    fun playTestPulse(context: Context, config: VitaCoreConfig): Boolean {
        if (!config.gamepadVibration) return false
        val intensity = config.gamepadVibrationStrength.toIntensity()
        if (intensity <= 0f) return false

        val gamepadHandled = vibratePhysicalGamepads(intensity, TEST_PULSE_MS)
        if (gamepadHandled) return true
        return config.deviceVibrationFallback && vibrateDevice(context.applicationContext, intensity, TEST_PULSE_MS)
    }

    private fun vibratePhysicalGamepads(intensity: Float, lengthMs: Int): Boolean {
        var handled = false
        InputDevice.getDeviceIds().forEach { deviceId ->
            val device = InputDevice.getDevice(deviceId) ?: return@forEach
            if (!InputDeviceClassifier.isPhysicalGameController(device)) return@forEach
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val manager = device.vibratorManager
                manager.vibratorIds.forEach { vibratorId ->
                    handled = vibrate(manager.getVibrator(vibratorId), intensity, lengthMs) || handled
                }
            } else {
                handled = vibrate(device.vibrator, intensity, lengthMs) || handled
            }
        }
        return handled
    }

    private fun vibrateDevice(context: Context, intensity: Float, lengthMs: Int): Boolean {
        val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)?.defaultVibrator
        } else {
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        } ?: return false
        return vibrate(vibrator, intensity, lengthMs)
    }

    private fun vibrate(vibrator: Vibrator, intensity: Float, lengthMs: Int): Boolean {
        if (!vibrator.hasVibrator()) return false
        val amplitude = (intensity.coerceIn(0f, 1f) * 255f).roundToInt().coerceIn(1, 255)
        return runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(VibrationEffect.createOneShot(lengthMs.toLong(), amplitude))
            } else {
                vibrator.vibrate(lengthMs.toLong())
            }
        }.isSuccess
    }

    private fun Int.toIntensity(): Float = coerceIn(0, 100) / 100f
}
