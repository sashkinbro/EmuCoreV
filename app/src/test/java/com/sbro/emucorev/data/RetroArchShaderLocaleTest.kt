package com.sbro.emucorev.data

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Test

class RetroArchShaderLocaleTest {
    @Test fun everyAppLanguageHasShaderStringsWithMatchingFormatArguments() {
        val working = Path.of(System.getProperty("user.dir"))
        val resources = sequenceOf(working.resolve("src/main/res"), working.resolve("app/src/main/res"))
            .first { Files.isDirectory(it) }
        val base = strings(resources.resolve("values/strings.xml"))
        val locales = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(resources.resolve("xml/locales_config.xml").toFile()).getElementsByTagName("locale")
        val format = Regex("%\\d+\\$[dsf]")
        for (index in 0 until locales.length) {
            val locale = locales.item(index).attributes.getNamedItem("android:name").nodeValue
            val folder = if (locale == "en") "values" else "values-$locale"
            val translated = strings(resources.resolve("$folder/strings.xml"))
            assertEquals("Missing shader translations in $locale", base.keys, translated.keys)
            base.forEach { (key, value) ->
                assertEquals("Format arguments differ for $locale/$key",
                    format.findAll(value).map { it.value }.sorted().toList(),
                    format.findAll(translated.getValue(key)).map { it.value }.sorted().toList())
            }
        }
    }

    private fun strings(path: Path): Map<String, String> {
        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(path.toFile())
            .getElementsByTagName("string")
        val result = linkedMapOf<String, String>()
        for (index in 0 until nodes.length) {
            val node = nodes.item(index)
            val key = node.attributes.getNamedItem("name").nodeValue
            if (key.startsWith("shader_")) {
                check(result.put(key, node.textContent) == null) { "Duplicate $key in $path" }
            }
        }
        return result
    }
}
