package com.sbro.emucorev.core

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlin.math.log10
import kotlin.math.sqrt

enum class MicDeviceKind { BUILT_IN, USB, BLUETOOTH, WIRED, OTHER }

/** An input device of this phone the emulated microphone can record from. */
data class MicInputDevice(val id: Int, val type: Int, val name: String) {
    /** Stable across reconnects (the numeric id is not): "<AudioDeviceInfo type>:<product name>". */
    val key: String get() = micDeviceKey(type, name)
    val kind: MicDeviceKind get() = micDeviceKind(type)
}

fun micDeviceKey(type: Int, name: String): String = "$type:$name"

// AudioDeviceInfo.TYPE_* values, spelled out so the mapping is testable on the JVM.
private const val TYPE_WIRED_HEADSET = 3
private const val TYPE_BLUETOOTH_SCO = 7
private const val TYPE_USB_DEVICE = 11
private const val TYPE_BUILTIN_MIC = 15
private const val TYPE_USB_HEADSET = 22
private const val TYPE_HEARING_AID = 23
private const val TYPE_BLE_HEADSET = 26

fun micDeviceKind(type: Int): MicDeviceKind = when (type) {
    TYPE_BUILTIN_MIC -> MicDeviceKind.BUILT_IN
    TYPE_USB_DEVICE, TYPE_USB_HEADSET -> MicDeviceKind.USB
    TYPE_BLUETOOTH_SCO, TYPE_BLE_HEADSET, TYPE_HEARING_AID -> MicDeviceKind.BLUETOOTH
    TYPE_WIRED_HEADSET -> MicDeviceKind.WIRED
    else -> MicDeviceKind.OTHER
}

/** Types offered in the picker; telephony, FM and submix style sources are not real microphones. */
fun isSelectableMicType(type: Int): Boolean = micDeviceKind(type) != MicDeviceKind.OTHER

/** Current id of the saved device key, or 0 (system default) when it is blank or not connected. */
fun resolveMicDeviceId(key: String, devices: List<MicInputDevice>): Int =
    if (key.isBlank()) 0 else devices.firstOrNull { it.key == key }?.id ?: 0

/** 0..1 meter value of 16-bit samples (RMS in dBFS mapped from -60 dB..0 dB), with the user gain applied. */
fun micLevel(samples: ShortArray, count: Int, gainPercent: Int = 100): Float {
    val n = count.coerceIn(0, samples.size)
    if (n == 0) return 0f
    var sum = 0.0
    for (i in 0 until n) {
        val v = samples[i] * gainPercent.coerceIn(MIC_GAIN_MIN, MIC_GAIN_MAX) / 100.0
        val clipped = v.coerceIn(-32768.0, 32767.0)
        sum += clipped * clipped
    }
    val rms = sqrt(sum / n) / 32768.0
    if (rms <= 0.0) return 0f
    return ((20.0 * log10(rms) + 60.0) / 60.0).coerceIn(0.0, 1.0).toFloat()
}

object MicrophoneSupport {
    /** True while this app routed a Bluetooth headset microphone; only then is the route cleared again. */
    @Volatile
    private var bluetoothRouted = false

    fun hasPermission(context: Context): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    fun listInputDevices(context: Context): List<MicInputDevice> {
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return emptyList()
        return runCatching {
            manager.getDevices(AudioManager.GET_DEVICES_INPUTS)
                .filter { it.isSource && isSelectableMicType(it.type) }
                .map { MicInputDevice(it.id, it.type, it.productName?.toString().orEmpty()) }
                .distinctBy { it.key }
        }.getOrDefault(emptyList())
    }

    fun deviceInfo(context: Context, id: Int): AudioDeviceInfo? {
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return null
        return runCatching { manager.getDevices(AudioManager.GET_DEVICES_INPUTS).firstOrNull { it.id == id } }.getOrNull()
    }

    /**
     * Live input level for the Settings test meter. Collecting starts a short-lived AudioRecord on the
     * chosen device; cancelling the collection stops and releases it. Requires RECORD_AUDIO.
     */
    @SuppressLint("MissingPermission")
    fun levels(context: Context, deviceId: Int, gainPercent: () -> Int): Flow<Float> = flow {
        if (!hasPermission(context)) return@flow
        val sampleRate = 16000
        val buffer = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        if (buffer <= 0) return@flow
        val record = runCatching {
            AudioRecord(
                MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT, buffer * 2
            )
        }.getOrNull() ?: return@flow
        try {
            if (record.state != AudioRecord.STATE_INITIALIZED) return@flow
            if (deviceId > 0) deviceInfo(context, deviceId)?.let { record.preferredDevice = it }
            record.startRecording()
            val samples = ShortArray(sampleRate / 20) // 50 ms
            while (true) {
                val read = record.read(samples, 0, samples.size)
                if (read < 0) break
                emit(micLevel(samples, read, gainPercent()))
            }
        } finally {
            runCatching { record.stop() }
            record.release()
        }
    }.flowOn(Dispatchers.IO)

    /**
     * Bluetooth headset microphones only deliver audio while the SCO / communication link is routed to
     * them; wired and built-in inputs need nothing. Best effort and reversible via [clearRouting].
     */
    @Suppress("DEPRECATION")
    fun applyRouting(context: Context, device: AudioDeviceInfo?) {
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        runCatching {
            if (device != null && micDeviceKind(device.type) == MicDeviceKind.BLUETOOTH) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) manager.setCommunicationDevice(device)
                else manager.startBluetoothSco()
                bluetoothRouted = true
            } else {
                clearRouting(context)
            }
        }
    }

    @Suppress("DEPRECATION")
    fun clearRouting(context: Context) {
        if (!bluetoothRouted) return
        bluetoothRouted = false
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) manager.clearCommunicationDevice()
            else manager.stopBluetoothSco()
        }
    }
}
