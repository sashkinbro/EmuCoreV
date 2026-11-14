package com.sbro.emucorev.data

import android.content.Context
import java.io.File
import java.security.MessageDigest

enum class RetroArchShaderSelectionScope { GLOBAL, GAME_OVERRIDE }

data class RetroArchShaderSelection(
    val enabled: Boolean,
    val presetPath: String,
    val scope: RetroArchShaderSelectionScope,
    val inheritsGlobal: Boolean
)

/** Stores user-facing shader selections without changing the native Vita config schema. */
class RetroArchShaderSelectionRepository(context: Context) {
    private val appContext = context.applicationContext
    private val preferences = appContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
    private val packRoot: File get() = File(appContext.filesDir, "retroarch-shaders")

    fun global(): RetroArchShaderSelection = readSelection(
        key = GLOBAL_KEY,
        scope = RetroArchShaderSelectionScope.GLOBAL,
        inheritsGlobal = false
    )

    fun effectiveFor(titleId: String): RetroArchShaderSelection {
        val globalSelection = global()
        val key = gameKey(titleId)
        if (!preferences.getBoolean("$key.override", false)) {
            return RetroArchShaderSelectionPolicy.effective(globalSelection, null)
        }
        return RetroArchShaderSelectionPolicy.effective(
            globalSelection,
            readSelection(
                key = key,
                scope = RetroArchShaderSelectionScope.GAME_OVERRIDE,
                inheritsGlobal = false
            )
        )
    }

    fun setGlobal(enabled: Boolean, presetAbsolutePath: String) {
        writeSelection(GLOBAL_KEY, enabled, presetAbsolutePath)
    }

    fun setGameOverride(titleId: String, enabled: Boolean, presetAbsolutePath: String) {
        val key = gameKey(titleId)
        val preset = encodePresetPath(presetAbsolutePath)
        preferences.edit()
            .putBoolean("$key.override", true)
            .putBoolean("$key.enabled", enabled && preset.isNotBlank())
            .putString("$key.preset", preset)
            .apply()
    }

    fun clearGameOverride(titleId: String) {
        val key = gameKey(titleId)
        preferences.edit()
            .remove("$key.override")
            .remove("$key.enabled")
            .remove("$key.preset")
            .apply()
    }

    /** Reads saved values for one preset and one selection scope, ignoring unknown or invalid entries. */
    fun parameterOverrides(
        scope: RetroArchShaderSelectionScope,
        titleId: String?,
        preset: RetroArchShaderPreset
    ): Map<String, Float> {
        val key = parameterKey(scope, titleId, preset)
        val stored = preset.parameters.mapNotNull { parameter ->
            val valueKey = "$key.${parameter.id}"
            if (preferences.contains(valueKey)) {
                runCatching { preferences.getFloat(valueKey, Float.NaN) }
                    .getOrNull()?.let { parameter.id to it }
            } else null
        }.toMap()
        return RetroArchShaderSelectionPolicy.validParameterOverrides(preset, stored)
    }

    /** Saves only finite values for a parameter declared by this installed preset. */
    fun setParameterOverride(
        scope: RetroArchShaderSelectionScope,
        titleId: String?,
        preset: RetroArchShaderPreset,
        parameterId: String,
        value: Float
    ): Boolean {
        val bounded = RetroArchShaderSelectionPolicy.validParameterOverrides(preset, mapOf(parameterId to value))[parameterId]
            ?: return false
        preferences.edit().putFloat("${parameterKey(scope, titleId, preset)}.$parameterId", bounded).apply()
        return true
    }

    /** Clears values only for the specified scope, title and preset; preset defaults then apply. */
    fun clearParameterOverrides(
        scope: RetroArchShaderSelectionScope,
        titleId: String?,
        preset: RetroArchShaderPreset
    ) {
        val key = parameterKey(scope, titleId, preset)
        val editor = preferences.edit()
        preset.parameters.forEach { editor.remove("$key.${it.id}") }
        editor.apply()
    }

