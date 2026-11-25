package com.sbro.emucorev.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomThemeConfig
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.data.theme.InstalledThemePackage
import com.sbro.emucorev.data.theme.ThemeDecor
import com.sbro.emucorev.data.theme.ThemeFont
import com.sbro.emucorev.data.theme.ThemeShapeStyle
import com.sbro.emucorev.data.theme.UiStyle
import com.sbro.emucorev.data.theme.toCustomThemeConfig
import com.sbro.emucorev.data.theme.toStyleSpec
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.neon.NeonBlue
import com.sbro.emucorev.ui.theme.neon.NeonRed
import com.sbro.emucorev.ui.theme.neon.NeonYellow
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
    // Material3 replaces tint alpha at elevation; transparent black would darken light cards.
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
    SYSTEM, LIGHT, DARK, PRO, NEON, PACKAGE
}

/** Whether [this] mode renders dark; shared with MainActivity's window background. */
fun ThemeMode.isDark(
    systemDark: Boolean,
    themePackage: InstalledThemePackage?
): Boolean = when (this) {
    ThemeMode.SYSTEM -> systemDark
    ThemeMode.LIGHT -> false
    ThemeMode.DARK -> true
    ThemeMode.PRO -> true
    ThemeMode.NEON -> true
    ThemeMode.PACKAGE -> themePackage
        ?.takeIf { it.isSupported }
        ?.let { pkg -> pkg.spec.toCustomThemeConfig("en").dark }
        ?: systemDark
}

private val NeonExtras = ThemeExtras(
    shapeStyle = ThemeShapeStyle.CUT_DIAGONAL,
    decor = ThemeDecor.NEON,
    uiStyle = UiStyle.NEON,
    accent1 = NeonYellow,
    accent2 = NeonBlue,
    accent3 = NeonRed,
    scanlines = true
)

val LocalCustomizationSettings = staticCompositionLocalOf { CustomizationSettings() }

@Composable
fun EmuCoreVTheme(
    themeMode: ThemeMode = ThemeMode.SYSTEM,
    customization: CustomizationSettings = CustomizationSettings(),
    themePackage: InstalledThemePackage? = null,
    enableCrtOverlay: Boolean = true,
    content: @Composable () -> Unit
) {
    // A package theme that is missing or unsupported by this app version renders as SYSTEM.
    val activePackage = themePackage?.takeIf { themeMode == ThemeMode.PACKAGE && it.isSupported }
    val effectiveMode = if (themeMode == ThemeMode.PACKAGE && activePackage == null) ThemeMode.SYSTEM else themeMode
    val systemDark = isSystemInDarkTheme()
    val localeTag = LocalConfiguration.current.locales[0]?.language ?: "en"

    val packageConfig = remember(activePackage, localeTag) {
        activePackage?.spec?.toCustomThemeConfig(localeTag)
    }
    val packageStyle = remember(activePackage) { activePackage?.spec?.toStyleSpec() }
    val darkTheme = effectiveMode.isDark(systemDark, activePackage)

    val colorScheme = when (effectiveMode) {
        ThemeMode.PRO -> ProColorScheme
        ThemeMode.NEON -> NeonColorScheme
        ThemeMode.PACKAGE -> packageConfig!!.toColorScheme()
        else -> if (darkTheme) DarkColorScheme else LightColorScheme
    }
    val extras = remember(effectiveMode, colorScheme, packageStyle, activePackage) {
        when (effectiveMode) {
            ThemeMode.NEON -> NeonExtras
            ThemeMode.PACKAGE -> packageStyle!!.toThemeExtras(
                primary = colorScheme.primary,
                secondary = colorScheme.secondary,
                tertiary = colorScheme.tertiary,
                backgroundFile = activePackage?.backgroundFile
            )
            else -> ThemeExtras(
                accent1 = colorScheme.primary,
                accent2 = colorScheme.tertiary,
                accent3 = colorScheme.secondary
            )
        }
    }
    val shapes: Shapes = when (effectiveMode) {
        ThemeMode.NEON -> NeonShapes
        ThemeMode.PACKAGE -> packageConfig!!.toShapes(extras.shapeStyle, extras.shapeScale)
        else -> MaterialTheme.shapes
    }

    val packageFont = packageStyle?.font
    val packageFontFile = activePackage?.fontFile
    val effectiveCustomization = when {
        packageFont == ThemeFont.PACKAGE && packageFontFile != null ->
            customization.copy(appFont = AppFont.CUSTOM, customFontPath = packageFontFile.absolutePath)
        packageFont == ThemeFont.SYSTEM -> customization.copy(appFont = AppFont.SYSTEM)
        packageFont == ThemeFont.RUBIK -> customization.copy(appFont = AppFont.RUBIK)
        packageFont == ThemeFont.EXO2 -> customization.copy(appFont = AppFont.EXO2)
        else -> customization
    }
    val baseTypography = rememberCustomizedTypography(effectiveCustomization)
    val bundledFamily = bundledThemeFamily(packageFont)
    val typography = when {
        effectiveMode == ThemeMode.NEON -> baseTypography.neonMonospace()
        packageFont == ThemeFont.MONOSPACE -> baseTypography.withFamily(FontFamily.Monospace)
        packageFont == ThemeFont.SERIF -> baseTypography.withFamily(FontFamily.Serif)
        bundledFamily != null -> baseTypography.withFamily(bundledFamily).scaledText(themeFontSizeFactor(packageFont))
        else -> baseTypography
    }.withTitleLetterSpacing(if (effectiveMode == ThemeMode.PACKAGE) extras.titleLetterSpacingSp else 0f)
    MaterialTheme(
        colorScheme = colorScheme,
        typography = typography,
        shapes = shapes
    ) {
        CompositionLocalProvider(
            LocalCustomizationSettings provides customization,
            LocalThemeExtras provides extras,
            LocalNeonTheme provides extras.neonDecor
        ) {
            WindowAwareRipple {
                Box(modifier = Modifier.fillMaxSize()) {
                    content()
                    com.sbro.emucorev.ui.theme.style.ThemePatternOverlay(extras.pattern)
                    if (extras.scanlines && enableCrtOverlay) {
                        NeonCrtOverlay()
                    }
                }
            }
        }
    }
}

