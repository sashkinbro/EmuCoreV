package com.sbro.emucorev.ui.common

import com.sbro.emucorev.data.VitaCoverResolver
import com.sbro.emucorev.data.VitaCoverIndex
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit

/** Local image decoding and managed requests have separate bounded admission. */
internal class CoverLoadCoordinator {
    private val managedLoads = Semaphore(4)
    private val localDecodes = Semaphore(4)
    private val speculativeLoads = Semaphore(1)

    suspend fun loadVisible(
        managedArtwork: suspend () -> Unit,
        localCustom: suspend () -> Boolean = { false },
        cachedArtwork: suspend () -> Boolean = { false }
    ) {
        if (decode(localCustom)) return
        if (decode(cachedArtwork)) return
        managedLoads.withPermit { managedArtwork() }
    }

    suspend fun <T> decode(block: suspend () -> T): T = localDecodes.withPermit { block() }

    suspend fun prefetch(block: suspend () -> Unit) {
        if (!speculativeLoads.tryAcquire()) return
        try {
            if (!managedLoads.tryAcquire()) return
            try { block() } finally { managedLoads.release() }
        } finally {
            speculativeLoads.release()
        }
    }

    companion object {
        fun isLocalCustomCandidate(path: String): Boolean =
            !VitaCoverIndex.isManagedRemoteUrl(path) &&
                (path.startsWith("content://") || VitaCoverResolver.localFile(path) != null)
    }
}
