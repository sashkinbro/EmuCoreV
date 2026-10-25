package com.sbro.emucorev.ui.library

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import com.sbro.emucorev.ui.common.localBitmapLoader
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map

@Composable
internal fun LibraryCoverPrefetch(
    gridState: LazyListState,
    window: LibraryCoverPrefetchWindow,
    enabled: Boolean,
    artworkVersion: Long,
    // The same integration can be checked with a deterministic image loader.
    load: (suspend (String) -> Unit)? = null
) {
    LaunchedEffect(gridState, window, enabled, artworkVersion, load) {
        if (!enabled) return@LaunchedEffect
        var previousIndex = gridState.firstVisibleItemIndex
        var previousOffset = gridState.firstVisibleItemScrollOffset
        var forward = true
        snapshotFlow {
            Triple(
                gridState.layoutInfo.visibleItemsInfo.map { it.key.toString() },
                gridState.firstVisibleItemIndex,
                gridState.firstVisibleItemScrollOffset
            )
        }.map { (keys, index, offset) ->
            if (index != previousIndex || offset != previousOffset) {
                forward = index > previousIndex || (index == previousIndex && offset > previousOffset)
                previousIndex = index
                previousOffset = offset
            }
            window.paths(keys, forward)
        }.distinctUntilChanged().collectLatest { paths ->
            // Sequential speculation leaves the other loader permits for visible cards.
            // A direction/filter change cancels this queue instead of enqueuing a library.
            for (path in paths) {
                currentCoroutineContext().ensureActive()
                if (load != null) load(path) else localBitmapLoader.prefetch(path, artworkVersion)
            }
        }
    }
}
