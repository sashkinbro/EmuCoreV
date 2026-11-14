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
            "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)" +
            "(?:\\s+([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?))?\\s*(?://.*)?$"
    )

    private data class Value(val text: String, val directory: File)
    private data class SourceBudget(var files: Int = 0, var bytes: Long = 0L)
    private data class PresetBudget(var files: Int = 0, var bytes: Long = 0L)
    private class Invalid(message: String) : Exception(message)
    private data class SourceData(val parameters: List<RetroArchShaderParameter>, val includes: List<File>, val bytes: Long)
    private data class PresetData(val fields: Map<String, Value>, val depth: Int, val files: Int, val bytes: Long)
    private class InspectionContext {
        val fields = mutableMapOf<String, PresetData>()
        val sources = mutableMapOf<String, SourceData>()
        val paths = mutableMapOf<String, File>()
    }

    fun validPresetFiles(packRoot: File): List<File> {
        return inspectPresets(packRoot).map { it.first }
    }

    fun inspectPresets(packRoot: File, onProgress: (Int, Int) -> Unit = { _, _ -> }): List<Pair<File, RetroArchShaderPresetInspection>> {
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
        val context = InspectionContext()
        return files.mapIndexedNotNull { index, file ->
            val inspection = inspect(file, root, context)
            onProgress(index + 1, files.size)
            if (inspection.isValid) file to inspection else null
        }
    }

    fun inspect(presetFile: File, packRoot: File): RetroArchShaderPresetInspection =
        inspect(presetFile, packRoot, InspectionContext())

    private fun inspect(presetFile: File, packRoot: File, context: InspectionContext): RetroArchShaderPresetInspection = try {
        val root = packRoot.canonicalFile
        val preset = presetFile.canonicalFile
        requireInside(root, preset)
        if (!preset.isFile || preset.length() > MAX_PRESET_BYTES) fail("preset is missing or too large")

        val fields = resolvePreset(preset, root, linkedSetOf(), PresetBudget(), context)
        val count = fields["shaders"]?.text?.toIntOrNull()
            ?: fail("preset has no valid shader count")
        if (count !in 1..MAX_PASSES) fail("shader count is outside the supported range")

        val sources = (0 until count).map { index ->
            val value = fields["shader$index"] ?: fail("preset is missing shader$index")
            resolveLocal(root, value.directory, value.text, ".slang", context)
        }
        val textureIds = fields["textures"]?.text.orEmpty()
            .split(';').map(String::trim).filter(String::isNotEmpty).distinct()
        textureIds.forEach { id ->
            if (!id.matches(Regex("[A-Za-z_][A-Za-z0-9_]*"))) fail("preset has an invalid texture identifier")
            val value = fields[id] ?: fail("preset is missing texture $id")
            resolveLocal(root, value.directory, value.text, null, context)
        }

        val sourceFiles = linkedSetOf<String>()
        val sourceBudget = SourceBudget()
        val parameterDeclarations = sources.flatMap {
            readParameters(it, root, sourceFiles, 0, sourceBudget, context)
        }
        val parameters = parameterDeclarations.distinctBy { it.id }.sortedBy { it.id }.take(RetroArchShaderPreset.MAX_PARAMETERS).map { p ->
            val override = fields[p.id]?.text?.toFloatOrNull()?.takeIf { it.isFinite() }
            if (override == null) p else p.copy(initialValue = override.coerceIn(p.minimum, p.maximum))
        }
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
        budget: PresetBudget,
        context: InspectionContext
    ): Map<String, Value> {
        requireInside(root, file)
        context.fields[file.path]?.let { cached ->
            if (visiting.size + cached.depth > MAX_REFERENCE_DEPTH || budget.files + cached.files > MAX_REFERENCE_DEPTH ||
                budget.bytes + cached.bytes > MAX_TOTAL_PRESET_BYTES) fail("preset reference chain exceeds the validation limit")
            budget.files += cached.files
            budget.bytes += cached.bytes
            return cached.fields
        }
        val filesBefore = budget.files
        val bytesBefore = budget.bytes
        var depth = 1
        if (!file.isFile || file.length() > MAX_PRESET_BYTES) fail("preset reference is missing or too large")
        if (visiting.size >= MAX_REFERENCE_DEPTH || ++budget.files > MAX_REFERENCE_DEPTH ||
            budget.bytes + file.length() > MAX_TOTAL_PRESET_BYTES) {
            fail("preset reference chain exceeds the validation limit")
        }
        budget.bytes += file.length()
        val presetDirectory = file.parentFile ?: fail("preset has no parent directory")
        val path = file.path
        if (!visiting.add(path)) fail("preset reference cycle detected")
        try {
            val lines = file.readLines(Charsets.UTF_8)
            val inherited = linkedMapOf<String, Value>()
            lines.mapNotNull { reference.matchEntire(it.trim())?.groupValues?.get(1) }.forEach { target ->
                val referenceFile = resolveLocal(root, presetDirectory, target, null, context).also {
                    if (it.extension.lowercase() !in setOf("slangp", "params")) fail("preset reference must use .slangp or .params")
                }
                inherited.putAll(resolvePreset(referenceFile, root, visiting, budget, context))
                depth = maxOf(depth, 1 + context.fields.getValue(referenceFile.path).depth)
            }
            lines.mapNotNull { assignment.matchEntire(it.trim()) }.forEach { match ->
                inherited[match.groupValues[1]] = Value(
                    match.groupValues[2].substringBefore(" //").trim().trim('"', '\''),
                    presetDirectory
                )
            }
            context.fields[file.path] = PresetData(inherited, depth, budget.files - filesBefore, budget.bytes - bytesBefore)
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
        budget: SourceBudget,
        context: InspectionContext
    ): List<RetroArchShaderParameter> {
        requireInside(root, source)
        if (depth > MAX_INCLUDE_DEPTH) fail("shader include nesting is too deep")
        val path = source.path
        if (!visited.add(path)) return emptyList()
        val data = context.sources.getOrPut(path) {
            if (!source.isFile || source.length() > MAX_SOURCE_BYTES) fail("shader source is missing or too large")
            val sourceDirectory = source.parentFile ?: fail("shader source has no parent directory")
            val found = mutableListOf<RetroArchShaderParameter>()
            val dependencies = mutableListOf<File>()
            source.forEachLine(Charsets.UTF_8) { line ->
                // Metadata may be conditional or use macros. The runtime's
                // preprocessor resolves it; it must not hide an otherwise valid preset.
                parameter.find(line)?.let { match ->
                    val values = match.groupValues
                    val initial = values[3].toFloatOrNull()
                    val minimum = values[4].toFloatOrNull()
                    val maximum = values[5].toFloatOrNull()
                    if (values[1].length <= 128 && initial != null && minimum != null && maximum != null &&
                        initial.isFinite() && minimum.isFinite() && maximum.isFinite() && minimum <= maximum) {
                        val step = values[6].toFloatOrNull()?.takeIf { it.isFinite() && it > 0f }
                            ?: ((maximum - minimum) * 0.1f).takeIf { it.isFinite() && it > 0f } ?: 1f
                        found += RetroArchShaderParameter(values[1], values[2].take(1024), initial.coerceIn(minimum, maximum), minimum, maximum, step)
                    }
                }
                include.find(line)?.groupValues?.get(1)?.let { dependency ->
                    dependencies += resolveLocal(root, sourceDirectory, dependency, null, context)
                }
            }
            SourceData(found, dependencies, source.length())
        }
        if (++budget.files > MAX_SOURCE_FILES || budget.bytes + data.bytes > MAX_TOTAL_SOURCE_BYTES) {
            fail("shader source dependencies exceed the validation limit")
        }
        budget.bytes += data.bytes
        val found = data.parameters.toMutableList()
        data.includes.forEach { dependency ->
            found += readParameters(dependency, root, visited, depth + 1, budget, context)
        }
        return found
    }

    private fun resolveLocal(root: File, directory: File, relative: String, allowedExtension: String?, context: InspectionContext): File {
        val key = directory.path + "\u0000" + relative + "\u0000" + allowedExtension
        context.paths[key]?.let { return it }
        if (relative.isBlank() || relative.startsWith('/') || relative.startsWith('\\') || '\\' in relative ||
            ':' in relative || relative.split('/').any { it.isEmpty() }) {
            fail("dependency path is unsafe")
        }
        val file = File(directory, relative).canonicalFile
        requireInside(root, file)
        if (!file.isFile) fail("dependency file is missing")
        if (allowedExtension != null && !file.extension.equals(allowedExtension.removePrefix("."), ignoreCase = true)) {
            fail("shader source must use the .slang extension")
        }
        context.paths[key] = file
        return file
    }

    private fun requireInside(root: File, file: File) {
        val rootPath = root.path.trimEnd(File.separatorChar) + File.separator
        if (!file.path.startsWith(rootPath)) fail("dependency resolves outside the shader pack")
    }

    private fun fail(message: String): Nothing = throw Invalid(message)
}
