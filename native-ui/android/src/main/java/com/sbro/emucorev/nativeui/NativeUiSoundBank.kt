package com.sbro.emucorev.nativeui

import android.media.AudioAttributes
import android.media.SoundPool
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.os.SystemClock
import java.io.File
import java.io.RandomAccessFile

/**
 * Plays PS Vita UI PCM16 WAV effects packaged in `audio/paf-common-resource/`.
 *
 * [play] is called from the render thread on every focus move and tap, so it only posts to a
 * dedicated audio-priority thread: starting a SoundPool stream creates an AudioTrack, which
 * blocks for milliseconds and used to stall frames (and starve the mixer) under rapid input.
 * [SoundVoicePolicy] keeps overlapping effects from clipping or stealing each other's stream.
 */
class NativeUiSoundBank(resources: File) {
    private val pool: SoundPool
    private val soundIds = HashMap<Int, Int>(8)
    private val loaded = HashSet<Int>(8)
    private val thread = HandlerThread("native-ui-sound", Process.THREAD_PRIORITY_URGENT_AUDIO).apply { start() }
    private val handler = Handler(thread.looper)
    private val policy: SoundVoicePolicy
    // One preallocated request per effect: a play() is a message post, not an allocation.
    private val requests = HashMap<Int, Runnable>(8)
    @Volatile private var released = false

    init {
        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        pool = SoundPool.Builder()
            // The policy caps simultaneous voices; headroom here keeps SoundPool from ever stealing one.
            .setMaxStreams(SoundVoicePolicy.DEFAULT_MAX_VOICES * 2)
            .setAudioAttributes(attrs)
            .build()
        pool.setOnLoadCompleteListener { _, sampleId, status ->
            if (status == 0) {
                synchronized(loaded) { loaded.add(sampleId) }
            }
        }
        val durations = HashMap<Int, Int>(8)
        for ((key, rel) in FILES) {
            val file = File(resources, rel)
            if (!file.isFile) continue
            val sampleId = pool.load(file.absolutePath, 1)
            if (sampleId == 0) continue
            soundIds[key] = sampleId
            durations[key] = wavDurationMs(file)
            requests[key] = Runnable { startVoice(key) }
        }
        policy = SoundVoicePolicy(durations)
    }

    /** Never blocks: queues the effect for the sound thread. */
    fun play(soundKey: Int) {
        if (released) return
        val request = requests[soundKey] ?: return
        handler.post(request)
    }

    // Runs on the sound thread.
    private fun startVoice(soundKey: Int) {
        if (released) return
        val sampleId = soundIds[soundKey] ?: return
        if (!synchronized(loaded) { sampleId in loaded }) return
        if (!policy.accept(soundKey, SystemClock.uptimeMillis())) return
        val volume = policy.volume
        pool.play(sampleId, volume, volume, 1, 0, 1.0f)
    }

    fun onPause() {
        if (!released) handler.post { if (!released) pool.autoPause() }
    }

    fun onResume() {
        if (!released) handler.post { if (!released) pool.autoResume() }
    }

    fun release() {
        if (released) return
        released = true
        handler.removeCallbacksAndMessages(null)
        // Last message on the sound thread: nothing can start a voice on a released pool.
        handler.post {
            pool.release()
            thread.quitSafely()
        }
    }

    companion object {
        const val SOUND_DECIDE = 1
        const val SOUND_CANCEL = 2
        const val SOUND_FOCUS = 3
        const val SOUND_PAGE_SCROLL = 4
        const val SOUND_PAGE_TURN = 5
        private const val FALLBACK_DURATION_MS = 120
        private val FILES = listOf(
            SOUND_DECIDE to "audio/paf-common-resource/48d64cf1.wav",
            SOUND_CANCEL to "audio/paf-common-resource/5c756da0.wav",
            SOUND_FOCUS to "audio/paf-common-resource/672a5e85.wav",
            SOUND_PAGE_SCROLL to "audio/paf-common-resource/857299df.wav",
            SOUND_PAGE_TURN to "audio/paf-common-resource/e0d9f918.wav",
        )

        /** Length of a canonical RIFF/WAVE file from its header, or a short default. */
        fun wavDurationMs(file: File): Int = runCatching {
            RandomAccessFile(file, "r").use { wav ->
                val header = ByteArray(44)
                wav.readFully(header)
                fun le32(at: Int) = (header[at].toInt() and 0xff) or ((header[at + 1].toInt() and 0xff) shl 8) or
                    ((header[at + 2].toInt() and 0xff) shl 16) or ((header[at + 3].toInt() and 0xff) shl 24)
                val byteRate = le32(28)
                val dataBytes = le32(40)
                if (String(header, 36, 4, Charsets.US_ASCII) != "data" || byteRate <= 0 || dataBytes <= 0) FALLBACK_DURATION_MS
                else (dataBytes.toLong() * 1000L / byteRate).toInt().coerceIn(10, 2000)
            }
        }.getOrDefault(FALLBACK_DURATION_MS)
    }
}
