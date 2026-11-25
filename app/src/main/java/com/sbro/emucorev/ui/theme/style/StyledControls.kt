package com.sbro.emucorev.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SelectableChipColors
import androidx.compose.material3.SelectableChipElevation
import androidx.compose.material3.SwitchColors
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.theme.ChipVariant
import com.sbro.emucorev.data.theme.SwitchVariant
import com.sbro.emucorev.data.theme.UiStyle
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import androidx.compose.material3.FilterChip as M3FilterChip
import androidx.compose.material3.Switch as M3Switch

/** Style decoration drawn over a chip; keeps Material's colors/semantics untouched. */
@Composable
private fun chipDecoration(selected: Boolean, shape: Shape): Modifier {
    val scheme = MaterialTheme.colorScheme
    return when (LocalThemeExtras.current.uiStyle) {
        UiStyle.CLASSIC -> Modifier.overlayInShape(shape) {
            drawBevel(Color.White, Color(0xFF404040), Color(0xFF808080), sunken = selected)
        }
        UiStyle.LUNA, UiStyle.S60, UiStyle.AERO, UiStyle.BUBBLE -> Modifier.overlayInShape(shape) {
            drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = 0.38f), 0.5f to Color.Transparent))
        }
        UiStyle.PIXEL -> Modifier.overlayInShape(shape) {
            drawPixelFrame(if (selected) scheme.primary else scheme.outline)
        }
        else -> Modifier
    }
}

@Composable
fun FilterChip(
    selected: Boolean,
    onClick: () -> Unit,
    label: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    leadingIcon: @Composable (() -> Unit)? = null,
    trailingIcon: @Composable (() -> Unit)? = null,
    shape: Shape = FilterChipDefaults.shape,
    colors: SelectableChipColors = FilterChipDefaults.filterChipColors(),
    elevation: SelectableChipElevation? = FilterChipDefaults.filterChipElevation(),
    border: BorderStroke? = FilterChipDefaults.filterChipBorder(enabled, selected),
    interactionSource: MutableInteractionSource? = null
) {
    val extras = LocalThemeExtras.current
    val style = extras.uiStyle
    val variant = extras.chip
    val variantShape: Shape? = when (variant) {
        ChipVariant.DEFAULT -> null
        ChipVariant.PILL -> RoundedCornerShape(50)
        ChipVariant.SQUARE -> RoundedCornerShape(2.dp)
        ChipVariant.TAG -> TagChipShape
    }
    val effectiveShape = variantShape ?: shape
    val decorated = style in setOf(UiStyle.CLASSIC, UiStyle.LUNA, UiStyle.S60, UiStyle.AERO, UiStyle.BUBBLE, UiStyle.PIXEL)
    StyledTouchTarget(decorated || variant != ChipVariant.DEFAULT) { M3FilterChip(
        selected = selected,
        onClick = onClick,
        label = label,
        modifier = modifier.then(chipDecoration(selected, effectiveShape)),
        enabled = enabled,
        leadingIcon = leadingIcon,
        trailingIcon = trailingIcon,
        shape = effectiveShape,
        colors = colors,
        elevation = elevation,
        border = if (style == UiStyle.CLASSIC || style == UiStyle.PIXEL) null else border,
        interactionSource = interactionSource
    ) }
}

@Composable
fun Switch(
    checked: Boolean,
    onCheckedChange: ((Boolean) -> Unit)?,
    modifier: Modifier = Modifier,
    thumbContent: (@Composable () -> Unit)? = null,
    enabled: Boolean = true,
    colors: SwitchColors = SwitchDefaults.colors(),
    interactionSource: MutableInteractionSource? = null
) {
    val extras = LocalThemeExtras.current
    val style = extras.uiStyle
    val scheme = MaterialTheme.colorScheme
    val toggle = if (onCheckedChange != null) {
        Modifier.toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
    } else Modifier
    // components.switch overrides the style's own switch drawing.
    when (extras.switch) {
        SwitchVariant.TRACK -> {
            M3Switch(checked, onCheckedChange, modifier, thumbContent, enabled, colors, interactionSource)
            return
        }
        SwitchVariant.CHECKBOX -> {
            val shape = RoundedCornerShape(5.dp)
            Box(
                modifier = modifier.then(toggle).size(24.dp)
                    .background(if (checked) scheme.primary else Color.Transparent, shape)
                    .border((extras.borderWidthDp?.takeIf { it > 0f } ?: 2f).dp, if (checked) scheme.primary else scheme.outline, shape),
                contentAlignment = Alignment.Center
            ) {
                if (checked) Icon(Icons.Rounded.Check, contentDescription = null, tint = scheme.onPrimary, modifier = Modifier.size(18.dp))
            }
            return
        }
        SwitchVariant.LED -> {
            // A lit dot: filled with a soft halo when on, a hollow ring when off.
            Box(modifier = modifier.then(toggle).size(32.dp), contentAlignment = Alignment.Center) {
                if (checked) {
                    Box(Modifier.size(26.dp).background(scheme.primary.copy(alpha = 0.22f), CircleShape))
                    Box(Modifier.size(14.dp).background(scheme.primary, CircleShape))
                } else {
                    Box(Modifier.size(14.dp).border(2.dp, scheme.outline, CircleShape))
                }
            }
            return
        }
        SwitchVariant.DEFAULT -> Unit
    }
    when (style) {
        // Classic desktops used check boxes: a sunken white well with a tick.
        UiStyle.CLASSIC -> Box(
            modifier = modifier.then(toggle).size(26.dp)
                .background(if (enabled) Color.White else Color(0xFFD4D0C8))
                .drawWithContent {
                    drawContent()
                    drawBevel(Color.White, Color(0xFF404040), Color(0xFF808080), sunken = true)
                },
            contentAlignment = Alignment.Center
        ) {
            if (checked) Icon(Icons.Rounded.Check, contentDescription = null, tint = Color.Black, modifier = Modifier.size(20.dp))
        }
        UiStyle.TERMINAL -> Box(modifier = modifier.then(toggle), contentAlignment = Alignment.Center) {
            Text(
                text = if (checked) "[x]" else "[ ]",
                color = if (checked) scheme.primary else scheme.onSurfaceVariant,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )
        }
        UiStyle.PIXEL -> Box(
            modifier = modifier.then(toggle).size(width = 48.dp, height = 24.dp)
                .background(if (checked) scheme.primary else scheme.surfaceVariant)
                .border(2.dp, scheme.outline, RectangleShape),
            contentAlignment = if (checked) Alignment.CenterEnd else Alignment.CenterStart
        ) {
            Box(Modifier.size(20.dp).background(if (checked) scheme.onPrimary else scheme.outline))
        }
        else -> M3Switch(checked, onCheckedChange, modifier, thumbContent, enabled, colors, interactionSource)
    }
}
