package com.sbro.emucorev.ui.achievements

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.WorkspacePremium
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AchievementAssistResult
import com.sbro.emucorev.data.VitaTrophy
import com.sbro.emucorev.data.VitaTrophyGrade
import com.sbro.emucorev.data.VitaTrophyGroup
import com.sbro.emucorev.data.VitaTrophySet
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset
import java.text.DateFormat
import java.util.Date

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun AchievementsScreen(
    focusTitleId: String? = null,
    onMenuClick: (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    viewModel: AchievementsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset

    LaunchedEffect(focusTitleId) {
        if (!focusTitleId.isNullOrBlank()) {
            viewModel.refresh(focusTitleId)
        }
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
            AchievementsTopBar(
                onMenuClick = onMenuClick,
                onBackClick = onBackClick,
                onRefreshClick = viewModel::refresh,
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
        } else if (uiState.sets.isEmpty()) {
            item {
                EmptyAchievementsState(
                    modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                )
            }
        } else {
            item {
                TrophySetSelector(
                    sets = uiState.sets,
                    selectedCommunicationId = uiState.selectedSet?.communicationId,
                    onSelect = viewModel::selectSet
                )
            }

            uiState.selectedSet?.let { set ->
                item {
                    TrophySetHeader(
                        set = set,
                        modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                    )
                }
                set.groups.forEach { group ->
                    item(key = "${set.communicationId}-${group.id}") {
                        TrophyGroupCard(
                            set = set,
                            group = group,
                            assistStates = uiState.trophyAssist,
                            canTranslate = !uiState.targetLanguageTag.startsWith("en", ignoreCase = true),
                            onTranslate = viewModel::requestTranslation,
                            onHint = viewModel::requestHint,
                            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AchievementsTopBar(
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
                text = stringResource(R.string.achievements_title),
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
private fun TrophySetSelector(
    sets: List<VitaTrophySet>,
    selectedCommunicationId: String?,
    onSelect: (String) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = stringResource(R.string.achievements_choose_game),
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
        )
        LazyRow(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding)
        ) {
            items(sets, key = { it.communicationId }) { set ->
                val selected = set.communicationId.equals(selectedCommunicationId, ignoreCase = true)
                FilterChip(
                    selected = selected,
                    onClick = { onSelect(set.communicationId) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                        selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                    ),
                    label = {
                        Text(
                            text = set.gameTitle,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.widthIn(max = 190.dp)
                        )
                    },
                    leadingIcon = {
                        Icon(Icons.Rounded.WorkspacePremium, contentDescription = null)
                    }
                )
            }
        }
    }
}

@Composable
private fun TrophySetHeader(
    set: VitaTrophySet,
    modifier: Modifier = Modifier
) {
    val progress = if (set.trophyCount > 0) set.unlockedCount / set.trophyCount.toFloat() else 0f
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Surface(
                modifier = Modifier.size(64.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh
            ) {
                LocalImage(
                    path = set.gameIconPath,
                    contentDescription = set.gameTitle,
                    fallbackLabel = set.gameTitle,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                Text(
                    text = set.setName,
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (set.setDetail.isNotBlank()) {
                    Text(
                        text = set.setDetail,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                LinearProgressIndicator(
                    progress = { progress.coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth(),
                    color = MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.surfaceVariant
                )
                Text(
                    text = stringResource(R.string.achievements_progress_count, set.unlockedCount, set.trophyCount),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun TrophyGroupCard(
    set: VitaTrophySet,
    group: VitaTrophyGroup,
    assistStates: Map<String, TrophyAssistUiState>,
    canTranslate: Boolean,
    onTranslate: (VitaTrophySet, VitaTrophy) -> Unit,
    onHint: (VitaTrophySet, VitaTrophy) -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = group.name,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    if (group.detail.isNotBlank()) {
                        Text(
                            text = group.detail,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                Text(
                    text = stringResource(
                        R.string.achievements_progress_count,
                        group.trophies.count { it.unlocked },
                        group.trophies.size
                    ),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary
                )
            }
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                group.trophies.forEach { trophy ->
                    val assistKey = "${set.communicationId}:${trophy.id}"
                    TrophyRow(
                        set = set,
                        trophy = trophy,
                        assistState = assistStates[assistKey] ?: TrophyAssistUiState(),
                        canTranslate = canTranslate,
                        onTranslate = onTranslate,
                        onHint = onHint
                    )
                }
            }
        }
    }
}

@Composable
private fun TrophyRow(
    set: VitaTrophySet,
    trophy: VitaTrophy,
    assistState: TrophyAssistUiState,
    canTranslate: Boolean,
    onTranslate: (VitaTrophySet, VitaTrophy) -> Unit,
    onHint: (VitaTrophySet, VitaTrophy) -> Unit
) {
    val hiddenLocked = trophy.hidden && !trophy.unlocked
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = 0.52f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.34f))
    ) {
        Column(
            modifier = Modifier.padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Surface(
                    modifier = Modifier
                        .size(56.dp)
                        .clip(RoundedCornerShape(14.dp)),
                    shape = RoundedCornerShape(14.dp),
                    color = if (trophy.unlocked) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.52f)
                ) {
                    if (hiddenLocked) {
                        Icon(
                            imageVector = Icons.Rounded.Lock,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(15.dp)
                        )
                    } else {
                        LocalImage(
                            path = trophy.iconPath,
                            contentDescription = trophy.name.ifBlank { stringResource(R.string.achievements_trophy) },
                            fallbackLabel = trophy.name.ifBlank { "?" },
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(3.dp)
                ) {
                    Text(
                        text = if (hiddenLocked) stringResource(R.string.achievements_hidden_trophy) else trophy.name.ifBlank { stringResource(R.string.achievements_trophy) },
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = if (trophy.unlocked) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = if (hiddenLocked) {
                            stringResource(R.string.achievements_hidden_trophy_body)
                        } else {
                            trophy.detail
                        },
                        style = MaterialTheme.typography.bodySmall,
                        maxLines = 4,
                        overflow = TextOverflow.Ellipsis,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    trophy.unlockedAtEpochSeconds?.let { epochSeconds ->
                        Text(
                            text = stringResource(
                                R.string.achievements_unlocked_at,
                                DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(epochSeconds * 1000L))
                            ),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                TrophyGradePill(grade = trophy.grade, unlocked = trophy.unlocked)
            }
            if (!hiddenLocked) {
                TrophyAssistActions(
                    canTranslate = canTranslate,
                    translationLoading = assistState.translationLoading,
                    hintLoading = assistState.hintLoading,
                    onTranslate = { onTranslate(set, trophy) },
                    onHint = { onHint(set, trophy) },
                    modifier = Modifier.fillMaxWidth()
                )
                TrophyAssistContent(assistState = assistState)
            }
        }
    }
}

@Composable
private fun TrophyAssistActions(
    canTranslate: Boolean,
    translationLoading: Boolean,
    hintLoading: Boolean,
    onTranslate: () -> Unit,
    onHint: () -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End)
    ) {
        AssistActionChip(
            text = stringResource(R.string.achievements_how_to_unlock),
            icon = Icons.Rounded.Info,
            loading = hintLoading,
            onClick = onHint
        )
        if (canTranslate) {
            TranslateIconButton(
                loading = translationLoading,
                onClick = onTranslate
            )
        }
    }
}

@Composable
private fun TranslateIconButton(
    loading: Boolean,
    onClick: () -> Unit
) {
    val transition = rememberInfiniteTransition(label = "achievement-translate-loading")
    val rotation by transition.animateFloat(
        initialValue = 0f,
        targetValue = if (loading) 360f else 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 950),
            repeatMode = RepeatMode.Restart
        ),
        label = "achievement-translate-rotation"
    )
    val pulse by transition.animateFloat(
        initialValue = 0.92f,
        targetValue = if (loading) 1.08f else 0.92f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 620),
            repeatMode = RepeatMode.Reverse
        ),
        label = "achievement-translate-pulse"
    )
    Surface(
        modifier = Modifier
            .size(30.dp)
            .scale(if (loading) pulse else 1f)
            .clip(CircleShape)
            .clickable(enabled = !loading, onClick = onClick),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.primary,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.35f))
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Rounded.Language,
                contentDescription = stringResource(R.string.achievements_translate),
                tint = MaterialTheme.colorScheme.onPrimary,
                modifier = Modifier
                    .size(18.dp)
                    .rotate(if (loading) rotation else 0f)
            )
        }
    }
}

@Composable
private fun AssistActionChip(
    text: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    loading: Boolean,
    onClick: () -> Unit
) {
    Surface(
        shape = RoundedCornerShape(50),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .clickable(enabled = !loading, onClick = onClick)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(16.dp)
            )
            Text(
                text = text,
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun TrophyAssistContent(assistState: TrophyAssistUiState) {
    if (assistState.hintLoading || assistState.translationLoading) {
        TrophyAssistSkeleton(modifier = Modifier.padding(top = 8.dp))
    }
    assistState.hint?.let { result ->
        TrophyAssistResultBlock(
            label = result.title.ifBlank { stringResource(R.string.achievements_hint_title) },
            result = result,
            modifier = Modifier.padding(top = 8.dp)
        )
    }
    assistState.translation?.let { result ->
        TrophyAssistResultBlock(
            label = stringResource(R.string.achievements_translation_title),
            result = result,
            modifier = Modifier.padding(top = 8.dp)
        )
    }
    if (assistState.hasError && !assistState.hintLoading && !assistState.translationLoading) {
        Text(
            text = stringResource(R.string.achievements_assist_error),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.error,
            modifier = Modifier.padding(top = 8.dp)
        )
    }
}

@Composable
private fun TrophyAssistResultBlock(
    label: String,
    result: AchievementAssistResult,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(3.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.primary
        )
        if (result.title.isNotBlank() && result.title != label) {
            Text(
                text = result.title,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
        }
        Text(
            text = result.body,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun TrophyAssistSkeleton(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(7.dp)
    ) {
        ShimmerLine(width = 118.dp)
        ShimmerLine(widthFraction = 0.94f)
        ShimmerLine(widthFraction = 0.78f)
        ShimmerLine(widthFraction = 0.56f)
    }
}

@Composable
private fun ShimmerLine(
    modifier: Modifier = Modifier,
    width: androidx.compose.ui.unit.Dp? = null,
    widthFraction: Float = 1f
) {
    val transition = rememberInfiniteTransition(label = "achievement-assist-shimmer")
    val shift by transition.animateFloat(
        initialValue = 0f,
        targetValue = 900f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 1100),
            repeatMode = RepeatMode.Restart
        ),
        label = "achievement-assist-shimmer-shift"
    )
    val base = MaterialTheme.colorScheme.surfaceContainerHigh.copy(alpha = 0.58f)
    val highlight = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.10f)
    val lineModifier = if (width != null) {
        modifier.width(width)
    } else {
        modifier.fillMaxWidth(widthFraction.coerceIn(0.1f, 1f))
    }
    Box(
        modifier = lineModifier
            .height(11.dp)
            .clip(RoundedCornerShape(50))
            .background(
                Brush.linearGradient(
                    colors = listOf(base, highlight, base),
                    start = Offset(shift - 900f, 0f),
                    end = Offset(shift, 0f)
                )
            )
    )
}

@Composable
private fun TrophyGradePill(
    grade: VitaTrophyGrade,
    unlocked: Boolean
) {
    val color = if (!unlocked) {
        MaterialTheme.colorScheme.outline
    } else {
        when (grade) {
            VitaTrophyGrade.Platinum -> Color(0xFF7AD8FF)
            VitaTrophyGrade.Gold -> Color(0xFFE1AA28)
            VitaTrophyGrade.Silver -> Color(0xFFB9C1CB)
            VitaTrophyGrade.Bronze -> Color(0xFFB8794A)
            VitaTrophyGrade.Unknown -> MaterialTheme.colorScheme.primary
        }
    }
    Surface(
        shape = CircleShape,
        color = color.copy(alpha = if (unlocked) 0.18f else 0.10f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.55f))
    ) {
        Text(
            text = when (grade) {
                VitaTrophyGrade.Platinum -> "P"
                VitaTrophyGrade.Gold -> "G"
                VitaTrophyGrade.Silver -> "S"
                VitaTrophyGrade.Bronze -> "B"
                VitaTrophyGrade.Unknown -> "-"
            },
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = color,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun EmptyAchievementsState(modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 220.dp),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.padding(22.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Icon(
                imageVector = Icons.Rounded.EmojiEvents,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(42.dp)
            )
            Text(
                text = stringResource(R.string.achievements_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 14.dp)
            )
            Text(
                text = stringResource(R.string.achievements_empty_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp)
            )
        }
    }
}
