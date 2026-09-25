package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryInitRegistrationContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private val libraryInitRegex = Regex("""LIBRARY_INIT\(([A-Za-z_][A-Za-z0-9_]*)\)""")
    private val libraryListRegex = Regex("""LIBRARY\(([A-Za-z_][A-Za-z0-9_]*)\)""")

    @Test fun everyLibraryInitIsRegisteredInLibraryInitList() {
        val modulesDir = app.resolve("src/main/cpp/vita3k/vita3k/modules")
        val declared = mutableSetOf<String>()
        Files.walk(modulesDir).use { paths ->
            paths.filter { it.toString().endsWith(".cpp") }.forEach { path ->
                libraryInitRegex.findAll(path.readText()).forEach { declared += it.groupValues[1] }
            }
        }

        val registered = libraryListRegex.findAll(
            app.resolve("src/main/cpp/vita3k/vita3k/modules/include/modules/library_init_list.inc").readText()
        ).map { it.groupValues[1] }.toSet()

        assertTrue("No LIBRARY_INIT declarations found in $modulesDir", declared.isNotEmpty())
        assertEquals(
            "Every LIBRARY_INIT(X) must be registered as LIBRARY(X) in library_init_list.inc",
            declared.sorted(),
            registered.sorted()
        )
    }
}
