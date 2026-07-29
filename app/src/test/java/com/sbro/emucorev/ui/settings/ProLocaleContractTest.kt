package com.sbro.emucorev.ui.settings

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProLocaleContractTest {
    @Test
    fun everySupportedLocaleContainsEveryProAndPrivacyString() {
        val resourceRoot = locateResourceRoot()
        val requiredKeys = requiredKeys(resourceRoot.resolve("values/strings.xml"))
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }

        assertEquals(11, localizedDirectories.size)
        assertTrue("Expected a complete Pro string contract", requiredKeys.size >= 39)
        val defaultValues = requiredValues(resourceRoot.resolve("values/strings.xml"))
        localizedDirectories.forEach { directory ->
            val localizedKeys = requiredKeys(directory.resolve("strings.xml"))
            assertEquals(
                "Pro/privacy resources differ in ${directory.fileName}",
                requiredKeys,
                localizedKeys
            )
            val localizedValues = requiredValues(directory.resolve("strings.xml"))
            val translatedCount = requiredKeys.count { key ->
                localizedValues[key] != defaultValues[key]
            }
            assertTrue(
                "Most Pro/privacy strings must be translated in ${directory.fileName}; translated=$translatedCount",
                translatedCount >= 20
            )
            assertTrue(
                "Old EmuCoreX name leaked into ${directory.fileName}",
                localizedValues.values.none { "EmuCoreX" in it }
            )
        }
    }

    private fun requiredKeys(path: Path): Set<String> {
        val document = DocumentBuilderFactory.newInstance()
            .newDocumentBuilder()
            .parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        val keys = mutableSetOf<String>()
        for (index in 0 until nodes.length) {
            val name = nodes.item(index).attributes?.getNamedItem("name")?.nodeValue.orEmpty()
            if (
                name.startsWith("pro_") ||
                name.startsWith("settings_pro_") ||
                name.startsWith("welcome_") ||
                name.startsWith("onboarding_pro_") ||
                name.startsWith("settings_theme") ||
                name == "profile_pro_badge" ||
                name.startsWith("settings_about_privacy_policy")
            ) {
                keys += name
            }
        }
        return keys
    }

    private fun requiredValues(path: Path): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance()
            .newDocumentBuilder()
            .parse(path.toFile())
        val required = requiredKeys(path)
        val nodes = document.getElementsByTagName("string")
        return buildMap {
            for (index in 0 until nodes.length) {
                val node = nodes.item(index)
                val name = node.attributes?.getNamedItem("name")?.nodeValue.orEmpty()
                if (name in required) {
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