    fun clearSelectionsForRemovedPresets(paths: Set<String>) {
        if (paths.isEmpty()) return
        val removed = paths.mapNotNull { path -> runCatching { File(path).canonicalPath }.getOrNull() }.toSet()
        if (removed.isEmpty()) return
        val root = packRoot.canonicalFile
        val removedRelative = removed.mapNotNull { path ->
            runCatching {
                File(path).canonicalFile.takeIf { isInside(root, it) && it.extension.equals("slangp", true) }
                    ?.relativeTo(root)?.invariantSeparatorsPath
            }.getOrNull()
        }.toSet()
        if (removedRelative.isEmpty()) return
        val editor = preferences.edit()
        if (preferences.getString("$GLOBAL_KEY.preset", "").orEmpty() in removedRelative) {
            editor.putBoolean("$GLOBAL_KEY.enabled", false).remove("$GLOBAL_KEY.preset")
        }
        removedRelative.forEach { relative ->
            val parameterSuffix = ".params.${presetHash(relative)}."
            preferences.all.keys.filter { key ->
                key.startsWith("$GLOBAL_KEY$parameterSuffix") ||
                    key.startsWith("game.") && ".params.${presetHash(relative)}." in key
            }.forEach(editor::remove)
        }
        preferences.all.keys.filter { it.endsWith(".override") && preferences.getBoolean(it, false) }
            .forEach { overrideKey ->
                val key = overrideKey.removeSuffix(".override")
                val selectedRelative = preferences.getString("$key.preset", "").orEmpty()
                if (selectedRelative in removedRelative) {
                    editor.remove(overrideKey).remove("$key.enabled").remove("$key.preset")
                }
            }
        editor.apply()
    }

    private fun writeSelection(key: String, enabled: Boolean, presetAbsolutePath: String) {
        val preset = encodePresetPath(presetAbsolutePath)
        preferences.edit()
            .putBoolean("$key.enabled", enabled && preset.isNotBlank())
            .putString("$key.preset", preset)
            .apply()
    }

    private fun readSelection(
        key: String,
        scope: RetroArchShaderSelectionScope,
        inheritsGlobal: Boolean
    ): RetroArchShaderSelection {
        val relative = preferences.getString("$key.preset", "").orEmpty()
        val absolute = decodePresetPath(relative)
        val enabled = preferences.getBoolean("$key.enabled", false) && absolute.isNotBlank()
        return RetroArchShaderSelection(enabled, absolute, scope, inheritsGlobal)
    }

    private fun encodePresetPath(path: String): String {
        if (path.isBlank()) return ""
        val root = packRoot.canonicalFile
        val preset = File(path).canonicalFile
        require(isInside(root, preset) && preset.isFile && preset.extension.equals("slangp", ignoreCase = true)) {
            "Shader preset must be an installed .slangp file"
        }
        return preset.relativeTo(root).invariantSeparatorsPath
    }

    private fun decodePresetPath(relative: String): String {
        if (relative.isBlank() || relative.startsWith('/') || relative.startsWith('\\') ||
            '\\' in relative || ':' in relative) return ""
        val root = packRoot.canonicalFile
        val preset = File(root, relative).canonicalFile
        if (!isInside(root, preset) || !preset.isFile || !preset.extension.equals("slangp", ignoreCase = true)) return ""
        return preset.absolutePath
    }

    private fun gameKey(titleId: String): String {
        val normalized = titleId.trim().uppercase()
        require(normalized.matches(Regex("[A-Z0-9_-]{1,32}"))) { "Invalid Vita title ID" }
        return "game.$normalized"
    }

    private fun parameterKey(
        scope: RetroArchShaderSelectionScope,
        titleId: String?,
        preset: RetroArchShaderPreset
    ): String {
        val presetPath = encodePresetPath(preset.absolutePath)
        require(preset.parameters.size <= MAX_PARAMETER_COUNT) { "Shader preset has too many parameters" }
        val base = when (scope) {
            RetroArchShaderSelectionScope.GLOBAL -> GLOBAL_KEY
            RetroArchShaderSelectionScope.GAME_OVERRIDE -> gameKey(requireNotNull(titleId) { "Game title ID is required" })
        }
        return "$base.params.${presetHash(presetPath)}"
    }

    private fun presetHash(relativePath: String): String = MessageDigest.getInstance("SHA-256")
        .digest(relativePath.toByteArray(Charsets.UTF_8))
        .take(12)
        .joinToString("") { byte -> "%02x".format(byte) }

    private fun isInside(root: File, file: File): Boolean =
        file.canonicalPath.startsWith(root.canonicalPath.trimEnd(File.separatorChar) + File.separator)

    private companion object {
        const val PREFERENCES_NAME = "retroarch_shader_selection"
        const val GLOBAL_KEY = "global"
        const val MAX_PARAMETER_COUNT = 256
    }
}
