package com.sbro.emucorev.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import com.sbro.emucorev.data.theme.AccentEdge
import com.sbro.emucorev.data.theme.BackgroundPattern
import com.sbro.emucorev.data.theme.ButtonVariant
import com.sbro.emucorev.data.theme.CardVariant
import com.sbro.emucorev.data.theme.ChipVariant
import com.sbro.emucorev.data.theme.HeaderVariant
import com.sbro.emucorev.data.theme.IconContainer
import com.sbro.emucorev.data.theme.SwitchVariant
import com.sbro.emucorev.data.theme.ThemeDecor
import com.sbro.emucorev.data.theme.TitleCase
import com.sbro.emucorev.data.theme.ThemeShapeStyle
import com.sbro.emucorev.data.theme.ThemeStyleSpec
import com.sbro.emucorev.data.theme.UiStyle
import java.io.File

/**
 * Theme properties that Material's ColorScheme/Shapes/Typography cannot express: the corner style
 * used by every `neonShape(...)` call site, decor (Neon brackets/strips/banners), decor accents,
 * semantic status colors and overlay effects. Built-in themes and downloaded theme packages both
 * resolve to one of these.
 */
@Immutable
data class ThemeExtras(
    val shapeStyle: ThemeShapeStyle = ThemeShapeStyle.ROUNDED,
    val shapeScale: Float = 1f,
    val decor: ThemeDecor = ThemeDecor.NONE,
    val uiStyle: UiStyle = UiStyle.STANDARD,
    val accent1: Color = Color.Unspecified,
    val accent2: Color = Color.Unspecified,
    val accent3: Color = Color.Unspecified,
    val success: Color = DefaultSuccess,
    val warning: Color = DefaultWarning,
    val info: Color = DefaultInfo,
    val gold: Color = DefaultGold,
    val scanlines: Boolean = false,
    val backgroundFile: File? = null,
    val backgroundDim: Float = 0.35f,
    // Structural variants; every DEFAULT/NONE value reproduces the UI style's own look.
    val card: CardVariant = CardVariant.DEFAULT,
    val button: ButtonVariant = ButtonVariant.DEFAULT,
    val chip: ChipVariant = ChipVariant.DEFAULT,
    val switch: SwitchVariant = SwitchVariant.DEFAULT,
    val header: HeaderVariant = HeaderVariant.DEFAULT,
    /** Border width override in dp, null = the style's own. */
    val borderWidthDp: Float? = null,
    val accentEdge: AccentEdge = AccentEdge.NONE,
    val pattern: BackgroundPattern = BackgroundPattern.NONE,
    val titleCase: TitleCase = TitleCase.NONE,
    val titleLetterSpacingSp: Float = 0f,
    val iconContainer: IconContainer = IconContainer.DEFAULT
) {
    val neonDecor: Boolean get() = decor == ThemeDecor.NEON

    fun accent(index: Int): Color = when (index.mod(3)) {
        0 -> accent1
        1 -> accent2
        else -> accent3
    }

    companion object {
        val DefaultSuccess = Color(0xFF4CAF50)
        val DefaultWarning = Color(0xFFFFB300)
        val DefaultInfo = Color(0xFF42A5F5)
        val DefaultGold = Color(0xFFE9B949)
    }
}

val LocalThemeExtras = staticCompositionLocalOf { ThemeExtras() }

internal fun ThemeStyleSpec.toThemeExtras(
    primary: Color,
    secondary: Color,
    tertiary: Color,
    backgroundFile: File?
): ThemeExtras = ThemeExtras(
    shapeStyle = shapeStyle,
    shapeScale = shapeScale,
    decor = decor,
    uiStyle = uiStyle,
    accent1 = accents.getOrNull(0)?.let(::Color) ?: primary,
    accent2 = accents.getOrNull(1)?.let(::Color) ?: tertiary,
    accent3 = accents.getOrNull(2)?.let(::Color) ?: secondary,
    success = success?.let(::Color) ?: ThemeExtras.DefaultSuccess,
    warning = warning?.let(::Color) ?: ThemeExtras.DefaultWarning,
    info = info?.let(::Color) ?: ThemeExtras.DefaultInfo,
    gold = gold?.let(::Color) ?: ThemeExtras.DefaultGold,
    scanlines = scanlines,
    backgroundFile = backgroundFile,
    backgroundDim = backgroundDim,
    card = card,
    button = button,
    chip = chip,
    switch = switch,
    header = header,
    borderWidthDp = borderWidthDp,
    accentEdge = accentEdge,
    pattern = pattern,
    titleCase = titleCase,
    titleLetterSpacingSp = titleLetterSpacingSp,
    iconContainer = iconContainer
)
