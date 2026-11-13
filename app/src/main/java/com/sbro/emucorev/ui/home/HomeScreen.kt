package com.sbro.emucorev.ui.home

import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import androidx.compose.foundation.background
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.combinedClickable
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import com.sbro.emucorev.ui.theme.neon.neonPillShape
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesomeMotion
import androidx.compose.material.icons.rounded.Games
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.QueryStats
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.GameCoverArt
import com.sbro.emucorev.ui.common.GameCoverAspectRatio
import com.sbro.emucorev.ui.common.CustomizationBackground
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.ScreenTopBarSurface
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.GradientEnd
import com.sbro.emucorev.ui.theme.GradientStart
import com.sbro.emucorev.ui.theme.LocalCustomizationSettings
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HomeScreen(
    onOpenSetup: () -> Unit,
    onOpenLibrary: () -> Unit,
    onOpenCatalog: () -> Unit,
    onLaunchGame: (String) -> Unit,
    onOpenGameManager: (String) -> Unit,
    onOpenPlayTime: (String) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: HomeViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val customization = LocalCustomizationSettings.current
    val isLandscape = LocalWindowInfo.current.containerSize.let { it.width > it.height }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val openLibraryClick = rememberDebouncedClick {
        viewModel.refresh()
        onOpenLibrary()
    }
    val openSetupClick = rememberDebouncedClick(onClick = onOpenSetup)
    val openCatalogClick = rememberDebouncedClick {
        viewModel.refresh()
        onOpenCatalog()
    }
    val refreshClick = rememberDebouncedClick(onClick = viewModel::refresh)

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        CustomizationBackground(
            path = customization.backgroundPath,
            mimeType = customization.backgroundMimeType,
            modifier = Modifier.matchParentSize()
        )
        if (customization.backgroundPath != null) {
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .background(MaterialTheme.colorScheme.background.copy(alpha = 0.72f))
            )
        }
        if (uiState.isLoading) {
            Box(modifier = Modifier.matchParentSize(), contentAlignment = Alignment.Center) {
                PremiumLoadingAnimation(size = 80.dp)
            }
            return@Box
        }

        LazyColumn(
            modifier = Modifier.matchParentSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset,
                bottom = ScreenContentBottomPadding
            ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                ScreenTopBarSurface {
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (onMenuClick != null) {
                            NavigationMenuButton(
                                onClick = onMenuClick,
                                modifier = Modifier.padding(end = 14.dp)
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.app_name_emucorev),
                                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onBackground
                            )
                        }
                    }
                    IconButton(onClick = refreshClick) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = stringResource(R.string.library_refresh),
                            tint = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            }

            item {
                Surface(
                    shape = neonShape(32.dp),
                    color = MaterialTheme.colorScheme.surface
                ) {
                    Column(
                        modifier = Modifier
                            .background(
                                Brush.linearGradient(
                                    colors = listOf(
                                        GradientStart.copy(alpha = 0.14f),
                                        GradientEnd.copy(alpha = 0.08f)
                                    )
                                )
                            )
                            .padding(CardContentPadding),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.home_title),
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold)
                        )
                        Text(
                            text = stringResource(R.string.home_subtitle),
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            HomeStatPill(
                                icon = Icons.Rounded.Games,
                                label = stringResource(R.string.home_installed_count, uiState.installedCount)
                            )
                            HomeStatPill(
                                icon = Icons.Rounded.AutoAwesomeMotion,
                                label = stringResource(R.string.home_catalog_count, uiState.catalogCount)
                            )
                            HomeStatPill(
                                icon = Icons.Rounded.Storage,
                                label = stringResource(R.string.home_storage_short)
                            )
                        }
                        Text(
                            text = stringResource(R.string.home_storage_path, uiState.storagePath),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            FilledTonalButton(shape = neonButtonShape(), onClick = openSetupClick) {
                                Icon(Icons.Rounded.Memory, contentDescription = null)
                                Text(stringResource(R.string.home_open_setup))
                            }
                            FilledTonalButton(shape = neonButtonShape(), onClick = openLibraryClick) {
                                Icon(Icons.Rounded.SportsEsports, contentDescription = null)
                                Text(stringResource(R.string.home_open_library))
                            }
                            FilledTonalButton(shape = neonButtonShape(), onClick = openCatalogClick) {
                                Icon(Icons.Rounded.AutoAwesomeMotion, contentDescription = null)
                                Text(stringResource(R.string.home_open_catalog))
                            }
                        }
                    }
                }
            }

            if (uiState.featuredGames.isNotEmpty()) {
                item {
                    SectionCard(title = stringResource(R.string.home_recent_titles_title)) {
                        LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            items(uiState.featuredGames, key = { it.titleId }) { game ->
                                FeaturedGameCard(
                                    game = game,
                                    onClick = { onLaunchGame(game.titleId) },
                                    onOpenGameManager = { onOpenGameManager(game.titleId) },
                                    onOpenPlayTime = { onOpenPlayTime(game.titleId) },
                                    compact = isLandscape,
                                    coverScale = customization.coverSizePercent.coerceIn(70, 150) / 100f
                                )
                            }
                        }
                    }
                }
            } else {
                item {
                    SectionCard(title = stringResource(R.string.home_empty_library_title)) {
                        Text(
                            text = stringResource(R.string.home_empty_library_body),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }


        }
    }
}

