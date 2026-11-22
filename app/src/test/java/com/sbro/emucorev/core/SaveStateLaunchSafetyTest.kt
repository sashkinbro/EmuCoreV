package com.sbro.emucorev.core

import android.app.Application
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class SaveStateLaunchSafetyTest {
    private val title = "PCSE00120"
    private var inspectStatus = SaveStateResult.STATUS_OK
    private var inspectTitle = title
    private var inspectEngine = 3
    private val backend = object : SaveStateBackend {
        private fun result(status: String) = SaveStateResult(status, "", 0L, inspectTitle, "", inspectEngine, true, 0L)
        override fun save(path: String, appVersion: String) = result(SaveStateResult.STATUS_OK)
        override fun load(path: String, allowCrossSession: Boolean) = result(SaveStateResult.STATUS_OK)
        override fun inspect(path: String) = result(inspectStatus)
        override fun delete(path: String) = result(SaveStateResult.STATUS_OK)
        override fun captureThumbnail(path: String) = true
    }
    private lateinit var repository: SaveStateRepository

    @Before fun setUp() {
        repository = SaveStateRepository(RuntimeEnvironment.getApplication(), backend)
    }

    @Test fun sweepRemovesOnlyInterruptedSaveAndStagingLeftovers() {
        val state = repository.stateFile(title, 0).apply { writeText("state") }
        val thumb = repository.thumbnailFile(title, 0).apply { writeText("thumb") }
        val dir = state.parentFile!!
        val archiveTemp = File(dir, "quick.ecvs.1a2b-3c4d-5e6f.tmp").apply { writeText("partial") }
        val preview = File(dir, ".preview-12345.jpg").apply { writeText("partial") }
        val marker = File(dir, ".loading").apply { writeText("x") }
        val staging = File(EmulatorStorage.cacheRoot(RuntimeEnvironment.getApplication()), "savestate-staging")
            .apply { mkdirs() }
        File(staging, "memory-image-1111-2222-3333.tmp").writeText("big")
        File(staging, "gpu-pages-1111-2222-3333.tmp").writeText("big")

        assertEquals(4, repository.sweepOrphans())

        assertFalse(archiveTemp.exists())
        assertFalse(preview.exists())
        assertTrue(staging.isDirectory)
        assertEquals(0, staging.listFiles()!!.size)
        assertTrue("published state survives", state.isFile)
        assertTrue("published preview survives", thumb.isFile)
        assertTrue("crash marker survives the sweep", marker.isFile)
    }

    @Test fun sweepToleratesMissingDirectories() {
        assertEquals(0, repository.sweepOrphans())
    }

    @Test fun explicitStateMustBeAValidStateOfTheSameTitle() {
        val state = repository.stateFile(title, 1).apply { writeText("state") }
        assertTrue(repository.validStatePath(title, state.absolutePath))

        inspectTitle = "PCSE99999"
        assertFalse("another title's state", repository.validStatePath(title, state.absolutePath))
        inspectTitle = title
        inspectEngine = 0
        assertFalse("unknown engine version", repository.validStatePath(title, state.absolutePath))
        inspectEngine = 3
        inspectStatus = SaveStateResult.STATUS_INVALID_FILE
        assertFalse("corrupt header", repository.validStatePath(title, state.absolutePath))
        inspectStatus = SaveStateResult.STATUS_OK
        assertFalse("missing file", repository.validStatePath(title, File(state.parentFile, "gone.ecvs").absolutePath))
        state.writeText("")
        assertFalse("empty file", repository.validStatePath(title, state.absolutePath))
        assertFalse(repository.validStatePath("", state.absolutePath))
    }

    @Test fun loadMarkerSurvivesUntilTheLoadFinishes() {
        assertNull(repository.interruptedLoad(title))
        repository.beginLoad(title, "/s/quick.ecvs")
        // A new process finding the marker knows the previous load never completed.
        assertEquals("/s/quick.ecvs", SaveStateRepository(RuntimeEnvironment.getApplication(), backend).interruptedLoad(title))
        repository.finishLoad(title)
        assertNull(repository.interruptedLoad(title))
    }
}
