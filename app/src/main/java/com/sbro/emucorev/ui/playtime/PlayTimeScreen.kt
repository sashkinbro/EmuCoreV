package com.sbro.emucorev.ui.playtime

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.QueryStats
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.PlayTimeSession
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset
import java.text.DateFormat
import java.util.Date

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PlayTimeScreen(
    focusTitleId: String? = null,
    onMenuClick: (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    viewModel: PlayTimeViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset

    LaunchedEffect(focusTitleId) {
        viewModel.refresh(focusTitleId)
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            PlayTimeTopBar(
                onMenuClick = onMenuClick,
                onBackClick = onBackClick,
                onRefreshClick = { viewModel.refresh(uiState.selectedTitleId ?: focusTitleId) },
                modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
            )
        }

        if (uiState.isLoading) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 360.dp),
                    contentAlignment = Alignment.Center
                ) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }
        } else {
            item {
                GameSelector(
                    games = uiState.gameStats,
                    selectedTitleId = uiState.selectedTitleId,
                    showAllGames = focusTitleId == null,
                    onSelect = viewModel::selectGame
                )
            }

            item {
                SummaryGrid(
                    uiState = uiState,
                    modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                )
            }

            if (uiState.visibleSessions.isEmpty()) {
                item {
                    EmptyPlayTimeState(
                        body = stringResource(
                            if (uiState.selectedTitleId == null) {
                                R.string.play_time_no_sessions_body
                            } else {
                                R.string.play_time_no_game_sessions_body
                            }
                        ),
                        modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                    )
                }
            } else {
                if (uiState.selectedTitleId == null) {
                    item {
                        PlayTimeSection(
                            title = stringResource(R.string.play_time_daily_chart),
                            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                        ) {
                            DayChart(days = uiState.dayStats)
                        }
                    }
                    item {
                        PlayTimeSection(
                            title = stringResource(R.string.play_time_games_chart),
                            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                        ) {
                            GameTotalsChart(games = uiState.gameStats.filter { it.totalMs > 0L }.take(6))
                        }
                    }
                }
                item {
                    PlayTimeSection(
                        title = stringResource(R.string.play_time_recent_sessions),
                        modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                    ) {
                        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            uiState.visibleSessions.take(18).forEach { session ->
                                SessionRow(session = session)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun PlayTimeTopBar(
    onMenuClick: (() -> Unit)?,
    onBackClick: (() -> Unit)?,
    onRefreshClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (onBackClick != null) {
                NavigationBackButton(onClick = onBackClick)
            } else if (onMenuClick != null) {
                NavigationMenuButton(onClick = onMenuClick)
            }
            Text(
                text = stringResource(R.string.play_time_title),
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(start = if (onBackClick != null || onMenuClick != null) 14.dp else 0.dp)
            )
        }
        IconButton(onClick = onRefreshClick) {
            Icon(
                imageVector = Icons.Rounded.Refresh,
                contentDescription = stringResource(R.string.library_refresh),
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun GameSelector(
    games: List<PlayTimeGameStats>,
    selectedTitleId: String?,
    showAllGames: Boolean,
    onSelect: (String?) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = stringResource(R.string.play_time_filter_game),
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
        )
        LazyRow(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding)
        ) {
            if (showAllGames) {
                item {
                    FilterChip(
                        selected = selectedTitleId == null,
                        onClick = { onSelect(null) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                            selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                        ),
                        label = { Text(stringResource(R.string.play_time_all_games)) },
                        leadingIcon = {
                            Icon(Icons.Rounded.QueryStats, contentDescription = null)
                        }
                    )
                }
            }
            items(games, key = { it.titleId }) { game ->
                GameSelectorCard(
                    game = game,
                    selected = game.titleId.equals(selectedTitleId, ignoreCase = true),
                    onClick = { onSelect(game.titleId) }
                )
            }
        }
    }
}

@Composable
private fun GameSelectorCard(
    game: PlayTimeGameStats,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        shape = RoundedCornerShape(18.dp),
        color = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.7f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f)
        )
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Surface(
                modifier = Modifier.size(46.dp),
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh
            ) {
                LocalImage(
                    path = game.iconPath,
                    contentDescription = game.title,
                    fallbackLabel = game.title,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(modifier = Modifier.widthIn(min = 150.dp, max = 190.dp)) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = formatDuration(game.totalMs),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun SummaryGrid(
    uiState: PlayTimeUiState,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        SummaryTile(
            label = stringResource(R.string.play_time_total_time),
            value = formatDuration(uiState.totalMs),
            modifier = Modifier.weight(1f)
        )
        SummaryTile(
            label = stringResource(R.string.play_time_sessions),
            value = uiState.sessionCount.toString(),
            modifier = Modifier.weight(1f)
        )
        SummaryTile(
            label = stringResource(R.string.play_time_games_played),
            value = uiState.gamesPlayedCount.toString(),
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun SummaryTile(
    label: String,
    value: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.heightIn(min = 92.dp),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.Schedule,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(20.dp)
            )
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun PlayTimeSection(
    title: String,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onBackground
        )
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 3.dp,
            shadowElevation = 6.dp,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
        ) {
            Column(
                modifier = Modifier.padding(CardContentPadding),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                content()
            }
        }
    }
}

@Composable
private fun DayChart(days: List<PlayTimeDayStats>) {
    val maxValue = days.maxOfOrNull { it.totalMs }?.coerceAtLeast(1L) ?: 1L
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(150.dp),
        horizontalArrangement = Arrangement.spacedBy(7.dp),
        verticalAlignment = Alignment.Bottom
    ) {
        days.forEach { day ->
            val fraction = (day.totalMs.toFloat() / maxValue.toFloat()).coerceIn(0f, 1f)
            Column(
                modifier = Modifier.weight(1f),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    contentAlignment = Alignment.BottomCenter
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(0.68f)
                            .fillMaxHeight(if (day.totalMs > 0L) fraction.coerceAtLeast(0.06f) else 0.02f)
                            .clip(RoundedCornerShape(topStart = 7.dp, topEnd = 7.dp))
                            .background(
                                if (day.totalMs > 0L) MaterialTheme.colorScheme.primary
                                else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)
                            )
                    )
                }
                Text(
                    text = day.label,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
private fun GameTotalsChart(games: List<PlayTimeGameStats>) {
    if (games.isEmpty()) {
        Text(
            text = stringResource(R.string.play_time_no_sessions_body),
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        return
    }
    val maxValue = games.maxOf { it.totalMs }.coerceAtLeast(1L)
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        games.forEach { game ->
            val fraction = (game.totalMs.toFloat() / maxValue.toFloat()).coerceIn(0.04f, 1f)
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    Text(
                        text = formatDuration(game.totalMs),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(10.dp)
                        .clip(RoundedCornerShape(999.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f))
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(fraction)
                            .fillMaxHeight()
                            .clip(RoundedCornerShape(999.dp))
                            .background(MaterialTheme.colorScheme.primary)
                    )
                }
            }
        }
    }
}

@Composable
private fun SessionRow(session: PlayTimeSession) {
    val configuration = LocalConfiguration.current
    val locale = remember(configuration) { configuration.locales.get(0) }
    val dateFormat = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT, locale) }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.42f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.SportsEsports,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(22.dp)
            )
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = session.title,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = dateFormat.format(Date(session.startedAt)),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Text(
                text = formatDuration(session.effectiveDurationMs()),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun EmptyPlayTimeState(
    body: String = stringResource(R.string.play_time_no_sessions_body),
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(
            modifier = Modifier.padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.QueryStats,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(42.dp)
            )
            Text(
                text = stringResource(R.string.play_time_no_sessions_title),
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                textAlign = TextAlign.Center
            )
            Text(
                text = body,
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}

private fun formatDuration(durationMs: Long): String {
    val totalMinutes = (durationMs / 60_000L).coerceAtLeast(0L)
    val hours = totalMinutes / 60L
    val minutes = totalMinutes % 60L
    return when {
        hours > 0L -> "${hours}h ${minutes.toString().padStart(2, '0')}m"
        minutes > 0L -> "${minutes}m"
        durationMs > 0L -> "<1m"
        else -> "0m"
    }
}
