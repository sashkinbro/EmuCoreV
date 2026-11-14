package com.sbro.emucorev.data

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

class RetroArchShaderOfficialPackTest {
    @Test fun discoversTheInstalledOfficialCatalog() {
        val root = System.getenv("ECV_SHADER_PACK_FIXTURE")?.let(::File)
        assumeTrue("Set ECV_SHADER_PACK_FIXTURE to an extracted official Slang pack", root?.isDirectory == true)
        val files = root!!.walkTopDown().filter { it.extension == "slangp" }.toList()
        val started = System.nanoTime()
        val catalog = RetroArchShaderPresetParser.inspectPresets(root)
        val valid = catalog.size
        val known = catalog.map { it.first }.toSet()
        val results = files.filter { it !in known }.map { it to RetroArchShaderPresetParser.inspect(it, root) }
        println("Official catalog: $valid/${files.size}, ${(System.nanoTime() - started) / 1_000_000} ms")
        results.filterNot { it.second.isValid }.groupBy { it.second.error }.forEach { (error, failures) ->
            println("${failures.size}: $error: ${failures.take(3).map { it.first.relativeTo(root) }}")
        }
        val presets = catalog.map { (file, data) -> RetroArchShaderPreset(file.relativeTo(root).path,
            file.path, data.passCount, data.textureIds, data.parameters) }
        RetroArchShaderCatalog.write(root, presets)
        val reopened = RetroArchShaderCatalog.read(root)
        assertTrue("The entire official catalog must survive reopening", reopened?.size == valid)
        println("Persisted catalog: ${File(root, ".preset-catalog-v3").length()} bytes")
        assertTrue("Most official presets must be discoverable: $valid/${files.size}", valid > files.size * 0.9)
    }
}
