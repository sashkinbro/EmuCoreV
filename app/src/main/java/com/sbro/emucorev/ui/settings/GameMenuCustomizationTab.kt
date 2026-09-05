package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.GameMenuLayoutStyle
import com.sbro.emucorev.ui.common.SectionCard

@Composable
internal fun GameMenuStyleSection(
    selected: GameMenuLayoutStyle,
    onSelected: (GameMenuLayoutStyle) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.settings_game_menu_tab),
        contentPadding = PaddingValues(vertical = 14.dp)
    ) {
        Text(
            text = stringResource(R.string.settings_game_menu_layout_help),
            modifier = Modifier.padding(horizontal = 14.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(GameMenuLayoutStyle.entries, key = { it.name }) { style ->
                StylePreviewCard(
                    selected = selected == style,
                    label = gameMenuLayoutLabel(style),
                    onClick = { onSelected(style) }
                ) {
                    GameMenuLayoutMiniature(style)
                }
            }
        }
    }
}

@Composable
private fun GameMenuLayoutMiniature(style: GameMenuLayoutStyle) {
    val primary = MaterialTheme.colorScheme.primary.copy(alpha = 0.64f)
    val row = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
    val panel = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.44f)
    when (style) {
        GameMenuLayoutStyle.SIDEBAR -> Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(10.dp),
            horizontalArrangement = Arrangement.spacedBy(7.dp)
        ) {
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .background(panel, neonShape(10.dp))
                    .padding(9.dp),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                MiniLine(primary, 0.56f)
                repeat(3) { MiniLine(row, 1f) }
            }
            Column(
                modifier = Modifier
                    .width(28.dp)
                    .fillMaxHeight()
                    .background(panel, neonShape(9.dp))
                    .padding(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                repeat(4) { MiniLine(if (it == 0) primary else row, 1f) }
            }
        }

        GameMenuLayoutStyle.DASHBOARD -> Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(10.dp)
                .background(panel, neonShape(9.dp))
        ) {
            Column(
                modifier = Modifier
                    .width(44.dp)
                    .fillMaxHeight()
                    .padding(7.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                repeat(4) { MiniLine(if (it == 0) primary else row, 1f) }
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                MiniLine(primary, 0.46f)
                repeat(3) { MiniLine(row, 1f) }
            }
        }

        GameMenuLayoutStyle.COMMAND_CENTER -> Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(10.dp)
                .background(panel, neonShape(9.dp))
                .padding(8.dp),
            verticalArrangement = Arrangement.spacedBy(7.dp)
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                repeat(4) {
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .height(11.dp)
                            .background(if (it == 0) primary else row, neonShape(4.dp))
                    )
                }
            }
            MiniLine(primary, 0.42f)
            repeat(3) { MiniLine(row, 1f) }
        }

        GameMenuLayoutStyle.COMPACT -> Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.CenterEnd
        ) {
            Column(
                modifier = Modifier
                    .width(96.dp)
                    .fillMaxHeight(0.92f)
                    .background(panel, neonShape(6.dp))
                    .padding(7.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    repeat(4) {
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .height(9.dp)
                                .background(if (it == 0) primary else row, neonShape(3.dp))
                        )
                    }
                }
                repeat(4) { MiniLine(row, 1f) }
            }
        }
    }
}

@Composable
private fun MiniLine(color: androidx.compose.ui.graphics.Color, fraction: Float) {
    Box(
        modifier = Modifier
            .fillMaxWidth(fraction)
            .height(8.dp)
            .background(color, neonShape(4.dp))
    )
}

@Composable
private fun gameMenuLayoutLabel(style: GameMenuLayoutStyle): String = stringResource(
    when (style) {
        GameMenuLayoutStyle.SIDEBAR -> R.string.settings_game_menu_layout_sidebar
        GameMenuLayoutStyle.DASHBOARD -> R.string.settings_game_menu_layout_dashboard
        GameMenuLayoutStyle.COMMAND_CENTER -> R.string.settings_game_menu_layout_command_center
        GameMenuLayoutStyle.COMPACT -> R.string.settings_game_menu_layout_compact
    }
)
