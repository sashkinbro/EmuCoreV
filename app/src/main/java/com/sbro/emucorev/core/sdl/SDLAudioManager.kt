package com.sbro.emucorev.core.sdl

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.util.Log

class SDLAudioManager {
    companion object {
        private const val TAG = "SDLAudio"

        private var mContext: Context? = null
        private var mAudioDeviceCallback: AudioDeviceCallback? = null

        @JvmStatic
        fun initialize() {
            mAudioDeviceCallback = null

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                mAudioDeviceCallback = object : AudioDeviceCallback() {
                    override fun onAudioDevicesAdded(addedDevices: Array<AudioDeviceInfo>) {
                        addedDevices.forEach { deviceInfo ->
                            addAudioDevice(deviceInfo.isSink, deviceInfo.productName.toString(), deviceInfo.id)
                        }
                    }

                    override fun onAudioDevicesRemoved(removedDevices: Array<AudioDeviceInfo>) {
                        removedDevices.forEach { deviceInfo ->
                            removeAudioDevice(deviceInfo.isSink, deviceInfo.id)
                        }
                    }
                }
            }
        }

        @JvmStatic
        fun setContext(context: Context?) {
            mContext = context
        }

        @JvmStatic
        fun release(context: Context?) {
            // no-op atm
        }

        private fun getAudioManager(): AudioManager? {
            return mContext?.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        }

        @Suppress("unused")
        private fun getInputAudioDeviceInfo(deviceId: Int): AudioDeviceInfo? {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                return null
            }
            return getAudioManager()
                ?.getDevices(AudioManager.GET_DEVICES_INPUTS)
                ?.firstOrNull { it.id == deviceId }
        }

        @Suppress("unused")
        private fun getPlaybackAudioDeviceInfo(deviceId: Int): AudioDeviceInfo? {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                return null
            }
            return getAudioManager()
                ?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
                ?.firstOrNull { it.id == deviceId }
        }

        @JvmStatic
        fun registerAudioDeviceCallback() {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                return
            }
            val audioManager = getAudioManager() ?: return
            val callback = mAudioDeviceCallback ?: return

            audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).forEach { device ->
                if (device.type == AudioDeviceInfo.TYPE_TELEPHONY) {
                    return@forEach
                }
                addAudioDevice(device.isSink, device.productName.toString(), device.id)
            }
            audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS).forEach { device ->
                addAudioDevice(device.isSink, device.productName.toString(), device.id)
            }
            audioManager.registerAudioDeviceCallback(callback, null)
        }

        @JvmStatic
        fun unregisterAudioDeviceCallback() {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                return
            }
            val audioManager = getAudioManager() ?: return
            val callback = mAudioDeviceCallback ?: return
            audioManager.unregisterAudioDeviceCallback(callback)
        }

        /** This method is called by SDL using JNI. */
        @JvmStatic
        fun audioSetThreadPriority(recording: Boolean, deviceId: Int) {
            try {
                Thread.currentThread().name = if (recording) {
                    "SDLAudioC$deviceId"
                } else {
                    "SDLAudioP$deviceId"
                }
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO)
            } catch (exception: Exception) {
                Log.v(TAG, "modify thread properties failed $exception")
            }
        }

        @JvmStatic
        external fun nativeSetupJNI(): Int

        @JvmStatic
        external fun removeAudioDevice(recording: Boolean, deviceId: Int)

        @JvmStatic
        external fun addAudioDevice(recording: Boolean, name: String, deviceId: Int)
    }
}
