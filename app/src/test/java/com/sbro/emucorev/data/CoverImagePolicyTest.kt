package com.sbro.emucorev.data

import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.collect

class CoverImagePolicyTest {
    @Test fun preservesLowResolutionSources() {
        assertEquals(1, CoverImagePolicy.sampleSize(320, 480))
        assertEquals(1, CoverImagePolicy.sampleSize(400, 600))
        assertEquals(1, CoverImagePolicy.sampleSize(720, 1080, 720, 1080))
    }
    @Test fun bothPortraitAndLandscapeDecodeWithinBudget() {
        for ((width, height) in listOf(1441 to 2161, 8000 to 6000, 6000 to 8000, 16384 to 1, 1 to 16384)) {
            val sample = CoverImagePolicy.sampleSize(width, height)!!
            assertTrue((width + sample - 1) / sample <= CoverImagePolicy.DECODE_WIDTH)
            assertTrue((height + sample - 1) / sample <= CoverImagePolicy.DECODE_HEIGHT)
        }
    }
    @Test fun visibleGridFitsMemoryWhileLargeArtworkRetainsItsResolution() {
        assertTrue(48L * CoverImagePolicy.DECODE_WIDTH * CoverImagePolicy.DECODE_HEIGHT * 4 <= CoverImagePolicy.MEMORY_CACHE_BYTES)
        assertEquals(400, CoverImagePolicy.decodeWidthForCanvas(224))
        assertEquals(400, CoverImagePolicy.decodeWidthForCanvas(400))
        assertEquals(720, CoverImagePolicy.decodeWidthForCanvas(620))
        assertEquals(1, CoverImagePolicy.sampleSize(720, 1080, 720, 1080))
    }
    @Test fun invalidAndExtremeImageBoundsAreRejected() {
        for ((width, height) in listOf(0 to 100, -1 to 1, 17000 to 100, 100 to Int.MAX_VALUE, 16000 to 16000)) {
            assertNull(CoverImagePolicy.sampleSize(width, height))
            assertFalse(CoverImagePolicy.validBounds(width, height))
        }
    }

    @Test fun memoryClearRejectsOldDecodeAndDoesNotEraseNewGenerationImage() = runBlocking {
        val generation = CoverImageCacheGeneration()
        val images = mutableMapOf<String, String>()
        val oldVersion = generation.version.value
        val started = CompletableDeferred<Unit>(); val finish = CompletableDeferred<Unit>()
        val oldDecode = async(start = CoroutineStart.UNDISPATCHED) {
            started.complete(Unit); finish.await()
            generation.putIfCurrent(oldVersion) { images["cover"] = "stale" }
        }
        started.await()
        generation.invalidate { images.clear() }
        assertTrue(generation.putIfCurrent(generation.version.value) { images["cover"] = "fresh" })
        finish.complete(Unit)
        assertFalse(oldDecode.await())
        assertEquals("fresh", images["cover"])
    }

    @Test fun oldMemoryTokenCannotReadOrRepopulateAfterClear() {
        val generation = CoverImageCacheGeneration()
        val images = mutableMapOf("cover" to "old")
        val oldVersion = generation.version.value
        generation.invalidate { images.clear() }
        assertNull(generation.getIfCurrent(oldVersion) { "stale reference" })
        assertFalse(generation.putIfCurrent(oldVersion) { images["cover"] = "old" })
        assertTrue(images.isEmpty())
        assertEquals(1L, generation.version.value)
    }

    @Test fun invalidationPublishesVersionOnlyAfterCachedEntriesAreRemoved() = runBlocking {
        val generation = CoverImageCacheGeneration()
        val images = mutableMapOf("cover" to "old")
        val observed = mutableListOf<Boolean>()
        val observer = launch(start = CoroutineStart.UNDISPATCHED) {
            generation.version.collect { if (it > 0) observed += images.isEmpty() }
        }
        generation.invalidate { images.clear() }
        yield()
        observer.cancelAndJoin()
        assertEquals(listOf(true), observed)
    }
}
