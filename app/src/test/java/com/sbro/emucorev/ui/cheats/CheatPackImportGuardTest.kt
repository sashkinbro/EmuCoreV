package com.sbro.emucorev.ui.cheats

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.*
import org.junit.Test

class CheatPackImportGuardTest {
    @Test fun staleTitleCannotCrossTheNativeImportCommitPoint() = runBlocking {
        var imports = 0
        assertNull(commitCheatPackIfCurrent({ false }) { imports++; "imported" })
        assertEquals(0, imports)
    }

    @Test fun activeMatchingTitleCommitsExactlyOnce() = runBlocking {
        var imports = 0
        assertEquals("PCSA00029", commitCheatPackIfCurrent({ true }) { imports++; "PCSA00029" })
        assertEquals(1, imports)
    }

    @Test fun cancelledDownloadCannotImportEvenIfItsBlockingReadCompleted() = runBlocking {
        var imports = 0
        var cancellationObserved = false
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        val job = scope.launch {
            try {
                coroutineContext.cancel()
                commitCheatPackIfCurrent({ true }) { imports++; "imported" }
            } catch (_: CancellationException) { cancellationObserved = true }
        }
        job.join()
        assertTrue(cancellationObserved)
        assertEquals(0, imports)
        scope.cancel()
    }
}
