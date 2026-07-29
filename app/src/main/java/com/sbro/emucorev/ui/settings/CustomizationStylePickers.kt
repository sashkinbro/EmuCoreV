package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.data.DrawerVisualStyle
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.VectorAnalogStick
import com.sbro.emucorev.ui.common.VectorOverlayButton

@Composable
internal fun TouchControlStyleSection(
    settings: CustomizationSettings,
    onStyleSelected: (TouchControlVisualStyle) -> Unit,
    onPressEffectSelected: (TouchControlPressEffect) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_customization_touch_controls_section),
        contentPadding = PaddingValues(vertical = 14.dp)
    ) {
        TouchControlsPreview(
            style = settings.touchControlVisualStyle,
            pressEffect = settings.touchControlPressEffect
        )
        Text(
            text = stringResource(R.string.settings_customization_touch_controls_style),
            modifier = Modifier.padding(horizontal = 14.dp),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
        )
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(TouchControlVisualStyle.entries, key = { it.name }) { style ->
                FilterChip(
                    selected = settings.touchControlVisualStyle == style,
                    onClick = { onStyleSelected(style) },
                    label = { Text(touchStyleLabel(style)) }
                )
            }
        }
        Text(
            text = stringResource(R.string.settings_customization_touch_press_effect),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp)
        )
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(TouchControlPressEffect.entries, key = { it.name }) { effect ->
                FilterChip(
                    selected = settings.touchControlPressEffect == effect,
                    onClick = { onPressEffectSelected(effect) },
                    label = { Text(touchPressEffectLabel(effect)) }
                )
            }
        }
        Text(
            text = stringResource(R.string.settings_customization_touch_press_effect_help),
            modifier = Modifier.padding(horizontal = 14.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = stringResource(R.string.settings_customization_touch_controls_help),
            modifier = Modifier.padding(horizontal = 14.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
internal fun DrawerStyleSection(
    selected: DrawerVisualStyle,
    onSelected: (DrawerVisualStyle) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_customization_drawer_section),
        contentPadding = PaddingValues(vertical = 14.dp)
    ) {
        Text(
            text = stringResource(R.string.settings_customization_drawer_style),
            modifier = Modifier.padding(horizontal = 14.dp),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
        )
        LazyRow(
            modifier = Modifier
                .fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(DrawerVisualStyle.entries, key = { it.name }) { style ->
                StylePreviewCard(
                    selected = selected == style,
                    label = drawerStyleLabel(style),
                    onClick = { onSelected(style) }
                ) {
                    DrawerStyleMiniature(style)
                }
            }
        }
    }
}

@Composable
internal fun StylePreviewCard(
    selected: Boolean,
    label: String,
    onClick: () -> Unit,
    preview: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier
            .width(176.dp)
            .height(132.dp)
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(20.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.58f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f)
        },
        border = BorderStroke(
            if (selected) 2.dp else 1.dp,
            if (selected) MaterialTheme.colorScheme.primary
            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.54f)
        )
    ) {
        Column(
            modifier = Modifier.padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(
                        MaterialTheme.colorScheme.background,
                        RoundedCornerShape(14.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                preview()
            }
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = if (selected) MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun TouchControlsPreview(
    style: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect
) {
    Surface(
        modifier = Modifier
            .padding(horizontal = 14.dp)
            .fillMaxWidth()
            .height(124.dp),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 22.dp, vertical = 18.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            VectorAnalogStick(
                analogSize = 76.dp,
                visualStyle = style,
                pressEffect = pressEffect,
                pressed = true,
                interactive = false
            )
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                VectorOverlayButton(
                    drawableRes = R.drawable.ic_controller_square_button,
                    width = 44.dp,
                    height = 44.dp,
                    visualStyle = style,
                    pressEffect = pressEffect,
                    interactive = false
                )
                VectorOverlayButton(
                    drawableRes = R.drawable.ic_controller_cross_button,
                    width = 44.dp,
                    height = 44.dp,
                    visualStyle = style,
                    pressEffect = pressEffect,
                    pressed = true,
                    interactive = false
                )
            }
        }
    }
}

@Composable
private fun DrawerStyleMiniature(style: DrawerVisualStyle) {
    val shape = when (style) {
        DrawerVisualStyle.CLASSIC -> RoundedCornerShape(12.dp)
        DrawerVisualStyle.COMPACT -> RoundedCornerShape(4.dp)
        DrawerVisualStyle.GLASS -> RoundedCornerShape(18.dp)
        DrawerVisualStyle.CONSOLE -> RoundedCornerShape(1.dp)
    }
    val width = when (style) {
        DrawerVisualStyle.COMPACT -> 76.dp
        DrawerVisualStyle.CONSOLE -> 116.dp
        else -> 96.dp
    }
    val fill = when (style) {
        DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surface.copy(alpha = 0.52f)
        DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.surfaceContainerLowest
        else -> MaterialTheme.colorScheme.surface
    }
    Column(
        modifier = Modifier
            .width(width)
            .height(76.dp)
            .background(fill, shape)
            .border(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f), shape)
            .padding(if (style == DrawerVisualStyle.COMPACT) 6.dp else 9.dp),
        verticalArrangement = Arrangement.spacedBy(if (style == DrawerVisualStyle.COMPACT) 4.dp else 6.dp)
    ) {
        repeat(if (style == DrawerVisualStyle.COMPACT) 5 else 4) { index ->
            Box(
                modifier = Modifier
                    .fillMaxWidth(if (index == 0) 0.92f else 0.78f)
                    .height(if (style == DrawerVisualStyle.CONSOLE) 9.dp else 8.dp)
                    .background(
                        if (index == 0) MaterialTheme.colorScheme.primary.copy(alpha = 0.72f)
                        else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        RoundedCornerShape(if (style == DrawerVisualStyle.CONSOLE) 1.dp else 5.dp)
                    )
            )
        }
    }
}

@Composable
private fun touchStyleLabel(style: TouchControlVisualStyle): String = stringResource(
    when (style) {
        TouchControlVisualStyle.CLASSIC -> R.string.settings_customization_touch_style_classic
        TouchControlVisualStyle.LEGACY -> R.string.settings_customization_touch_style_glass
        TouchControlVisualStyle.MODERN -> R.string.settings_customization_touch_style_neon
        TouchControlVisualStyle.ARCADE -> R.string.settings_customization_touch_style_arcade
        TouchControlVisualStyle.MINIMAL -> R.string.settings_customization_touch_style_minimal
    }
)

@Composable
private fun touchPressEffectLabel(effect: TouchControlPressEffect): String = stringResource(
    when (effect) {
        TouchControlPressEffect.GROW -> R.string.settings_customization_touch_press_effect_grow
        TouchControlPressEffect.SHRINK -> R.string.settings_customization_touch_press_effect_shrink
        TouchControlPressEffect.SPRING -> R.string.settings_customization_touch_press_effect_spring
        TouchControlPressEffect.GLOW -> R.string.settings_customization_touch_press_effect_glow
    }
)

@Composable
private fun drawerStyleLabel(style: DrawerVisualStyle): String = stringResource(
    when (style) {
        DrawerVisualStyle.CLASSIC -> R.string.settings_drawer_style_classic
        DrawerVisualStyle.COMPACT -> R.string.settings_drawer_style_compact
        DrawerVisualStyle.GLASS -> R.string.settings_drawer_style_glass
        DrawerVisualStyle.CONSOLE -> R.string.settings_drawer_style_console
    }
)
