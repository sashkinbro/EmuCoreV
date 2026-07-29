package com.sbro.emucorev.ui.theme

import android.graphics.Typeface
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.TextUnitType
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomizationSettings

private val RubikFontFamily = FontFamily(
    Font(R.font.rubik_variable, FontWeight.Normal),
    Font(R.font.rubik_variable, FontWeight.Medium),
    Font(R.font.rubik_variable, FontWeight.SemiBold),
    Font(R.font.rubik_variable, FontWeight.Bold)
)

private val Exo2FontFamily = FontFamily(
    Font(R.font.exo2_variable, FontWeight.Normal),
    Font(R.font.exo2_variable, FontWeight.Medium),
    Font(R.font.exo2_variable, FontWeight.SemiBold),
    Font(R.font.exo2_variable, FontWeight.Bold)
)

@Composable
internal fun rememberCustomizedTypography(settings: CustomizationSettings): Typography {
    val fontFamily = when (settings.appFont) {
        AppFont.SYSTEM -> null
        AppFont.RUBIK -> RubikFontFamily
        AppFont.EXO2 -> Exo2FontFamily
        AppFont.CUSTOM -> remember(settings.customFontPath) {
            settings.customFontPath?.let { path ->
                runCatching { FontFamily(Typeface.createFromFile(path)) }.getOrNull()
            }
        }
    }
    return remember(fontFamily, settings.textSizePercent) {
        Typography.withCustomization(fontFamily, settings.textSizePercent / 100f)
    }
}

private fun Typography.withCustomization(fontFamily: FontFamily?, scale: Float): Typography = copy(
    displayLarge = displayLarge.customized(fontFamily, scale),
    displayMedium = displayMedium.customized(fontFamily, scale),
    displaySmall = displaySmall.customized(fontFamily, scale),
    headlineLarge = headlineLarge.customized(fontFamily, scale),
    headlineMedium = headlineMedium.customized(fontFamily, scale),
    headlineSmall = headlineSmall.customized(fontFamily, scale),
    titleLarge = titleLarge.customized(fontFamily, scale),
    titleMedium = titleMedium.customized(fontFamily, scale),
    titleSmall = titleSmall.customized(fontFamily, scale),
    bodyLarge = bodyLarge.customized(fontFamily, scale),
    bodyMedium = bodyMedium.customized(fontFamily, scale),
    bodySmall = bodySmall.customized(fontFamily, scale),
    labelLarge = labelLarge.customized(fontFamily, scale),
    labelMedium = labelMedium.customized(fontFamily, scale),
    labelSmall = labelSmall.customized(fontFamily, scale)
)

private fun TextStyle.customized(fontFamily: FontFamily?, scale: Float): TextStyle = copy(
    fontFamily = fontFamily,
    fontSize = fontSize.scaled(scale),
    lineHeight = lineHeight.scaled(scale)
)

private fun TextUnit.scaled(scale: Float): TextUnit {
    return if (type == TextUnitType.Sp) (value * scale).sp else this
}
