package com.sbro.emucorev.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.theme.ThemeFont
import java.io.File

// Bundled theme fonts (SIL OFL, see assets/licenses). Static fonts get one file per weight; the
// variable ones pin the weight axis per entry.
private val PixelFontFamily = FontFamily(Font(R.font.pressstart2p_regular, FontWeight.Normal))

private val SpaceMonoFontFamily = FontFamily(
    Font(R.font.spacemono_regular, FontWeight.Normal),
    Font(R.font.spacemono_regular, FontWeight.Medium),
    Font(R.font.spacemono_bold, FontWeight.SemiBold),
    Font(R.font.spacemono_bold, FontWeight.Bold)
)

private fun variableFamily(res: Int) = FontFamily(
    listOf(FontWeight.Normal, FontWeight.Medium, FontWeight.SemiBold, FontWeight.Bold).map { weight ->
        Font(res, weight, variationSettings = FontVariation.Settings(FontVariation.weight(weight.weight)))
    }
)

private val LoraFontFamily by lazy { variableFamily(R.font.lora_variable) }
private val OswaldFontFamily by lazy { variableFamily(R.font.oswald_variable) }

/** Family for a bundled theme font, or null for fonts handled elsewhere (system/Rubik/Exo/package). */
fun bundledThemeFamily(font: ThemeFont?): FontFamily? = when (font) {
    ThemeFont.PIXEL -> PixelFontFamily
    ThemeFont.SPACE_MONO -> SpaceMonoFontFamily
    ThemeFont.LORA -> LoraFontFamily
    ThemeFont.OSWALD -> OswaldFontFamily
    else -> null
}

/** Press Start 2P has a huge em box; shrink it so body text stays near the other fonts' size. */
fun themeFontSizeFactor(font: ThemeFont?): Float = if (font == ThemeFont.PIXEL) 0.75f else 1f

val Typography = Typography(
    displayLarge = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 32.sp,
        lineHeight = 40.sp,
        letterSpacing = (-0.5).sp
    ),
    headlineLarge = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 28.sp,
        lineHeight = 36.sp,
        letterSpacing = (-0.25).sp
    ),
    headlineMedium = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 24.sp,
        lineHeight = 32.sp
    ),
    headlineSmall = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 20.sp,
        lineHeight = 28.sp
    ),
    titleLarge = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 18.sp,
        lineHeight = 26.sp
    ),
    titleMedium = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 16.sp,
        lineHeight = 24.sp,
        letterSpacing = 0.15.sp
    ),
    titleSmall = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 14.sp,
        lineHeight = 20.sp,
        letterSpacing = 0.1.sp
    ),
    bodyLarge = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 16.sp,
        lineHeight = 24.sp,
        letterSpacing = 0.15.sp
    ),
    bodyMedium = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        lineHeight = 20.sp,
        letterSpacing = 0.25.sp
    ),
    bodySmall = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 12.sp,
        lineHeight = 16.sp,
        letterSpacing = 0.4.sp
    ),
    labelLarge = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 14.sp,
        lineHeight = 20.sp,
        letterSpacing = 0.1.sp
    ),
    labelMedium = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 12.sp,
        lineHeight = 16.sp,
        letterSpacing = 0.5.sp
    ),
    labelSmall = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        lineHeight = 16.sp,
        letterSpacing = 0.5.sp
    )
)

private fun TextUnit.scaledBy(scale: Float): TextUnit =
    if (this == TextUnit.Unspecified) this else (value * scale).sp

/** Scales every text size/line height by [factor] (used to tame fonts with a large em box). */
fun Typography.scaledText(factor: Float): Typography = if (factor == 1f) this else copy(
    displayLarge = displayLarge.scaledText(factor), displayMedium = displayMedium.scaledText(factor),
    displaySmall = displaySmall.scaledText(factor), headlineLarge = headlineLarge.scaledText(factor),
    headlineMedium = headlineMedium.scaledText(factor), headlineSmall = headlineSmall.scaledText(factor),
    titleLarge = titleLarge.scaledText(factor), titleMedium = titleMedium.scaledText(factor),
    titleSmall = titleSmall.scaledText(factor), bodyLarge = bodyLarge.scaledText(factor),
    bodyMedium = bodyMedium.scaledText(factor), bodySmall = bodySmall.scaledText(factor),
    labelLarge = labelLarge.scaledText(factor), labelMedium = labelMedium.scaledText(factor),
    labelSmall = labelSmall.scaledText(factor)
)

private fun TextStyle.scaledText(factor: Float): TextStyle = copy(
    fontSize = fontSize.scaledBy(factor),
    lineHeight = lineHeight.scaledBy(factor)
)

/** Adds extra letter spacing (sp) to titles and headlines only. */
fun Typography.withTitleLetterSpacing(extraSp: Float): Typography = if (extraSp == 0f) this else copy(
    displayLarge = displayLarge.spaced(extraSp), displayMedium = displayMedium.spaced(extraSp),
    displaySmall = displaySmall.spaced(extraSp), headlineLarge = headlineLarge.spaced(extraSp),
    headlineMedium = headlineMedium.spaced(extraSp), headlineSmall = headlineSmall.spaced(extraSp),
    titleLarge = titleLarge.spaced(extraSp), titleMedium = titleMedium.spaced(extraSp),
    titleSmall = titleSmall.spaced(extraSp)
)

private fun TextStyle.spaced(extraSp: Float): TextStyle =
    copy(letterSpacing = (if (letterSpacing == TextUnit.Unspecified) 0f else letterSpacing.value).plus(extraSp).sp)

/**
 * Family for a user/Store font file. Declaring real weights makes variable fonts load at wght 400/500/600/700
 * (their default instance is often Thin or Light, e.g. Montserrat, Manrope, Raleway); static fonts ignore the
 * variation settings and keep synthesized bold.
 */
fun fileFontFamily(file: File): FontFamily = FontFamily(
    Font(file, FontWeight.Normal),
    Font(file, FontWeight.Medium),
    Font(file, FontWeight.SemiBold),
    Font(file, FontWeight.Bold)
)
