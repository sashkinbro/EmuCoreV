package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class AtomicTextFileTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun replacesCompleteFileAndCleansTemporaryFile() {
        val directory = temporaryFolder.newFolder("config")
        val target = directory.resolve("config.yml").apply { writeText("old") }

        AtomicTextFile.write(target, "new\ncomplete\n")

        assertEquals("new\ncomplete\n", target.readText())
        assertFalse(directory.listFiles().orEmpty().any { it.name.endsWith(".tmp") })
    }
}
