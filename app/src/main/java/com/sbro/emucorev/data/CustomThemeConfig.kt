package com.sbro.emucorev.data

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

@Serializable
data class CustomThemeConfig(
    val schemaVersion: Int = CURRENT_SCHEMA_VERSION,
    val name: String = DEFAULT_NAME,
    val dark: Boolean = true,
    val primary: Int = 0xFF4F7DFF.toInt(),
    val onPrimary: Int = 0xFFFFFFFF.toInt(),
    val secondary: Int = 0xFF9B7BFF.toInt(),
    val tertiary: Int = 0xFF4CD7A0.toInt(),
    val background: Int = 0xFF080A10.toInt(),
    val surface: Int = 0xFF12151D.toInt(),
    val surfaceVariant: Int = 0xFF202532.toInt(),
    val onBackground: Int = 0xFFF3F5FF.toInt(),
    val onSurface: Int = 0xFFE9ECF8.toInt(),
    val onSurfaceVariant: Int = 0xFFB6BED3.toInt(),
    val outline: Int = 0xFF3B4356.toInt(),
    val error: Int = 0xFFFF6B7A.toInt(),
    val primaryContainer: Int = blendArgb(primary, background, if (dark) 0.24f else 0.20f),
    val onPrimaryContainer: Int = onBackground,
    val onSecondary: Int = onPrimary,
    val secondaryContainer: Int = blendArgb(secondary, background, if (dark) 0.20f else 0.16f),
    val onSecondaryContainer: Int = onBackground,
    val onTertiary: Int = onPrimary,
    val tertiaryContainer: Int = blendArgb(tertiary, background, if (dark) 0.20f else 0.16f),
    val onTertiaryContainer: Int = onBackground,
    val outlineVariant: Int = blendArgb(outline, surface, 0.68f),
    val onError: Int = onPrimary,
    val errorContainer: Int = blendArgb(error, background, if (dark) 0.22f else 0.18f),
    val onErrorContainer: Int = onBackground,
    val smallCornerDp: Float = 10f,
    val mediumCornerDp: Float = 18f,
    val largeCornerDp: Float = 28f
) {
    fun sanitized(): CustomThemeConfig {
        val opaque = copy(
            schemaVersion = CURRENT_SCHEMA_VERSION,
            name = name.trim().take(MAX_NAME_LENGTH).ifBlank { DEFAULT_NAME },
            primary = primary.forceOpaque(),
            onPrimary = onPrimary.forceOpaque(),
            secondary = secondary.forceOpaque(),
            tertiary = tertiary.forceOpaque(),
            background = background.forceOpaque(),
            surface = surface.forceOpaque(),
            surfaceVariant = surfaceVariant.forceOpaque(),
            onBackground = onBackground.forceOpaque(),
            onSurface = onSurface.forceOpaque(),
            onSurfaceVariant = onSurfaceVariant.forceOpaque(),
            outline = outline.forceOpaque(),
            error = error.forceOpaque(),
            primaryContainer = primaryContainer.forceOpaque(),
            onPrimaryContainer = onPrimaryContainer.forceOpaque(),
            onSecondary = onSecondary.forceOpaque(),
            secondaryContainer = secondaryContainer.forceOpaque(),
            onSecondaryContainer = onSecondaryContainer.forceOpaque(),
            onTertiary = onTertiary.forceOpaque(),
            tertiaryContainer = tertiaryContainer.forceOpaque(),
            onTertiaryContainer = onTertiaryContainer.forceOpaque(),
            outlineVariant = outlineVariant.forceOpaque(),
            onError = onError.forceOpaque(),
            errorContainer = errorContainer.forceOpaque(),
            onErrorContainer = onErrorContainer.forceOpaque(),
            smallCornerDp = smallCornerDp.coerceIn(MIN_CORNER_DP, MAX_SMALL_CORNER_DP),
            mediumCornerDp = mediumCornerDp.coerceIn(MIN_CORNER_DP, MAX_MEDIUM_CORNER_DP),
            largeCornerDp = largeCornerDp.coerceIn(MIN_CORNER_DP, MAX_LARGE_CORNER_DP)
        )
        return opaque.copy(
            onPrimary = readableForeground(opaque.onPrimary, opaque.primary, MIN_ACCENT_CONTRAST),
            onPrimaryContainer = readableForeground(
                opaque.onPrimaryContainer,
                opaque.primaryContainer
            ),
            onSecondary = readableForeground(opaque.onSecondary, opaque.secondary, MIN_ACCENT_CONTRAST),
            onSecondaryContainer = readableForeground(
                opaque.onSecondaryContainer,
                opaque.secondaryContainer
            ),
            onTertiary = readableForeground(opaque.onTertiary, opaque.tertiary, MIN_ACCENT_CONTRAST),
            onTertiaryContainer = readableForeground(
                opaque.onTertiaryContainer,
                opaque.tertiaryContainer
            ),
            onBackground = readableForeground(opaque.onBackground, opaque.background),
            onSurface = readableForeground(opaque.onSurface, opaque.surface),
            onSurfaceVariant = readableForeground(
                opaque.onSurfaceVariant,
                opaque.surfaceVariant
            ),
            onError = readableForeground(opaque.onError, opaque.error, MIN_ACCENT_CONTRAST),
            onErrorContainer = readableForeground(
                opaque.onErrorContainer,
                opaque.errorContainer
            )
        )
    }

    fun encode(): String = JSON.encodeToString(serializer(), sanitized())

    companion object {
        const val CURRENT_SCHEMA_VERSION = 3
        const val DEFAULT_NAME = "My Theme"
        const val MAX_NAME_LENGTH = 40
        const val MIN_CORNER_DP = 0f
        const val MAX_SMALL_CORNER_DP = 20f
        const val MAX_MEDIUM_CORNER_DP = 32f
        const val MAX_LARGE_CORNER_DP = 48f

        val Default = CustomThemeConfig()

        fun decode(raw: String?): CustomThemeConfig {
            if (raw.isNullOrBlank()) return Default
            return runCatching {
                JSON.decodeFromString(serializer(), raw).sanitized()
            }.getOrDefault(Default)
        }

        private val JSON = Json {
            ignoreUnknownKeys = true
            encodeDefaults = true
        }
    }
}

