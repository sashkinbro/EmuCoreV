package com.sbro.emucorev.core

import android.app.Application
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class SaveStateRepositoryTest {
    private lateinit var repository: SaveStateRepository
    private lateinit var state: File
    private lateinit var thumbnail: File
    private val backend = object : SaveStateBackend {
        var failSave = true
        var failThumbnail = false
        var throwSave = false
        override fun save(path: String, appVersion: String): SaveStateResult {
            if (throwSave) throw IllegalStateException("synthetic save failure")
            if (!failSave) File(path).writeText("new state")
            return result(if (failSave) SaveStateResult.STATUS_IO_ERROR else SaveStateResult.STATUS_OK)
        }
        override fun load(path: String, allowCrossSession: Boolean) = result(SaveStateResult.STATUS_OK)
        override fun inspect(path: String) = result(SaveStateResult.STATUS_OK)
        override fun delete(path: String) = result(SaveStateResult.STATUS_IO_ERROR)
        override fun captureThumbnail(path: String): Boolean {
            File(path).writeText("new thumbnail")
            return !failThumbnail
        }
    }

    @Before fun setUp() {
        repository = SaveStateRepository(RuntimeEnvironment.getApplication(), backend)
        state = repository.stateFile("PCSE00120", 0)
        thumbnail = repository.thumbnailFile("PCSE00120", 0)
        state.writeText("previous state")
        thumbnail.writeText("previous thumbnail")
    }

    @Test fun failedOverwriteKeepsPreviousStateAndItsThumbnail() {
        assertFalse(repository.save("PCSE00120", 0, "test").isOk)
        assertEquals("previous state", state.readText())
        assertEquals("previous thumbnail", thumbnail.readText())
        assertEquals(setOf(state.name, thumbnail.name), state.parentFile!!.list()!!.toSet())
    }

    @Test fun successfulOverwritePublishesMatchingNewThumbnail() {
        backend.failSave = false
        assertTrue(repository.save("PCSE00120", 0, "test").isOk)
        assertEquals("new state", state.readText())
        assertEquals("new thumbnail", thumbnail.readText())
        assertEquals(setOf(state.name, thumbnail.name), state.parentFile!!.list()!!.toSet())
    }

    @Test fun failedThumbnailDoesNotDiscardACompletedStateOrDisplayAnOldPreview() {
        backend.failSave = false
        backend.failThumbnail = true
        assertTrue(repository.save("PCSE00120", 0, "test").isOk)
        assertEquals("new state", state.readText())
        assertFalse(thumbnail.exists())
    }

    @Test fun deleteFailureKeepsSlotAndPreview() {
        assertFalse(repository.delete("PCSE00120", 0).isOk)
        assertEquals("previous state", state.readText())
        assertEquals("previous thumbnail", thumbnail.readText())
    }

    @Test fun thrownSaveReturnsIoErrorAndKeepsPreviousStateAndPreview() {
        backend.throwSave = true

        val result = repository.save("PCSE00120", 0, "test")

        assertEquals(SaveStateResult.STATUS_IO_ERROR, result.status)
        assertEquals("previous state", state.readText())
        assertEquals("previous thumbnail", thumbnail.readText())
        assertEquals(setOf(state.name, thumbnail.name), state.parentFile!!.list()!!.toSet())
    }

    @Test fun separateRepositoriesSerializeThumbnailSaveAndPublishAsOneSlotOperation() {
        val firstInsideSave = CountDownLatch(1)
        val releaseFirstSave = CountDownLatch(1)
        val secondTaskStarted = CountDownLatch(1)
        val secondThumbnailCaptured = CountDownLatch(1)
        val backend = object : SaveStateBackend {
            override fun captureThumbnail(path: String): Boolean {
                val threadName = Thread.currentThread().name
                if (threadName == "save-B") secondThumbnailCaptured.countDown()
                File(path).writeText("preview-$threadName")
                return true
            }

            override fun save(path: String, appVersion: String): SaveStateResult {
                File(path).writeText("state-$appVersion")
                if (appVersion == "A") {
                    firstInsideSave.countDown()
                    check(releaseFirstSave.await(3, TimeUnit.SECONDS))
                }
                return result(SaveStateResult.STATUS_OK)
            }

            override fun load(path: String, allowCrossSession: Boolean) = result(SaveStateResult.STATUS_OK)
            override fun inspect(path: String) = result(SaveStateResult.STATUS_OK)
            override fun delete(path: String) = result(SaveStateResult.STATUS_OK)
        }
        val firstRepository = SaveStateRepository(RuntimeEnvironment.getApplication(), backend)
        val secondRepository = SaveStateRepository(RuntimeEnvironment.getApplication(), backend)
        val executor = Executors.newFixedThreadPool(2)
        try {
            val first = executor.submit<SaveStateResult> {
                Thread.currentThread().name = "save-A"
                firstRepository.save("PCSE00120", 0, "A")
            }
            assertTrue("first save did not reach its backend", firstInsideSave.await(2, TimeUnit.SECONDS))

            val second = executor.submit<SaveStateResult> {
                Thread.currentThread().name = "save-B"
                secondTaskStarted.countDown()
                secondRepository.save("PCSE00120", 0, "B")
            }
            assertTrue("second save did not start", secondTaskStarted.await(2, TimeUnit.SECONDS))
            assertFalse("second save captured while the first slot operation was active",
                secondThumbnailCaptured.await(200, TimeUnit.MILLISECONDS))

            releaseFirstSave.countDown()
            assertTrue(first.get(3, TimeUnit.SECONDS).isOk)
            assertTrue(second.get(3, TimeUnit.SECONDS).isOk)
            assertEquals("state-B", state.readText())
            assertEquals("preview-save-B", thumbnail.readText())
        } finally {
            releaseFirstSave.countDown()
            executor.shutdownNow()
        }
    }

    private fun result(status: String) = SaveStateResult(status, "", 0, "PCSE00120", "test", 11, true, 0)
}
