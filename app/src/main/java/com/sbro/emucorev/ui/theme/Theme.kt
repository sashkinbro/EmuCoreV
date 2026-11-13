package com.sbro.emucorev.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.neon.NeonColorScheme
import com.sbro.emucorev.ui.theme.neon.NeonCrtOverlay
import com.sbro.emucorev.ui.theme.neon.NeonShapes
import com.sbro.emucorev.ui.theme.neon.neonMonospace

// X inherits Material3 neutral surface roles. Match their luminance without its purple tint.
// In particular, enabled OFF switches use surfaceContainerHighest behind the outline thumb.
private val DarkColorScheme = darkColorScheme(
    primary = AccentPrimary,
    onPrimary = OnAccent,
    primaryContainer = AccentPrimaryContainer,
    onPrimaryContainer = AccentPrimaryLight,
    secondary = SecondaryAccent,
    onSecondary = OnAccent,
    secondaryContainer = SecondaryContainer,
    onSecondaryContainer = SecondaryAccent,
    tertiary = TertiaryAccent,
    onTertiary = OnAccent,
    background = DarkBackground,
    onBackground = DarkOnBackground,
    surface = DarkSurface,
    surfaceTint = Color.Transparent,
    onSurface = DarkOnSurface,
    surfaceVariant = DarkSurfaceVariant,
    onSurfaceVariant = DarkOnSurfaceVariant,
    outline = DarkOutline,
    outlineVariant = DarkOutline,
    error = ErrorRed,
    onError = OnAccent,
    errorContainer = ErrorContainer,
    onErrorContainer = ErrorRed,
    tertiaryContainer = Color(0xFF10382A),
    onTertiaryContainer = TertiaryAccent,
    inversePrimary = AccentPrimaryDark,
    inverseSurface = Color(0xFFE2E2E2),
    inverseOnSurface = Color(0xFF303030),
    surfaceDim = Color(0xFF131313),
    surfaceBright = Color(0xFF393939),
    surfaceContainerLowest = Color(0xFF0E0E0E),
    surfaceContainerLow = Color(0xFF1C1C1C),
    surfaceContainer = Color(0xFF202020),
    surfaceContainerHigh = Color(0xFF2A2A2A),
    surfaceContainerHighest = Color(0xFF353535),
    primaryFixed = AccentPrimaryLightContainer,
    primaryFixedDim = AccentPrimaryLight,
    onPrimaryFixed = AccentPrimaryContainer,
    onPrimaryFixedVariant = AccentPrimaryDark,
    secondaryFixed = SecondaryLightContainer,
    secondaryFixedDim = SecondaryAccent,
    onSecondaryFixed = SecondaryContainer,
    onSecondaryFixedVariant = SecondaryAccentDark,
    tertiaryFixed = Color(0xFFCBF5E1),
    tertiaryFixedDim = TertiaryAccent,
    onTertiaryFixed = Color(0xFF10382A),
    onTertiaryFixedVariant = TertiaryDark,
    scrim = DarkScrim
)

private val LightColorScheme = lightColorScheme(
    primary = AccentPrimaryDark,
    onPrimary = OnAccent,
    primaryContainer = AccentPrimaryLightContainer,
    onPrimaryContainer = AccentPrimaryDark,
    secondary = SecondaryAccentDark,
    onSecondary = OnAccent,
    secondaryContainer = SecondaryLightContainer,
    onSecondaryContainer = SecondaryAccentDark,
    tertiary = TertiaryDark,
    onTertiary = OnAccent,
    background = LightBackground,
    onBackground = LightOnBackground,
    surface = LightSurface,
    surfaceTint = Color.Transparent,
    surfaceContainerLow = LightSurfaceContainerLow,
    surfaceContainer = LightSurfaceContainer,
    surfaceContainerHigh = LightSurfaceContainerHigh,
    surfaceContainerHighest = LightSurfaceContainerHighest,
    onSurface = LightOnSurface,
    surfaceVariant = LightSurfaceVariant,
    onSurfaceVariant = LightOnSurfaceVariant,
    outline = LightOutline,
    outlineVariant = LightOutline,
    error = ErrorRedLight,
    onError = OnAccent,
    errorContainer = ErrorLightContainer,
    onErrorContainer = ErrorRedLight,
    tertiaryContainer = Color(0xFFCBF5E1),
    onTertiaryContainer = Color(0xFF10382A),
    inversePrimary = AccentPrimaryLight,
    inverseSurface = Color(0xFF303030),
    inverseOnSurface = Color(0xFFF1F1F1),
    surfaceDim = Color(0xFFDADADA),
    surfaceBright = Color(0xFFF9F9F9),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    primaryFixed = AccentPrimaryLightContainer,
    primaryFixedDim = AccentPrimaryLight,
    onPrimaryFixed = AccentPrimaryContainer,
    onPrimaryFixedVariant = AccentPrimaryDark,
    secondaryFixed = SecondaryLightContainer,
    secondaryFixedDim = SecondaryAccent,
    onSecondaryFixed = SecondaryContainer,
    onSecondaryFixedVariant = SecondaryAccentDark,
    tertiaryFixed = Color(0xFFCBF5E1),
    tertiaryFixedDim = TertiaryAccent,
    onTertiaryFixed = Color(0xFF10382A),
    onTertiaryFixedVariant = TertiaryDark,
    scrim = DarkScrim
)