private fun Typography.withFamily(family: FontFamily): Typography = copy(
    displayLarge = displayLarge.copy(fontFamily = family),
    displayMedium = displayMedium.copy(fontFamily = family),
    displaySmall = displaySmall.copy(fontFamily = family),
    headlineLarge = headlineLarge.copy(fontFamily = family),
    headlineMedium = headlineMedium.copy(fontFamily = family),
    headlineSmall = headlineSmall.copy(fontFamily = family),
    titleLarge = titleLarge.copy(fontFamily = family),
    titleMedium = titleMedium.copy(fontFamily = family),
    titleSmall = titleSmall.copy(fontFamily = family),
    bodyLarge = bodyLarge.copy(fontFamily = family),
    bodyMedium = bodyMedium.copy(fontFamily = family),
    bodySmall = bodySmall.copy(fontFamily = family),
    labelLarge = labelLarge.copy(fontFamily = family),
    labelMedium = labelMedium.copy(fontFamily = family),
    labelSmall = labelSmall.copy(fontFamily = family)
)

/**
 * Full Material scheme for a custom/package theme. Container, dim/bright and inverse roles are
 * derived from the theme's own surface/background/text colors; leaving them at Material's
 * baseline would tint cards, menus and dialogs with the default lilac.
 */
fun CustomThemeConfig.toColorScheme(): ColorScheme = baseColorScheme().withDerivedRoles(dark)

private fun ColorScheme.withDerivedRoles(dark: Boolean): ColorScheme {
    val toward = onSurface
    fun step(amount: Float) = lerp(surface, toward, amount)
    return copy(
        surfaceDim = if (dark) lerp(background, Color.Black, 0.25f) else lerp(surface, toward, 0.10f),
        surfaceBright = if (dark) step(0.12f) else lerp(surface, Color.White, 0.6f),
        surfaceContainerLowest = if (dark) lerp(background, Color.Black, 0.35f) else lerp(surface, Color.White, 0.7f),
        surfaceContainerLow = if (dark) lerp(background, surface, 0.6f) else lerp(surface, Color.White, 0.35f),
        surfaceContainer = surface,
        surfaceContainerHigh = step(if (dark) 0.05f else 0.04f),
        surfaceContainerHighest = surfaceVariant,
        inverseSurface = onSurface,
        inverseOnSurface = surface,
        inversePrimary = lerp(primary, if (dark) Color.Black else Color.White, 0.35f)
    )
}

