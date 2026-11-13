package com.sbro.emucorev.ui.library

import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.cancelAndJoin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryStartupCoverWarmupTest {
    @Test
    fun warmsOnlyTheFirst48FilteredSortedIdsOffMainAndBeforePublishing() = runBlocking {
        val items = (1..55).map { number ->
            game(number, title = "Keep ${number.toString().padStart(2, '0')}")
        } + game(99, title = "Exclude me")
        val events = mutableListOf<String>()
        val callerThread = Thread.currentThread()
        var requestedIds: List<String>? = null
        var requestedVersion: Long? = null
        val titleIds = startupVisibleTitleIds(
            allItems = items,
            query = "keep",
            sortOption = LibrarySortOption.TITLE_DESC,
            sortMetadata = emptyMap()
        )

        warmStartupCoverArtBeforePublish(
            titleIds = titleIds,
            artworkVersion = 17L,
            warmCached = { ids, version ->
                assertTrue("cache warmup must not run on the caller thread", Thread.currentThread() !== callerThread)
                requestedIds = ids
                requestedVersion = version
                events += "warm"
            },
            publish = { events += "publish" }
        )

        assertEquals((55 downTo 8).map { "ID-${it.toString().padStart(2, '0')}" }, requestedIds)
        assertEquals(17L, requestedVersion)
        assertEquals(listOf("warm", "publish"), events)
    }

    @Test
    fun cancellationDuringCacheWarmupPreventsFirstPublication() = runBlocking {
        var published = false
        val warmupStarted = CompletableDeferred<Unit>()
        val job = launch(Dispatchers.Default) {
            warmStartupCoverArtBeforePublish(
                titleIds = startupVisibleTitleIds(
                    allItems = listOf(game(1, "First")),
                    query = "",
                    sortOption = LibrarySortOption.TITLE_ASC,
                    sortMetadata = emptyMap()
                ),
                artworkVersion = 0L,
                warmCached = { _, _ ->
                    warmupStarted.complete(Unit)
                    awaitCancellation()
                },
                publish = { published = true }
            )
        }

        warmupStarted.await()
        job.cancelAndJoin()

        assertFalse(published)
    }

    @Test
    fun emptyVisibleListPublishesWithoutCallingCacheBoundary() = runBlocking {
        var warmWasCalled = false
        var published = false

        warmStartupCoverArtBeforePublish(
            titleIds = startupVisibleTitleIds(
                allItems = listOf(game(1, "Other")),
                query = "missing",
                sortOption = LibrarySortOption.TITLE_ASC,
                sortMetadata = emptyMap()
            ),
            artworkVersion = 3L,
            warmCached = { _, _ -> warmWasCalled = true },
            publish = { published = true }
        )

        assertFalse(warmWasCalled)
        assertTrue(published)
    }

    private fun game(number: Int, title: String) = InstalledVitaGame(
        titleId = "ID-${number.toString().padStart(2, '0')}",
        title = title,
        contentId = null,
        saveDataId = null,
        version = null,
        category = null,
        iconPath = null,
        installPath = "/synthetic/$number"
    )
}
