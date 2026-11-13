package com.sbro.emucorev.ui.common

import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class CoverLoadCoordinatorTest {
    @Test fun managedImageUrlsNeverEnterTheIdentityFreeLocalProbe() {
        for (url in listOf(
            "https://images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg",
            "//images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg",
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/${"a".repeat(40)}/covers/42.webp",
            "//raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/${"a".repeat(40)}/covers/42.webp"
        )) assertFalse(url, CoverLoadCoordinator.isLocalCustomCandidate(url))
    }

    @Test fun localFileAndContentCandidatesCanEnterTheLocalProbe() {
        for (path in listOf("/storage/emulated/0/custom.webp", "file:///storage/emulated/0/custom.webp", "content://art/42")) {
            assertTrue(path, CoverLoadCoordinator.isLocalCustomCandidate(path))
        }
    }

    @Test fun fifthCustomWithoutIconDisplaysWhileFourManagedRequestsAreBlocked() = runBlocking {
        withTimeout(2_000) {
            val loads = CoverLoadCoordinator()
            val release = CompletableDeferred<Unit>()
            val jobs = (1..4).map {
                launch(start = CoroutineStart.UNDISPATCHED) {
                    loads.loadVisible(managedArtwork = { release.await() })
                }
            }
            var customDisplayed = false
            var managedStarted = false
            val fifth = launch(start = CoroutineStart.UNDISPATCHED) {
                loads.loadVisible(
                    localCustom = { customDisplayed = true; true },
                    managedArtwork = { managedStarted = true }
                )
            }
            try {
                assertTrue("Custom local art must not wait for a network slot", customDisplayed)
                assertTrue("Valid custom art needs no managed slot", fifth.isCompleted)
                assertFalse(managedStarted)
            } finally {
                release.complete(Unit); jobs.joinAll(); fifth.join()
            }
        }
    }

    @Test fun validCustomArtIsAuthoritativeBeforeManagedArtwork() = runBlocking {
        val loads = CoverLoadCoordinator()
        val displayed = mutableListOf<String>()
        loads.loadVisible(
            localCustom = { displayed += "custom"; true },
            managedArtwork = { displayed += "managed" }
        )
        assertEquals(listOf("custom"), displayed)
    }

    @Test fun unusableCustomArtLoadsManagedArtworkWithoutPublishingOldIcon() = runBlocking {
        val loads = CoverLoadCoordinator()
        val attempted = mutableListOf<String>()
        loads.loadVisible(
            localCustom = { attempted += "custom-invalid"; false },
            managedArtwork = { attempted += "managed" }
        )
        assertEquals(listOf("custom-invalid", "managed"), attempted)
    }

    @Test fun titlePlaceholderRemainsWhileFourManagedRequestsAreBlocked() = runBlocking {
        withTimeout(2_000) {
            val loads = CoverLoadCoordinator()
            val release = CompletableDeferred<Unit>()
            val fourManagedStarted = CompletableDeferred<Unit>()
            var managedStarts = 0
            val jobs = (1..4).map {
                launch(start = CoroutineStart.UNDISPATCHED) {
                    loads.loadVisible(managedArtwork = {
                        if (++managedStarts == 4) fourManagedStarted.complete(Unit)
                        release.await()
                    })
                }
            }
            fourManagedStarted.await()
            var fifthManagedStarted = false
            val displayed = mutableListOf<String>()
            val fifth = launch(start = CoroutineStart.UNDISPATCHED) {
                loads.loadVisible(managedArtwork = { fifthManagedStarted = true; displayed += "managed" })
            }
            try {
                assertTrue("The title placeholder must remain until managed artwork is available", displayed.isEmpty())
                assertFalse(fifthManagedStarted)
            } finally {
                release.complete(Unit)
                jobs.joinAll(); fifth.join()
            }
            assertEquals(listOf("managed"), displayed)
        }
    }

    @Test fun independentLocalDecodingStillHasAtMostFourConcurrentJobs() = runBlocking {
        withTimeout(2_000) {
            val loads = CoverLoadCoordinator()
            val release = CompletableDeferred<Unit>()
            val fourDecodesStarted = CompletableDeferred<Unit>()
            var active = 0
            var peak = 0
            val jobs = (1..12).map {
                launch(start = CoroutineStart.UNDISPATCHED) {
                    loads.decode {
                        active++; peak = maxOf(active, peak)
                        if (active == 4) fourDecodesStarted.complete(Unit)
                        try { release.await() } finally { active-- }
                    }
                }
            }
            try {
                fourDecodesStarted.await(); assertEquals(4, active)
            } finally {
                release.complete(Unit); jobs.joinAll()
            }
            assertEquals(4, peak)
        }
    }

    @Test fun prefetchDoesNotQueueAheadOfVisibleManagedRequests() = runBlocking {
        withTimeout(2_000) {
            val loads = CoverLoadCoordinator()
            val release = CompletableDeferred<Unit>()
            val jobs = (1..4).map {
                launch(start = CoroutineStart.UNDISPATCHED) {
                    loads.loadVisible(managedArtwork = { release.await() })
                }
            }
            var prefetched = false
            try {
                loads.prefetch { prefetched = true }
                assertFalse(prefetched)
            } finally {
                release.complete(Unit); jobs.joinAll()
            }
            loads.prefetch { prefetched = true }
            assertTrue(prefetched)
        }
    }
}
