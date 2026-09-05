package com.sbro.emucorev.ui.library

import java.nio.file.Path
import kotlin.io.path.isRegularFile
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LibrarySearchAnimationContractTest {
    @Test
    fun searchFieldAnimatesOpenAndClosedWithoutLeavingACollapsedListItemGap() {
        val source = locateLibraryScreen().readText()

        assertTrue("Search must remain composed long enough to animate both directions", "AnimatedVisibility(" in source)
        assertTrue("Search opening must expand from the top", "expandVertically(expandFrom = Alignment.Top)" in source)
        assertTrue("Search opening must fade in", "fadeIn()" in source)
        assertTrue("Search closing must shrink toward the top", "shrinkVertically(shrinkTowards = Alignment.Top)" in source)
        assertTrue("Search closing must fade out", "fadeOut()" in source)
        assertFalse(
            "Search must not be a conditional LazyColumn item because that skips the exit animation and can leave an arrangement gap",
            Regex("""if \(searchExpanded\)\s*\{\s*item\s*\{""").containsMatchIn(source)
        )
    }

    private fun locateLibraryScreen(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory.resolve("src/main/java/com/sbro/emucorev/ui/library/LibraryScreen.kt"),
            workingDirectory.resolve("app/src/main/java/com/sbro/emucorev/ui/library/LibraryScreen.kt")
        ).firstOrNull(Path::isRegularFile)
            ?: error("Unable to locate LibraryScreen.kt from $workingDirectory")
    }
}
