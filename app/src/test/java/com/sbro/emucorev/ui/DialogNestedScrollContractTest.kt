package com.sbro.emucorev.ui

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.extension
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The shared dialog scaffold (SettingsStyledDialog / AppAlertDialog / StyledDialogScaffold)
 * already wraps its content in a verticalScroll. A second verticalScroll or a Lazy list in
 * that content is measured with infinite height and crashes Compose at runtime (this broke
 * installing archives that contain several packages).
 */
class DialogNestedScrollContractTest {
    private val sources = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }
        .resolve("src/main/java")

    private val dialogCall = Regex("""\b(SettingsStyledDialog|AppAlertDialog)\s*\(""")
    private val scrolling = Regex("""\.verticalScroll\(|\bLazyColumn\s*\(|\bLazyVerticalGrid\s*\(""")

    /** Returns the argument list plus a trailing lambda of the call starting at [open] '('. */
    private fun callBody(text: String, open: Int): String {
        var depth = 0
        var i = open
        while (i < text.length) {
            when (text[i]) { '(' -> depth++; ')' -> { depth--; if (depth == 0) break } }
            i++
        }
        var end = i + 1
        val after = text.substring(end).trimStart()
        if (after.startsWith("{")) {
            var j = text.indexOf('{', end)
            var braces = 0
            while (j < text.length) {
                when (text[j]) { '{' -> braces++; '}' -> { braces--; if (braces == 0) break } }
                j++
            }
            end = j + 1
        }
        return text.substring(open, minOf(end, text.length))
    }

    @Test fun styledDialogContentNeverNestsAScrollContainer() {
        val offenders = mutableListOf<String>()
        Files.walk(sources).use { paths ->
            paths.filter { it.extension == "kt" && !it.endsWith("SettingAssist.kt") }.forEach { file ->
                val text = file.readText()
                dialogCall.findAll(text).forEach { match ->
                    val body = callBody(text, match.range.last)
                    if (scrolling.containsMatchIn(body)) {
                        val line = text.substring(0, match.range.first).count { it == '\n' } + 1
                        offenders += "${sources.relativize(file)}:$line ${match.groupValues[1]}"
                    }
                }
            }
        }
        assertTrue("Nested scroll inside a scrolling dialog: $offenders", offenders.isEmpty())
    }
}
