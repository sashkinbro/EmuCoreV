package com.sbro.emucorev.ui.feedback

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FeedbackLocaleContractTest {
    @Test
    fun everySupportedLocaleContainsTranslatedFeedbackAndDiscordStrings() {
        val resourceRoot = locateResourceRoot()
        val defaultPath = resourceRoot.resolve("values/strings.xml")
        val requiredKeys = requiredValues(defaultPath).keys
        val defaultValues = requiredValues(defaultPath)
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }

        assertEquals(11, localizedDirectories.size)
        assertTrue("Expected the complete feedback contract", requiredKeys.size >= 40)
        localizedDirectories.forEach { directory ->
            val values = requiredValues(directory.resolve("strings.xml"))
            assertEquals("Feedback resources differ in ${directory.fileName}", requiredKeys, values.keys)
            val translatedCount = requiredKeys.count { key -> values[key] != defaultValues[key] }
            assertTrue(
                "Most feedback strings must be translated in ${directory.fileName}; translated=$translatedCount",
                translatedCount >= 25
            )
            assertTrue(
                "Old product name leaked into ${directory.fileName}",
                values.values.none { "EmuCoreX" in it }
            )
        }
    }

    private fun requiredValues(path: Path): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        return buildMap {
            for (index in 0 until nodes.length) {
                val node = nodes.item(index)
                val name = node.attributes?.getNamedItem("name")?.nodeValue.orEmpty()
                if (name.startsWith("feedback_") || name == "shell_discord_server") {
                    put(name, node.textContent.trim())
                }
            }
        }
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
