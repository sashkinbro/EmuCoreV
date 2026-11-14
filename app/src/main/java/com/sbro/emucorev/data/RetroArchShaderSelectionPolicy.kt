package com.sbro.emucorev.data

internal object RetroArchShaderSelectionPolicy {
    fun effective(
        global: RetroArchShaderSelection,
        gameOverride: RetroArchShaderSelection?
    ): RetroArchShaderSelection = if (gameOverride == null) {
        global.copy(scope = RetroArchShaderSelectionScope.GLOBAL, inheritsGlobal = true)
    } else {
        gameOverride.copy(scope = RetroArchShaderSelectionScope.GAME_OVERRIDE, inheritsGlobal = false)
    }

    /** Accepts only finite values for parameters declared by this preset and clamps to their ranges. */
    fun validParameterOverrides(
        preset: RetroArchShaderPreset,
        stored: Map<String, Float>
    ): Map<String, Float> {
        val known = preset.parameters.asSequence()
            .filter { parameter ->
                parameter.id.matches(PARAMETER_ID) &&
                    parameter.minimum.isFinite() && parameter.maximum.isFinite() &&
                    parameter.minimum <= parameter.maximum
            }
            .associateBy { it.id }
        return stored.entries.mapNotNull { (id, value) ->
            val parameter = known[id] ?: return@mapNotNull null
            if (!value.isFinite()) return@mapNotNull null
            id to value.coerceIn(parameter.minimum, parameter.maximum)
        }.toMap()
    }

    private val PARAMETER_ID = Regex("[A-Za-z_][A-Za-z0-9_]{0,127}")
}
