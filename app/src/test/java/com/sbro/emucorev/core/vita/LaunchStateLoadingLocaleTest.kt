package com.sbro.emucorev.core.vita

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LaunchStateLoadingLocaleTest {
    @Test
    fun loadingLabelExistsAndIsTranslatedInEverySupportedLocale() {
        val root = locateResourceRoot()
        val defaultText = readLabel(root.resolve("values/strings.xml"))
        assertEquals("Loading save state…", defaultText)

        val locales = Files.list(root).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }
        assertEquals("Expected the app's 17 localized resource directories", 17, locales.size)
        locales.forEach { locale ->
            val localizedText = readLabel(locale.resolve("strings.xml"))
            assertTrue("Missing loading label in ${locale.fileName}", localizedText.isNotBlank())
            assertNotEquals("Loading label is not translated in ${locale.fileName}", defaultText, localizedText)
        }
    }

    private fun readLabel(path: Path): String {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        val matches = (0 until nodes.length).mapNotNull { index ->
            val element = nodes.item(index)
            val key = element.attributes?.getNamedItem("name")?.nodeValue
            if (key == "emulation_savestate_startup_loading") element.textContent.trim() else null
        }
        assertEquals("Expected exactly one startup loading label in $path", 1, matches.size)
        return matches.single()
    }

    private fun locateResourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory.resolve("src/main/res"),
            workingDirectory.resolve("app/src/main/res")
        ).firstOrNull(Path::isDirectory)
            ?: error("Unable to locate app/src/main/res from $workingDirectory")
    }
}