private val ProColorScheme = darkColorScheme(
    primary = ProPrimary,
    onPrimary = OnAccent,
    primaryContainer = ProPrimaryContainer,
    onPrimaryContainer = ProOnPrimaryContainer,
    secondary = ProSecondary,
    onSecondary = Color(0xFF21181C),
    secondaryContainer = ProSecondaryContainer,
    onSecondaryContainer = Color(0xFFE6D8DD),
    tertiary = ProTertiary,
    onTertiary = Color(0xFF1A0D00),
    background = ProBackground,
    onBackground = ProOnBackground,
    surface = ProSurface,
    surfaceTint = Color.Transparent,
    onSurface = ProOnSurface,
    surfaceVariant = ProSurfaceVariant,
    onSurfaceVariant = ProOnSurfaceVariant,
    outline = ProOutline,
    outlineVariant = ProOutline,
    error = ErrorRed,
    onError = OnAccent,
    errorContainer = ErrorContainer,
    onErrorContainer = ErrorRed,
    tertiaryContainer = Color(0xFF33260D),
    onTertiaryContainer = Color(0xFFFFE6AF),
    inversePrimary = Color(0xFFFFB1C0),
    inverseSurface = Color(0xFFE2E2E2),
    inverseOnSurface = Color(0xFF303030),
    surfaceDim = Color(0xFF131313),
    surfaceBright = Color(0xFF393939),
    surfaceContainerLowest = Color(0xFF0E0E0E),
    surfaceContainerLow = Color(0xFF1C1C1C),
    surfaceContainer = Color(0xFF202020),
    surfaceContainerHigh = Color(0xFF2A2A2A),
    surfaceContainerHighest = Color(0xFF353535),
    primaryFixed = ProOnPrimaryContainer,
    primaryFixedDim = Color(0xFFFFB1C0),
    onPrimaryFixed = ProPrimaryContainer,
    onPrimaryFixedVariant = ProPrimary,
    secondaryFixed = Color(0xFFE6D8DD),
    secondaryFixedDim = ProSecondary,
    onSecondaryFixed = ProSecondaryContainer,
    onSecondaryFixedVariant = ProOutline,
    tertiaryFixed = Color(0xFFFFE6AF),
    tertiaryFixedDim = ProTertiary,
    onTertiaryFixed = Color(0xFF33260D),
    onTertiaryFixedVariant = Color(0xFF674B19),
    scrim = ProScrim
)

enum class ThemeMode {
    SYSTEM, LIGHT, DARK, PRO, NEON
}

val LocalCustomizationSettings = staticCompositionLocalOf { CustomizationSettings() }

@Composable
fun EmuCoreVTheme(
    themeMode: ThemeMode = ThemeMode.SYSTEM,
    customization: CustomizationSettings = CustomizationSettings(),
    enableCrtOverlay: Boolean = true,
    content: @Composable () -> Unit
) {
    val darkTheme = when (themeMode) {
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
        ThemeMode.PRO -> true
        ThemeMode.NEON -> true
    }

    val baseTypography = rememberCustomizedTypography(customization)
    val typography = if (themeMode == ThemeMode.NEON) {
        baseTypography.neonMonospace()
    } else {
        baseTypography
    }
    val shapes: Shapes = if (themeMode == ThemeMode.NEON) NeonShapes else MaterialTheme.shapes
    MaterialTheme(
        colorScheme = when (themeMode) {
            ThemeMode.PRO -> ProColorScheme
            ThemeMode.NEON -> NeonColorScheme
            else -> if (darkTheme) DarkColorScheme else LightColorScheme
        },
        typography = typography,
        shapes = shapes
    ) {
        CompositionLocalProvider(
            LocalCustomizationSettings provides customization,
            LocalNeonTheme provides (themeMode == ThemeMode.NEON)
        ) {
            WindowAwareRipple {
                Box(modifier = Modifier.fillMaxSize()) {
                    content()
                    if (themeMode == ThemeMode.NEON && enableCrtOverlay) {
                        NeonCrtOverlay()
                    }
                }
            }
        }
    }
}