private fun CustomThemeConfig.baseColorScheme() = if (dark) {
    darkColorScheme(
        primary = Color(primary),
        onPrimary = Color(onPrimary),
        primaryContainer = Color(primaryContainer),
        onPrimaryContainer = Color(onPrimaryContainer),
        secondary = Color(secondary),
        onSecondary = Color(onSecondary),
        secondaryContainer = Color(secondaryContainer),
        onSecondaryContainer = Color(onSecondaryContainer),
        tertiary = Color(tertiary),
        onTertiary = Color(onTertiary),
        tertiaryContainer = Color(tertiaryContainer),
        onTertiaryContainer = Color(onTertiaryContainer),
        background = Color(background),
        onBackground = Color(onBackground),
        surface = Color(surface),
        onSurface = Color(onSurface),
        surfaceVariant = Color(surfaceVariant),
        onSurfaceVariant = Color(onSurfaceVariant),
        surfaceTint = Color.Transparent,
        outline = Color(outline),
        outlineVariant = Color(outlineVariant),
        error = Color(error),
        onError = Color(onError),
        errorContainer = Color(errorContainer),
        onErrorContainer = Color(onErrorContainer),
        scrim = Color.Black.copy(alpha = 0.72f)
    )
} else {
    lightColorScheme(
        primary = Color(primary),
        onPrimary = Color(onPrimary),
        primaryContainer = Color(primaryContainer),
        onPrimaryContainer = Color(onPrimaryContainer),
        secondary = Color(secondary),
        onSecondary = Color(onSecondary),
        secondaryContainer = Color(secondaryContainer),
        onSecondaryContainer = Color(onSecondaryContainer),
        tertiary = Color(tertiary),
        onTertiary = Color(onTertiary),
        tertiaryContainer = Color(tertiaryContainer),
        onTertiaryContainer = Color(onTertiaryContainer),
        background = Color(background),
        onBackground = Color(onBackground),
        surface = Color(surface),
        onSurface = Color(onSurface),
        surfaceVariant = Color(surfaceVariant),
        onSurfaceVariant = Color(onSurfaceVariant),
        surfaceTint = Color.Transparent,
        outline = Color(outline),
        outlineVariant = Color(outlineVariant),
        error = Color(error),
        onError = Color(onError),
        errorContainer = Color(errorContainer),
        onErrorContainer = Color(onErrorContainer),
        scrim = Color.Black.copy(alpha = 0.48f)
    )
}

fun CustomThemeConfig.toShapes(): Shapes {
    val safe = sanitized()
    return Shapes(
        extraSmall = RoundedCornerShape((safe.smallCornerDp * 0.65f).dp),
        small = RoundedCornerShape(safe.smallCornerDp.dp),
        medium = RoundedCornerShape(safe.mediumCornerDp.dp),
        large = RoundedCornerShape(safe.largeCornerDp.dp),
        extraLarge = RoundedCornerShape((safe.largeCornerDp * 1.25f).coerceAtMost(60f).dp)
    )
}

/** Material shapes for a package theme: the custom radii, scaled, in the package's corner style. */
fun CustomThemeConfig.toShapes(style: ThemeShapeStyle, scale: Float): Shapes {
    val safe = sanitized()
    fun corner(dp: Float): CornerBasedShape {
        val size = (dp * scale).coerceAtLeast(0f).dp
        return when (style) {
            ThemeShapeStyle.ROUNDED -> RoundedCornerShape(size)
            ThemeShapeStyle.CUT -> CutCornerShape(size)
            ThemeShapeStyle.CUT_DIAGONAL -> CutCornerShape(topEnd = size, bottomStart = size)
            ThemeShapeStyle.SQUARE -> RoundedCornerShape(0.dp)
        }
    }
    return Shapes(
        extraSmall = corner(safe.smallCornerDp * 0.65f),
        small = corner(safe.smallCornerDp),
        medium = corner(safe.mediumCornerDp),
        large = corner(safe.largeCornerDp),
        extraLarge = corner((safe.largeCornerDp * 1.25f).coerceAtMost(60f))
    )
}