private fun Int.forceOpaque(): Int = this or 0xFF000000.toInt()

private fun readableForeground(foreground: Int, background: Int, minimum: Double = MIN_TEXT_CONTRAST): Int {
    if (contrastRatio(foreground, background) >= minimum) return foreground
    val dark = 0xFF101216.toInt()
    val light = 0xFFF8F9FF.toInt()
    return if (contrastRatio(dark, background) >= contrastRatio(light, background)) {
        dark
    } else {
        light
    }
}

private fun contrastRatio(first: Int, second: Int): Double {
    val firstLuminance = relativeLuminance(first)
    val secondLuminance = relativeLuminance(second)
    return (max(firstLuminance, secondLuminance) + 0.05) /
        (min(firstLuminance, secondLuminance) + 0.05)
}

private fun relativeLuminance(color: Int): Double {
    fun channel(shift: Int): Double {
        val value = ((color ushr shift) and 0xFF) / 255.0
        return if (value <= 0.04045) value / 12.92 else ((value + 0.055) / 1.055).pow(2.4)
    }
    return 0.2126 * channel(16) + 0.7152 * channel(8) + 0.0722 * channel(0)
}

private const val MIN_TEXT_CONTRAST = 4.5

// Text/icons on accent fills (buttons, switch thumbs) are bold/large; WCAG large-text contrast is enough.
private const val MIN_ACCENT_CONTRAST = 3.0

private fun blendArgb(foreground: Int, background: Int, foregroundAmount: Float): Int {
    val amount = foregroundAmount.coerceIn(0f, 1f)
    fun channel(shift: Int): Int {
        val foregroundChannel = (foreground ushr shift) and 0xFF
        val backgroundChannel = (background ushr shift) and 0xFF
        return (backgroundChannel + (foregroundChannel - backgroundChannel) * amount)
            .toInt()
            .coerceIn(0, 255)
    }
    return (0xFF shl 24) or
        (channel(16) shl 16) or
        (channel(8) shl 8) or
        channel(0)
}

@Serializable
data class SavedCustomTheme(
    val id: String,
    val config: CustomThemeConfig,
    val createdAtMillis: Long = 0L,
    val updatedAtMillis: Long = createdAtMillis
) {
    fun sanitized(): SavedCustomTheme? {
        val safeId = id.trim().take(MAX_ID_LENGTH)
        if (safeId.isBlank()) return null
        return copy(
            id = safeId,
            config = config.sanitized(),
            createdAtMillis = createdAtMillis.coerceAtLeast(0L),
            updatedAtMillis = updatedAtMillis.coerceAtLeast(createdAtMillis.coerceAtLeast(0L))
        )
    }

    companion object {
        const val MAX_ID_LENGTH = 80
    }
}

@Serializable
data class CustomThemeLibrary(
    val schemaVersion: Int = CURRENT_SCHEMA_VERSION,
    val activeThemeId: String? = null,
    val themes: List<SavedCustomTheme> = emptyList()
) {
    fun sanitized(): CustomThemeLibrary {
        val unique = LinkedHashMap<String, SavedCustomTheme>()
        themes.forEach { entry ->
            entry.sanitized()?.let { safe ->
                if (!unique.containsKey(safe.id) && unique.size < MAX_THEMES) {
                    unique[safe.id] = safe
                }
            }
        }
        val safeActiveId = activeThemeId?.takeIf(unique::containsKey)
        return copy(
            schemaVersion = CURRENT_SCHEMA_VERSION,
            activeThemeId = safeActiveId,
            themes = unique.values.toList()
        )
    }

    fun activeTheme(): SavedCustomTheme? {
        val safe = sanitized()
        return safe.themes.firstOrNull { it.id == safe.activeThemeId }
    }

    fun encode(): String = JSON.encodeToString(serializer(), sanitized())

    companion object {
        const val CURRENT_SCHEMA_VERSION = 1
        const val MAX_THEMES = 24
        const val LEGACY_THEME_ID = "legacy-custom-theme"

        val Empty = CustomThemeLibrary()

        fun decode(raw: String?, legacyThemeRaw: String? = null): CustomThemeLibrary {
            if (!raw.isNullOrBlank()) {
                val decoded = runCatching {
                    JSON.decodeFromString(serializer(), raw).sanitized()
                }.getOrNull()
                if (decoded != null) return decoded
            }

            if (!legacyThemeRaw.isNullOrBlank()) {
                val legacy = CustomThemeConfig.decode(legacyThemeRaw)
                return CustomThemeLibrary(
                    activeThemeId = LEGACY_THEME_ID,
                    themes = listOf(
                        SavedCustomTheme(
                            id = LEGACY_THEME_ID,
                            config = legacy
                        )
                    )
                )
            }
            return Empty
        }

        private val JSON = Json {
            ignoreUnknownKeys = true
            encodeDefaults = true
        }
    }
}
