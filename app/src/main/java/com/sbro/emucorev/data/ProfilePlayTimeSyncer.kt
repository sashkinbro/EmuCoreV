package com.sbro.emucorev.data

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Bridges the local play-time store with the cloud profile: finished sessions are queued
 * persistently and pushed in batches when a player account is signed in.
 */
class ProfilePlayTimeSyncer(private val context: Context) {
    private val appContext = context.applicationContext
    private val cache = PlayTimeSyncCacheRepository(appContext)
    private val profileRepository = PlayerProfileRepository(appContext)
    private val catalogRepository = VitaCatalogRepository(appContext)

    suspend fun recordSession(titleId: String, title: String, durationMs: Long) {
        if (titleId.isBlank() || durationMs <= 0L) return
        val coverUrl = runCatching {
            catalogRepository.findBySerial(titleId)?.coverUrl
                ?: catalogRepository.findBestMatch(title)?.coverUrl
        }.getOrNull()
        cache.add(
            PlayerPlayTimeDelta(
                titleId = titleId,
                title = title,
                coverArtPath = coverUrl,
                durationMs = durationMs,
                sessionCount = 1L,
                lastPlayedAtMs = System.currentTimeMillis()
            )
        )
    }

    suspend fun syncPending() {
        if (!profileRepository.hasSignedInUser()) return
        val entries = cache.drain()
        if (entries.isEmpty()) return
        runCatching {
            profileRepository.recordPlayTimeBatch(entries)
            cache.setLastCloudSyncAtMs(System.currentTimeMillis())
        }.onFailure {
            cache.restore(entries)
        }
    }

    companion object {
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        /** Fire-and-forget entry point for emulation lifecycle callbacks. */
        fun recordAndSync(context: Context, titleId: String, title: String, durationMs: Long) {
            val applicationContext = context.applicationContext
            scope.launch {
                runCatching {
                    val syncer = ProfilePlayTimeSyncer(applicationContext)
                    syncer.recordSession(titleId, title, durationMs)
                    syncer.syncPending()
                }
            }
        }

        fun syncPendingAsync(context: Context) {
            val applicationContext = context.applicationContext
            scope.launch { runCatching { ProfilePlayTimeSyncer(applicationContext).syncPending() } }
        }
    }
}