@Composable
private fun HomeStatPill(
    icon: ImageVector,
    label: String
) {
    Surface(
        shape = neonPillShape(),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun FeaturedGameCard(
    game: InstalledVitaGame,
    onClick: () -> Unit,
    onOpenGameManager: () -> Unit,
    onOpenPlayTime: () -> Unit,
    compact: Boolean,
    coverScale: Float
) {
    val guardedClick = rememberDebouncedClick(onClick = onClick)
    var menuExpanded by remember(game.titleId) { mutableStateOf(false) }
    var is3dCover by remember(game.titleId, game.iconPath) { mutableStateOf(false) }
    val interactionSource = remember(game.titleId) { MutableInteractionSource() }
    val pressed by interactionSource.collectIsPressedAsState()
    val pressScale by animateFloatAsState(if (pressed) 0.95f else 1f, tween(100), label = "home-recent-press")
    val shape = RectangleShape
    Box {
        Surface(
            modifier = Modifier
                .width((if (compact) 98.dp else 108.dp) * coverScale)
                .graphicsLayer { scaleX = pressScale; scaleY = pressScale }
                .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false, showIdleBorder = !is3dCover)
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = guardedClick,
                    onLongClick = { menuExpanded = true }
                ),
            shape = shape,
            color = if (is3dCover) Color.Transparent else MaterialTheme.colorScheme.surface,
            tonalElevation = 0.dp,
            shadowElevation = 0.dp
        ) {
            Column {
                GameCoverArt(
                    titleId = game.titleId,
                    fallbackIconPath = game.iconPath,
                    contentDescription = game.title,
                    fallbackTitle = game.title,
                    onPresentationChanged = { is3dCover = it },
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(GameCoverAspectRatio)
                )
                Column(
                    modifier = Modifier.padding(CardContentPadding),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = game.titleId,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
        DropdownMenu(
            expanded = menuExpanded,
            onDismissRequest = { menuExpanded = false },
            modifier = Modifier.widthIn(min = 248.dp, max = 310.dp),
            shape = neonShape(20.dp),
            containerColor = MaterialTheme.colorScheme.surface,
            tonalElevation = 0.dp,
            shadowElevation = 12.dp,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
        ) {
            HomeGameContextMenuItem(
                text = stringResource(R.string.game_manager_open_for_game),
                icon = Icons.Rounded.Tune,
                onClick = {
                    onOpenGameManager()
                    menuExpanded = false
                }
            )
            HomeGameContextMenuItem(
                text = stringResource(R.string.play_time_open_for_game),
                icon = Icons.Rounded.QueryStats,
                onClick = {
                    onOpenPlayTime()
                    menuExpanded = false
                }
            )
        }
    }
}

@Composable
private fun HomeGameContextMenuItem(
    text: String,
    icon: ImageVector,
    onClick: () -> Unit
) {
    DropdownMenuItem(
        text = {
            Text(
                text = text,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        },
        onClick = onClick,
        leadingIcon = {
            Box(
                modifier = Modifier
                    .size(34.dp)
                    .clip(neonShape(11.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.68f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(19.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        contentPadding = PaddingValues(horizontal = 12.dp)
    )
}
