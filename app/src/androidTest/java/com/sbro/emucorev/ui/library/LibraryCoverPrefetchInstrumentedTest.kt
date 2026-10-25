package com.sbro.emucorev.ui.library

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.awaitCancellation
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class LibraryCoverPrefetchInstrumentedTest {
    @get:Rule val compose = createComposeRule()

    @Test fun actualLazyLibraryCancelsOldLookaheadAndReprioritizesAfterReversal() {
        val fixture = Fixture()
        fixture.show()
        compose.waitUntil { fixture.active.get() == 1 }
        compose.onNodeWithTag("library").performScrollToIndex(1000)
        fixture.assertCurrentWindow(forward = true)
        compose.onNodeWithTag("library").performScrollToIndex(900)
        fixture.assertCurrentWindow(forward = false)
        assertEquals("More than one speculative decode ran together", 1, fixture.maximumActive.get())
        assertTrue("A scroll enqueued the catalog instead of a small window", fixture.started.get() < 20)
        assertTrue("Stale speculative requests were retained", fixture.cancelled.get() >= 2)
    }

    @Test fun disablingLibraryLookaheadCancelsItsPendingDecode() {
        val fixture = Fixture()
        fixture.show()
        compose.waitUntil { fixture.active.get() == 1 }
        compose.runOnIdle { fixture.enabled.value = false }
        compose.waitUntil { fixture.active.get() == 0 }
        val before = fixture.started.get()
        compose.onNodeWithTag("library").performScrollToIndex(1000)
        compose.runOnIdle { assertEquals(before, fixture.started.get()) }
    }

    private inner class Fixture {
        val enabled = mutableStateOf(true)
        val active = AtomicInteger()
        val maximumActive = AtomicInteger()
        val started = AtomicInteger()
        val cancelled = AtomicInteger()
        val current = AtomicReference<String?>(null)
        lateinit var gridState: LazyListState
        val games = List(10000) { "title_$it" }
        val window = LibraryCoverPrefetchWindow(games.map {
            LibraryCoverPrefetchRow(it, listOf("cover_$it"))
        })
        private val load: suspend (String) -> Unit = { path ->
            val running = active.incrementAndGet()
            maximumActive.updateAndGet { maxOf(it, running) }
            started.incrementAndGet()
            current.set(path)
            try {
                // Hold the request so a real lazy-scroll update must cancel it.
                awaitCancellation()
            } finally {
                current.set(null)
                active.decrementAndGet()
                cancelled.incrementAndGet()
            }
        }

        fun show() {
            compose.setContent {
                gridState = rememberLazyListState()
                LibraryCoverPrefetch(gridState, window, enabled.value, 0L, load)
                LazyColumn(
                    Modifier.height(300.dp).testTag("library"), state = gridState
                ) {
                    items(games, key = { it }) {
                        Box(Modifier.fillMaxWidth().height(100.dp))
                    }
                }
            }
        }

        fun assertCurrentWindow(forward: Boolean) {
            var expected: String? = null
            compose.runOnIdle {
                expected = window.paths(gridState.layoutInfo.visibleItemsInfo.map { it.key.toString() }, forward).first()
            }
            compose.waitUntil { current.get() == expected }
            compose.runOnIdle { assertEquals(expected, current.get()) }
        }
    }
}
