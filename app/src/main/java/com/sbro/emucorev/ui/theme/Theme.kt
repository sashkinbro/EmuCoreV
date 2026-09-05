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

private val DarkColorScheme = darkColorScheme(
    primary = AccentPrimary,
    onPrimary = OnAccent,
    primaryContainer = AccentPrimaryContainer,
    onPrimaryContainer = OnAccent,
    secondary = SecondaryAccent,
    onSecondary = OnAccent,
    secondaryContainer = SecondaryContainer,
    onSecondaryContainer = OnAccent,
    tertiary = TertiaryAccent,
    onTertiary = OnAccent,
    background = DarkBackground,
    onBackground = DarkOnBackground,
    surface = DarkSurface,
    surfaceTint = DarkSurface,
    onSurface = DarkOnSurface,
    surfaceVariant = DarkSurfaceVariant,
    onSurfaceVariant = DarkOnSurfaceVariant,
    outline = DarkOutline,
    outlineVariant = DarkOutline,
    error = ErrorRed,
    onError = OnAccent,
    errorContainer = ErrorContainer,
    onErrorContainer = ErrorRed,
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
    surfaceTint = LightSurface,
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
    onErrorContainer = ErrorRedLight
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
            Box(modifier = Modifier.fillMaxSize()) {
                content()
                if (themeMode == ThemeMode.NEON && enableCrtOverlay) {
                    NeonCrtOverlay()
                }
            }
        }
    }
}
