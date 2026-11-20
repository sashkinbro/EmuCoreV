package com.sbro.emucorev.nativeui

import android.media.AudioAttributes
import android.media.SoundPool
import java.io.File

/** Plays PS Vita UI PCM16 WAV effects packaged in `audio/paf-common-resource/`. */
class NativeUiSoundBank(resources: File) {
    private val pool: SoundPool
    private val soundIds = HashMap<Int, Int>(5)
    private val loaded = HashSet<Int>(5)
    @Volatile private var released = false

    init {
        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        pool = SoundPool.Builder()
            .setMaxStreams(6)
            .setAudioAttributes(attrs)
            .build()
        pool.setOnLoadCompleteListener { _, sampleId, status ->
            if (status == 0) {
                synchronized(loaded) { loaded.add(sampleId) }
            }
        }
        val files = listOf(
            SOUND_DECIDE to "audio/paf-common-resource/48d64cf1.wav",
            SOUND_CANCEL to "audio/paf-common-resource/5c756da0.wav",
            SOUND_FOCUS to "audio/paf-common-resource/672a5e85.wav",
            SOUND_PAGE_SCROLL to "audio/paf-common-resource/857299df.wav",
            SOUND_PAGE_TURN to "audio/paf-common-resource/e0d9f918.wav",
        )
        for ((key, rel) in files) {
            val file = File(resources, rel)
            if (file.isFile) {
                val sampleId = pool.load(file.absolutePath, 1)
                if (sampleId != 0) soundIds[key] = sampleId
            }
        }
    }

    fun play(soundKey: Int, volume: Float = 0.72f) {
        if (released) return
        val sampleId = soundIds[soundKey] ?: return
        val isReady = synchronized(loaded) { sampleId in loaded }
        if (isReady) {
            pool.play(sampleId, volume, volume, 1, 0, 1.0f)
        }
    }

    fun onPause() {
        if (!released) pool.autoPause()
    }

    fun onResume() {
        if (!released) pool.autoResume()
    }

    fun release() {
        released = true
        pool.release()
    }

    companion object {
        const val SOUND_DECIDE = 1
        const val SOUND_CANCEL = 2
        const val SOUND_FOCUS = 3
        const val SOUND_PAGE_SCROLL = 4
        const val SOUND_PAGE_TURN = 5
    }
}
