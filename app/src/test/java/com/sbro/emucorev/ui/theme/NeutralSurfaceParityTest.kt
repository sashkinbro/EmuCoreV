package com.sbro.emucorev.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NeutralSurfaceParityTest {
    @Test fun neutralFallbackSurfacesKeepReferenceElevationAndSwitchTrackLuminance() {
        // X leaves these roles at Material3 defaults. Strip the purple tint without
        // changing their elevation/contrast, especially the enabled OFF switch track.
        for ((name, reference) in listOf("Dark" to darkColorScheme(),
            "Light" to lightColorScheme(), "Pro" to darkColorScheme())) {
            val actual = scheme(name)
            for ((role, color) in neutralRoles(actual)) {
                val expected = neutralRoles(reference).getValue(role)
                assertEquals("$name/$role luminance", expected.luminance().toDouble(),
                    color.luminance().toDouble(), 0.003)
                val argb = color.toArgb()
                assertEquals("$name/$role neutral R/G", argb shr 16 and 255, argb shr 8 and 255)
                assertEquals("$name/$role neutral G/B", argb shr 8 and 255, argb and 255)
            }
        }
    }

    @Test fun darkOffSwitchTrackRemainsLighterThanItsEnabledThumb() {
        // Material3 uses outline for the OFF thumb/border and Highest for the track.
        // The previous port collapsed 292930 and 2A2A35 to almost the same luminance.
        for (name in listOf("Dark", "Pro")) {
            val actual = scheme(name)
            assertTrue(name, actual.surfaceContainerHighest.luminance() > actual.outline.luminance())
        }
    }

    private fun scheme(name: String): ColorScheme = Class.forName("com.sbro.emucorev.ui.theme.ThemeKt")
        .getDeclaredField("${name}ColorScheme").apply { isAccessible = true }.get(null) as ColorScheme

    private fun neutralRoles(s: ColorScheme): Map<String, Color> = linkedMapOf(
        "surfaceDim" to s.surfaceDim, "surfaceBright" to s.surfaceBright,
        "surfaceContainerLowest" to s.surfaceContainerLowest,
        "surfaceContainerLow" to s.surfaceContainerLow, "surfaceContainer" to s.surfaceContainer,
        "surfaceContainerHigh" to s.surfaceContainerHigh,
        "surfaceContainerHighest" to s.surfaceContainerHighest,
        "inverseSurface" to s.inverseSurface, "inverseOnSurface" to s.inverseOnSurface
    )
}
