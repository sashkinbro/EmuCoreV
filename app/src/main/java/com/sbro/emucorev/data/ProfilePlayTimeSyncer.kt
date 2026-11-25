package com.sbro.emucorev.data

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/** The cloud side of [ProfilePlayTimeSyncer]; a seam so the queue logic can be tested without Firebase. */
interface PlayTimeBackend {
    fun currentUid(): String?

    /**
     * Applies [entries] to the profile of [uid] exactly once for [batchId]; repeating a batch id that
     * was already applied must succeed without counting it again. Must fail if [uid] is no longer
     * the signed-in account.
     */
    suspend fun commitBatch(uid: String, batchId: String, entries: List<PlayerPlayTimeDelta>)
}

/**
 * Bridges the local play-time store with the cloud profile: finished sessions are queued
 * persistently per account and pushed in batches when that account is signed in.
 */
class ProfilePlayTimeSyncer internal constructor(
    private val cache: PlayTimeSyncCacheRepository,
    private val backend: PlayTimeBackend,
    private val coverUrlFor: suspend (String) -> String?
) {
    constructor(context: Context) : this(
        PlayTimeSyncCacheRepository(context.applicationContext),
        PlayerProfilePlayTimeBackend(PlayerProfileRepository(context.applicationContext)),
        coverUrlLookup(CoverArtRepository.get(context.applicationContext))
    )

    suspend fun recordSession(titleId: String, title: String, durationMs: Long) {
        if (titleId.isBlank() || durationMs <= 0L) return
        // A session belongs to the account that was signed in while it was played (B062). Signed-out
        // play time is not queued, so it can never be credited to whoever signs in later.
        val uid = backend.currentUid() ?: return
        val coverUrl = runCatchingCancellable { coverUrlFor(titleId) }.getOrNull()
        cache.add(
            PlayerPlayTimeDelta(
                titleId = titleId,
                title = title,
                coverArtPath = coverUrl,
                durationMs = durationMs,
                sessionCount = 1L,
                lastPlayedAtMs = System.currentTimeMillis(),
                uid = uid
            )
        )
    }

    /** Uploads the signed-in account's queue: peek -> commit -> remove (B063). */
    suspend fun syncPending() {
        val uid = backend.currentUid() ?: return
        repeat(MAX_BATCHES_PER_SYNC) {
            val batch = cache.peekBatch(uid) ?: return
            val committed = runCatchingCancellable {
                backend.commitBatch(uid, batch.id, batch.entries)
            }.isSuccess
            // On failure the sealed batch stays queued under the same id and is retried unchanged.
            if (!committed) return
            cache.removeBatch(batch.id)
            cache.setLastCloudSyncAtMs(System.currentTimeMillis())
        }
    }

    /** Forgets everything queued for [uid]; used when the account is deleted. */
    suspend fun discardAccount(uid: String) = cache.discardAccount(uid)

    private class PlayerProfilePlayTimeBackend(private val repository: PlayerProfileRepository) : PlayTimeBackend {
        override fun currentUid(): String? = repository.currentUid()

        override suspend fun commitBatch(uid: String, batchId: String, entries: List<PlayerPlayTimeDelta>) {
            repository.recordPlayTimeBatch(uid, batchId, entries)
        }
    }

    companion object {
        private fun coverUrlLookup(covers: CoverArtRepository): suspend (String) -> String? =
            { titleId -> covers.buildPublicCoverUrl(titleId = titleId) }

        private const val MAX_BATCHES_PER_SYNC = 3
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        /** Fire-and-forget entry point for emulation lifecycle callbacks. */
        fun recordAndSync(context: Context, titleId: String, title: String, durationMs: Long) {
            val applicationContext = context.applicationContext
            scope.launch {
                runCatchingCancellable {
                    val syncer = ProfilePlayTimeSyncer(applicationContext)
                    syncer.recordSession(titleId, title, durationMs)
                    syncer.syncPending()
                }
            }
        }

        fun syncPendingAsync(context: Context) {
            val applicationContext = context.applicationContext
            scope.launch { runCatchingCancellable { ProfilePlayTimeSyncer(applicationContext).syncPending() } }
        }
    }
}
