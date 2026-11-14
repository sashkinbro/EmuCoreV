package com.sbro.emucorev.data

import java.io.File

data class RetroArchShaderPresetInspection(
    val isValid: Boolean,
    val passCount: Int = 0,
    val textureIds: List<String> = emptyList(),
    val parameters: List<RetroArchShaderParameter> = emptyList(),
    val error: String? = null
) {
    val parameterIds: List<String> get() = parameters.map { it.id }
}

/** Bounded structural validation for RetroArch Slang presets and their local dependencies. */
internal object RetroArchShaderPresetParser {
    private const val MAX_PASSES = 64
    private const val MAX_SOURCE_BYTES = 8L * 1024 * 1024
    private const val MAX_PRESET_BYTES = 1024L * 1024
    private const val MAX_PRESET_FILES = 4096
    private const val MAX_TOTAL_PRESET_BYTES = 64L * 1024 * 1024
    private const val MAX_REFERENCE_DEPTH = 64
    private const val MAX_INCLUDE_DEPTH = 64
    private const val MAX_SOURCE_FILES = 256
    private const val MAX_TOTAL_SOURCE_BYTES = 64L * 1024 * 1024

    private val assignment = Regex("^([\\w]+)\\s*=\\s*(.*)$")
    private val reference = Regex("^#reference\\s+[\"'](.+)[\"']\\s*(?://.*)?$")
    private val include = Regex("^\\s*#include\\s+[\"']([^\"']+)[\"']")
    private val parameter = Regex(
        "^\\s*#pragma\\s+parameter\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+\"([^\"]*)\"\\s+" +
            "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s+" +
            "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s+" +
            "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s+" +
            "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s*(?://.*)?$"
    )

    private data class Value(val text: String, val directory: File)
    private data class SourceBudget(var files: Int = 0, var bytes: Long = 0L)
    private data class PresetBudget(var files: Int = 0, var bytes: Long = 0L)
    private class Invalid(message: String) : Exception(message)

    fun validPresetFiles(packRoot: File): List<File> {
        return inspectPresets(packRoot).map { it.first }
    }

    fun inspectPresets(packRoot: File): List<Pair<File, RetroArchShaderPresetInspection>> {
        val root = packRoot.canonicalFile
        val files = mutableListOf<File>()
        val presetBudget = PresetBudget()
        var visited = 0
        for (file in packRoot.walkTopDown()) {
            if (++visited > 20_000) fail("shader pack contains too many files")
            val canonical = runCatching { file.canonicalFile }.getOrNull() ?: continue
            if (file != packRoot && !canonical.path.startsWith(root.path + File.separator)) continue
            if (canonical.isFile && canonical.extension.equals("slangp", ignoreCase = true)) {
                if (++presetBudget.files > MAX_PRESET_FILES ||
                    presetBudget.bytes + canonical.length() > MAX_TOTAL_PRESET_BYTES) {
                    fail("shader pack contains too many or oversized presets")
                }
                presetBudget.bytes += canonical.length()
                files += canonical
            }
        }
        return files.mapNotNull { file ->
            val inspection = inspect(file, root)
            if (inspection.isValid) file to inspection else null
        }
    }

    fun inspect(presetFile: File, packRoot: File): RetroArchShaderPresetInspection = try {
        val root = packRoot.canonicalFile
        val preset = presetFile.canonicalFile
        requireInside(root, preset)
        if (!preset.isFile || preset.length() > MAX_PRESET_BYTES) fail("preset is missing or too large")

        val fields = resolvePreset(preset, root, linkedSetOf(), PresetBudget())
        val count = fields["shaders"]?.text?.toIntOrNull()
            ?: fail("preset has no valid shader count")
        if (count !in 1..MAX_PASSES) fail("shader count is outside the supported range")

        val sources = (0 until count).map { index ->
            val value = fields["shader$index"] ?: fail("preset is missing shader$index")
            resolveLocal(root, value.directory, value.text, ".slang")
        }
        val textureIds = fields["textures"]?.text.orEmpty()
            .split(';').map(String::trim).filter(String::isNotEmpty).distinct()
        textureIds.forEach { id ->
            if (!id.matches(Regex("[A-Za-z_][A-Za-z0-9_]*"))) fail("preset has an invalid texture identifier")
            val value = fields[id] ?: fail("preset is missing texture $id")
            resolveLocal(root, value.directory, value.text, allowedExtension = null)
        }

        val sourceFiles = linkedSetOf<String>()
        val sourceBudget = SourceBudget()
        val parameterDeclarations = sources.flatMap {
            readParameters(it, root, sourceFiles, 0, sourceBudget)
        }
        val conflictingIds = parameterDeclarations.groupBy { it.id }.filterValues { declarations ->
            declarations.distinct().size > 1
        }.keys
        if (conflictingIds.isNotEmpty()) fail("shader chain has conflicting definitions for a parameter")
        val parameters = parameterDeclarations.distinctBy { it.id }.sortedBy { it.id }
        RetroArchShaderPresetInspection(true, count, textureIds, parameters)
    } catch (invalid: Invalid) {
        RetroArchShaderPresetInspection(false, error = invalid.message)
    } catch (_: Exception) {
        RetroArchShaderPresetInspection(false, error = "preset or dependency could not be read")
    }

