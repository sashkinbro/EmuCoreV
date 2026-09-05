package com.sbro.emucorev.ui.common

import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

class GameContextMenuStyleContractTest {
    @Test
    fun allGameCardLongPressMenusUseTheEmuCoreXSurfaceStyle() {
        val sourceRoot = locateSourceRoot()
        listOf(
            "library/LibraryScreen.kt" to "LibraryGameContextMenuItem(",
            "home/HomeScreen.kt" to "HomeGameContextMenuItem("
        ).forEach { (relativePath, styledItemCall) ->
            val source = sourceRoot.resolve("com/sbro/emucorev/ui/$relativePath").readText()

            assertTrue("$relativePath must constrain the context-menu width", ".widthIn(min = 248.dp, max = 310.dp)" in source)
            assertTrue("$relativePath must use the theme-aware menu shape", "shape = neonShape(20.dp)" in source)
            assertTrue("$relativePath must use the theme surface color", "containerColor = MaterialTheme.colorScheme.surface" in source)
            assertTrue("$relativePath must use the EmuCoreX menu elevation", "shadowElevation = 12.dp" in source)
            assertTrue("$relativePath must use a subtle outlined menu surface", "outlineVariant.copy(alpha = 0.7f)" in source)
            assertTrue("$relativePath must render styled menu rows", styledItemCall in source)
            assertTrue("$relativePath must render icons in compact containers", ".size(34.dp)" in source && "neonShape(11.dp)" in source)
        }
    }

    private fun locateSourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory.resolve("src/main/java"),
            workingDirectory.resolve("app/src/main/java")
        ).firstOrNull(Path::isDirectory)
            ?: error("Unable to locate app/src/main/java from $workingDirectory")
    }
}
