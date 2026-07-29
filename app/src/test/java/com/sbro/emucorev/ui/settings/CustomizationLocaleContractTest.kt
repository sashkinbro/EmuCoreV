package com.sbro.emucorev.ui.settings

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizationLocaleContractTest {
    @Test
    fun everySupportedLocaleContainsEveryCustomizationString() {
        val resourceRoot = locateResourceRoot()
        val defaultKeys = customizationKeys(resourceRoot.resolve("values/strings.xml"))
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }

        assertEquals(11, localizedDirectories.size)
        localizedDirectories.forEach { directory ->
            val localizedKeys = customizationKeys(directory.resolve("strings.xml"))
            assertEquals(
                "Customization resources differ in ${directory.fileName}",
                defaultKeys,
                localizedKeys
            )
        }
        assertTrue("Expected customization resources", defaultKeys.size >= 28)
    }

    private fun customizationKeys(path: Path): Set<String> {
        val document = DocumentBuilderFactory.newInstance()
            .newDocumentBuilder()
            .parse(path.toFile())
        val keys = mutableSetOf<String>()
        for (tag in listOf("string", "plurals")) {
            val nodes = document.getElementsByTagName(tag)
            for (index in 0 until nodes.length) {
                val name = nodes.item(index).attributes?.getNamedItem("name")?.nodeValue.orEmpty()
                if (
                    name.startsWith("customization_") ||
                    name.startsWith("settings_customization_") ||
                    name.startsWith("settings_drawer_style_") ||
                    name.startsWith("settings_game_menu_") ||
                    name == "settings_tab_customization"
                ) {
                    keys += name
                }
            }
        }
        return keys
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
