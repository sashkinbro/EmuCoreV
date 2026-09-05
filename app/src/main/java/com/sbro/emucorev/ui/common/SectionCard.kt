package com.sbro.emucorev.ui.common

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.neon.NeonRed
import com.sbro.emucorev.ui.theme.neon.NeonYellow
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

@Composable
fun SectionCard(
    title: String,
    modifier: Modifier = Modifier,
    contentPadding: PaddingValues = PaddingValues(CardContentPadding),
    content: @Composable () -> Unit
) {
    val neon = LocalNeonTheme.current
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text = if (neon) title.uppercase() else title,
            style = MaterialTheme.typography.titleLarge.copy(
                fontWeight = FontWeight.SemiBold,
                letterSpacing = if (neon) 1.2.sp else MaterialTheme.typography.titleLarge.letterSpacing
            ),
            color = if (neon) NeonYellow else MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
        )
        if (neon) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding)
                    .height(1.dp)
                    .background(
                        Brush.horizontalGradient(
                            listOf(NeonRed, NeonYellow, Color.Transparent)
                        )
                    )
            ) {
                Box(modifier = Modifier.fillMaxWidth())
            }
        }
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding),
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = if (neon) 2.dp else 3.dp,
            shadowElevation = if (neon) 0.dp else 6.dp,
            border = BorderStroke(
                width = 1.dp,
                color = if (neon) {
                    NeonYellow.copy(alpha = 0.14f)
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f)
                }
            )
        ) {
            Column(
                modifier = Modifier.padding(contentPadding),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                content()
            }
        }
    }
}
