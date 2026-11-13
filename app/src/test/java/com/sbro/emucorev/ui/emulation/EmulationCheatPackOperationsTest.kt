package com.sbro.emucorev.ui.emulation

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.yield
import org.junit.Assert.*
import org.junit.Test

class EmulationCheatPackOperationsTest {
    @Test fun anotherRegionOrExplicitVersionMismatchNeverStartsAnImport() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("PCSA00029", "01.00"))
            var imports = 0
            operations.install("PCSF00001", "1.00") { imports++; true }
            yield()
            assertEquals(0, imports)
            assertEquals(CheatPackFeedback.INCOMPATIBLE, operations.state.value.feedback)
            operations.install("PCSA00029", "1.01") { imports++; true }
            yield()
            assertEquals(0, imports)
            assertFalse(operations.state.value.busy)
            assertEquals(CheatPackFeedback.INCOMPATIBLE, operations.state.value.feedback)
        } finally { scope.cancel() }
    }

    @Test fun downloadCompletesWithoutAMenuCollectorAndSameTargetRebindDoesNotCancelIt() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            val target = CheatPackTarget("PCSA00029", "01.00")
            operations.bind(target)
            val bytes = CompletableDeferred<Boolean>()
            operations.install("pcsa00029", "1.0") { canCommit ->
                assertTrue(canCommit())
                bytes.await() && canCommit()
            }
            yield()
            assertTrue(operations.state.value.busy)
            operations.bind(target)
            assertTrue(operations.state.value.busy)
            bytes.complete(true)
            yield()
            assertFalse(operations.state.value.busy)
            assertEquals(CheatPackFeedback.INSTALLED, operations.state.value.feedback)
            assertEquals(1L, operations.state.value.installedRevision)
        } finally { scope.cancel() }
    }

    @Test fun staleBlockingDownloadCannotCommitOrClearTheNextTitlesBusyState() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("PCSA00029", "1.00"))
            val oldDone = CompletableDeferred<Unit>()
            val newDone = CompletableDeferred<Boolean>()
            var oldCanCommit: Boolean? = null
            operations.install("PCSA00029", "1.00") { canCommit ->
                withContext(NonCancellable) {
                    oldDone.await()
                    oldCanCommit = canCommit()
                    true
                }
            }
            yield()
            assertTrue(operations.state.value.busy)
            operations.bind(CheatPackTarget("PCSB00336", "1.00"))
            operations.install("PCSB00336", "1.00") { newDone.await() }
            yield()
            oldDone.complete(Unit)
            repeat(3) { yield() }
            assertEquals(false, oldCanCommit)
            assertTrue(operations.state.value.busy)
            assertNull(operations.state.value.feedback)
            assertEquals("PCSB00336", operations.state.value.target?.titleId)
            newDone.complete(true)
            repeat(2) { yield() }
            assertEquals(1L, operations.state.value.installedRevision)
            assertFalse(operations.state.value.busy)
        } finally { scope.cancel() }
    }

    @Test fun failedInstallClearsBusyAndAllowsRetryWithoutInventingSuccess() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("PCSB00336", "1.00"))
            operations.install("PCSB00336", "1.00") { error("digest/import rejected") }
            yield()
            assertFalse(operations.state.value.busy)
            assertEquals(CheatPackFeedback.FAILED, operations.state.value.feedback)
            assertEquals(0L, operations.state.value.installedRevision)
            operations.install("PCSB00336", "1.00") { true }
            yield()
            assertEquals(CheatPackFeedback.INSTALLED, operations.state.value.feedback)
            assertEquals(1L, operations.state.value.installedRevision)
        } finally { scope.cancel() }
    }

    @Test fun cancellationIsNotAnInstallFailureAndAlwaysReleasesBusy() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("PCSA00029", "1.00"))
            val cancelled = CompletableDeferred<Boolean>()
            operations.install("PCSA00029", "1.00") { cancelled.await() }
            yield()
            assertTrue(operations.state.value.busy)
            cancelled.completeExceptionally(CancellationException("Activity destroyed"))
            yield()
            assertFalse(operations.state.value.busy)
            assertNull(operations.state.value.feedback)
            assertEquals(0L, operations.state.value.installedRevision)
        } finally { scope.cancel() }
    }

    @Test fun secondClickWhileBusyCannotStartAnOverlappingPackReplacement() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("PCSA00029", "1.00"))
            val done = CompletableDeferred<Boolean>()
            var imports = 0
            operations.install("PCSA00029", "1.00") { imports++; done.await() }
            operations.install("PCSA00029", "1.00") { imports++; true }
            yield()
            assertEquals(1, imports)
            assertTrue(operations.state.value.busy)
            done.complete(true)
            yield()
            assertEquals(1L, operations.state.value.installedRevision)
        } finally { scope.cancel() }
    }

    @Test fun unsafeTitleIdsAreRejectedBeforeAnyFileOperation() = runBlocking {
        val scope = CoroutineScope(coroutineContext + SupervisorJob())
        try {
            val operations = EmulationCheatPackOperations(scope)
            operations.bind(CheatPackTarget("../PCSA00029", "1.00"))
            operations.install("../PCSA00029", "1.00") { fail("must not import a path"); true }
            yield()
            assertEquals(CheatPackFeedback.INCOMPATIBLE, operations.state.value.feedback)
            assertEquals(0L, operations.state.value.installedRevision)
        } finally { scope.cancel() }
    }
}