    private fun resolvePreset(
        file: File,
        root: File,
        visiting: MutableSet<String>,
        budget: PresetBudget
    ): Map<String, Value> {
        requireInside(root, file)
        if (!file.isFile || file.length() > MAX_PRESET_BYTES) fail("preset reference is missing or too large")
        if (visiting.size >= MAX_REFERENCE_DEPTH || ++budget.files > MAX_REFERENCE_DEPTH ||
            budget.bytes + file.length() > MAX_TOTAL_PRESET_BYTES) {
            fail("preset reference chain exceeds the validation limit")
        }
        budget.bytes += file.length()
        val presetDirectory = file.parentFile ?: fail("preset has no parent directory")
        val path = file.canonicalPath
        if (!visiting.add(path)) fail("preset reference cycle detected")
        try {
            val lines = file.readLines(Charsets.UTF_8)
            val inherited = linkedMapOf<String, Value>()
            lines.mapNotNull { reference.matchEntire(it.trim())?.groupValues?.get(1) }.forEach { target ->
                val referenceFile = resolveLocal(root, presetDirectory, target, ".slangp")
                inherited.putAll(resolvePreset(referenceFile, root, visiting, budget))
            }
            lines.mapNotNull { assignment.matchEntire(it.trim()) }.forEach { match ->
                inherited[match.groupValues[1]] = Value(
                    match.groupValues[2].substringBefore(" //").trim().trim('"', '\''),
                    presetDirectory
                )
            }
            return inherited
        } finally {
            visiting.remove(path)
        }
    }

    private fun readParameters(
        source: File,
        root: File,
        visited: MutableSet<String>,
        depth: Int,
        budget: SourceBudget
    ): List<RetroArchShaderParameter> {
        requireInside(root, source)
        if (!source.isFile || source.length() > MAX_SOURCE_BYTES) fail("shader source is missing or too large")
        if (depth > MAX_INCLUDE_DEPTH) fail("shader include nesting is too deep")
        val path = source.canonicalPath
        if (!visited.add(path)) return emptyList() // Include guards can intentionally make a cycle.
        if (++budget.files > MAX_SOURCE_FILES || (budget.bytes + source.length()) > MAX_TOTAL_SOURCE_BYTES) {
            fail("shader source dependencies exceed the validation limit")
        }
        budget.bytes += source.length()
        val sourceDirectory = source.parentFile ?: fail("shader source has no parent directory")
        val found = mutableListOf<RetroArchShaderParameter>()
        source.forEachLine(Charsets.UTF_8) { line ->
            if (line.trimStart().startsWith("#pragma parameter")) {
                val match = parameter.matchEntire(line) ?: fail("shader has a malformed parameter declaration")
                val values = match.groupValues
                val initial = values[3].toFloatOrNull() ?: fail("shader parameter value is invalid")
                val minimum = values[4].toFloatOrNull() ?: fail("shader parameter minimum is invalid")
                val maximum = values[5].toFloatOrNull() ?: fail("shader parameter maximum is invalid")
                val step = values[6].toFloatOrNull() ?: fail("shader parameter step is invalid")
                if (!initial.isFinite() || !minimum.isFinite() || !maximum.isFinite() || !step.isFinite() ||
                    minimum > initial || initial > maximum || step <= 0f) {
                    fail("shader parameter range is invalid")
                }
                found += RetroArchShaderParameter(values[1], values[2], initial, minimum, maximum, step)
            }
            val dependency = include.find(line)?.groupValues?.get(1) ?: return@forEachLine
            found += readParameters(resolveLocal(root, sourceDirectory, dependency, null), root, visited, depth + 1, budget)
        }
        return found
    }

    private fun resolveLocal(root: File, directory: File, relative: String, allowedExtension: String?): File {
        if (relative.isBlank() || relative.startsWith('/') || relative.startsWith('\\') || '\\' in relative ||
            ':' in relative || relative.split('/').any { it == "." || it.isEmpty() }) {
            fail("dependency path is unsafe")
        }
        val file = File(directory, relative).canonicalFile
        requireInside(root, file)
        if (!file.isFile) fail("dependency file is missing")
        if (allowedExtension != null && !file.extension.equals(allowedExtension.removePrefix("."), ignoreCase = true)) {
            fail("shader source must use the .slang extension")
        }
        return file
    }

    private fun requireInside(root: File, file: File) {
        val rootPath = root.canonicalPath.trimEnd(File.separatorChar) + File.separator
        if (!file.canonicalPath.startsWith(rootPath)) fail("dependency resolves outside the shader pack")
    }

    private fun fail(message: String): Nothing = throw Invalid(message)
}
