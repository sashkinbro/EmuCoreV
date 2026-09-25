package com.sbro.emucorev.ui.profile

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ViewList
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.CloudUpload
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.ViewModule
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.ProfileCatalogGame
import com.sbro.emucorev.data.ProfileGameStatus
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.ScreenTopBarSurface
import com.sbro.emucorev.ui.common.UrlImage
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonPillShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.useMultiColumnLayout
import java.util.Locale

@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
fun MyListsScreen(
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onGameClick: (Long) -> Unit,
    viewModel: MyListsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val useDenseCards = configuration.useMultiColumnLayout()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    val gridState = rememberLazyGridState()
    val cloudPreferences = remember(context) {
        context.getSharedPreferences("my_lists_preferences", android.content.Context.MODE_PRIVATE)
    }
    var cloudCardHidden by rememberSaveable {
        mutableStateOf(cloudPreferences.getBoolean("cloud_card_hidden", false))
    }
    val setCloudCardHidden: (Boolean) -> Unit = { hidden ->
        cloudCardHidden = hidden
        cloudPreferences.edit().putBoolean("cloud_card_hidden", hidden).apply()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        when {
            uiState.isLoading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }

            uiState.layoutMode == MyListsLayoutMode.LIST -> {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = topInset,
                        bottom = ScreenContentBottomPadding
                    ),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    item {
                        MyListsHeader(
                            totalCount = uiState.totalCount,
                            layoutMode = uiState.layoutMode,
                            cloudCardVisible = !cloudCardHidden,
                            onBackClick = guardedBackClick,
                            onMenuClick = onMenuClick,
                            onToggleCloudCard = { setCloudCardHidden(!cloudCardHidden) },
                            onRefresh = viewModel::refresh,
                            onLayoutMode = viewModel::setLayoutMode
                        )
                    }
                    item {
                        AnimatedVisibility(
                            visible = !cloudCardHidden,
                            enter = fadeIn(animationSpec = tween(180)) +
                                expandVertically(animationSpec = tween(240)),
                            exit = fadeOut(animationSpec = tween(140)) +
                                shrinkVertically(animationSpec = tween(240))
                        ) {
                            MyListsCloudCard(
                                isSignedIn = uiState.isSignedIn,
                                accountEmail = uiState.accountEmail,
                                isBusy = uiState.isCloudBusy,
                                message = uiState.cloudMessage,
                                onBackup = viewModel::backup,
                                onRestore = viewModel::restore,
                                onDismissMessage = viewModel::clearCloudMessage
                            )
                        }
                    }
                    if (uiState.totalCount == 0) {
                        item { MyListsEmptyState() }
                    }
                    if (uiState.favoriteGames.isNotEmpty()) {
                        item {
                            MyListsSectionTitle(
                                title = stringResource(R.string.profile_status_favorites),
                                count = uiState.favoriteGames.size
                            )
                        }
                        items(uiState.favoriteGames, key = { "favorite-${it.catalog.igdbId}" }) { game ->
                            ProfileListGameCard(
                                game = game,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                    uiState.visibleStatuses.forEach { status ->
                        val games = uiState.gamesByStatus[status].orEmpty()
                        item {
                            MyListsSectionTitle(
                                title = stringResource(status.titleResId()),
                                count = games.size
                            )
                        }
                        items(games, key = { "${status.name}-${it.profile.igdbId}" }) { game ->
                            ProfileListGameCard(
                                game = game,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                }
            }

            else -> {
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = if (useDenseCards) 128.dp else 154.dp),
                    state = gridState,
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = topInset,
                        bottom = ScreenContentBottomPadding
                    ),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        MyListsHeader(
                            totalCount = uiState.totalCount,
                            layoutMode = uiState.layoutMode,
                            cloudCardVisible = !cloudCardHidden,
                            onBackClick = guardedBackClick,
                            onMenuClick = onMenuClick,
                            onToggleCloudCard = { setCloudCardHidden(!cloudCardHidden) },
                            onRefresh = viewModel::refresh,
                            onLayoutMode = viewModel::setLayoutMode
                        )
                    }
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        AnimatedVisibility(
                            visible = !cloudCardHidden,
                            enter = fadeIn(animationSpec = tween(180)) +
                                expandVertically(animationSpec = tween(240)),
                            exit = fadeOut(animationSpec = tween(140)) +
                                shrinkVertically(animationSpec = tween(240))
                        ) {
                            MyListsCloudCard(
                                isSignedIn = uiState.isSignedIn,
                                accountEmail = uiState.accountEmail,
                                isBusy = uiState.isCloudBusy,
                                message = uiState.cloudMessage,
                                onBackup = viewModel::backup,
                                onRestore = viewModel::restore,
                                onDismissMessage = viewModel::clearCloudMessage
                            )
                        }
                    }
                    if (uiState.totalCount == 0) {
                        item(span = { GridItemSpan(maxLineSpan) }) { MyListsEmptyState() }
                    }
                    if (uiState.favoriteGames.isNotEmpty()) {
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            MyListsSectionTitle(
                                title = stringResource(R.string.profile_status_favorites),
                                count = uiState.favoriteGames.size
                            )
                        }
                        items(uiState.favoriteGames, key = { "favorite-${it.profile.igdbId}" }) { game ->
                            ProfileGridGameCard(
                                game = game,
                                compact = useDenseCards,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                    uiState.visibleStatuses.forEach { status ->
                        val games = uiState.gamesByStatus[status].orEmpty()
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            MyListsSectionTitle(
                                title = stringResource(status.titleResId()),
                                count = games.size
                            )
                        }
                        items(games, key = { "${status.name}-${it.profile.igdbId}" }) { game ->
                            ProfileGridGameCard(
                                game = game,
                                compact = useDenseCards,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MyListsHeader(
    totalCount: Int,
    layoutMode: MyListsLayoutMode,
    cloudCardVisible: Boolean,
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onToggleCloudCard: () -> Unit,
    onRefresh: () -> Unit,
    onLayoutMode: (MyListsLayoutMode) -> Unit
) {
    ScreenTopBarSurface {
        if (onMenuClick != null) {
            NavigationMenuButton(onClick = onMenuClick)
        } else {
            NavigationBackButton(onClick = onBackClick)
        }
        Spacer(modifier = Modifier.width(12.dp))
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = stringResource(R.string.my_lists_title),
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = stringResource(R.string.profile_game_count, totalCount),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        IconButton(onClick = onToggleCloudCard) {
            Icon(
                imageVector = if (cloudCardVisible) Icons.Rounded.VisibilityOff else Icons.Rounded.CloudUpload,
                contentDescription = stringResource(
                    if (cloudCardVisible) R.string.profile_hide_backup else R.string.profile_show_backup
                )
            )
        }
        IconButton(onClick = onRefresh) {
            Icon(Icons.Rounded.Refresh, contentDescription = stringResource(R.string.profile_refresh))
        }
        IconButton(
            onClick = {
                onLayoutMode(
                    if (layoutMode == MyListsLayoutMode.GRID) MyListsLayoutMode.LIST else MyListsLayoutMode.GRID
                )
            }
        ) {
            Icon(
                imageVector = if (layoutMode == MyListsLayoutMode.GRID) Icons.AutoMirrored.Rounded.ViewList else Icons.Rounded.ViewModule,
                contentDescription = stringResource(R.string.profile_toggle_layout)
            )
        }
    }
}

@Composable
private fun MyListsSectionTitle(title: String, count: Int) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = stringResource(R.string.profile_section_count, count),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun MyListsCloudCard(
    isSignedIn: Boolean,
    accountEmail: String?,
    isBusy: Boolean,
    message: String?,
    onBackup: () -> Unit,
    onRestore: () -> Unit,
    onDismissMessage: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Surface(
                    modifier = Modifier.size(42.dp),
                    shape = neonShape(14.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            imageVector = Icons.Rounded.CloudUpload,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        text = stringResource(R.string.my_lists_cloud_title),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold)
                    )
                    Text(
                        text = stringResource(
                            if (isSignedIn) R.string.my_lists_cloud_synced else R.string.my_lists_cloud_hint
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
                    if (isSignedIn && !accountEmail.isNullOrBlank()) {
                        Text(
                            text = accountEmail,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
            if (isSignedIn) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Button(
                        shape = neonButtonShape(),
                        enabled = !isBusy,
                        onClick = onBackup,
                        modifier = Modifier.weight(1f)
                    ) {
                        if (isBusy) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        } else {
                            Icon(Icons.Rounded.CloudUpload, contentDescription = null, modifier = Modifier.size(18.dp))
                        }
                        Spacer(modifier = Modifier.width(7.dp))
                        Text(stringResource(R.string.profile_backup), maxLines = 1)
                    }
                    OutlinedButton(
                        shape = neonButtonShape(),
                        enabled = !isBusy,
                        onClick = onRestore,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.CloudDownload, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(7.dp))
                        Text(stringResource(R.string.profile_restore), maxLines = 1)
                    }
                }
            }
            message?.let { text ->
                Surface(
                    shape = neonPillShape(),
                    color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f),
                    onClick = onDismissMessage
                ) {
                    Text(
                        text = text,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }
    }
}

@Composable
private fun MyListsEmptyState() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(62.dp),
                shape = neonShape(22.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.10f))
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Person,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(34.dp)
                    )
                }
            }
            Text(
                text = stringResource(R.string.my_lists_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            Text(
                text = stringResource(R.string.my_lists_empty_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 360.dp)
            )
        }
    }
}

@Composable
private fun ProfileGameCardMenuDialog(
    game: ProfileCatalogGame,
    onDismiss: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit
) {
    val profileStatus = game.profile.status
    val isFavorite = game.profile.isFavorite
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            val scrollState = rememberScrollState()
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 620.dp)
                    .heightIn(max = maxHeight),
                shape = neonShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 10.dp,
                shadowElevation = 18.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .verticalScroll(scrollState)
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(52.dp),
                            shape = neonShape(16.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                        ) {
                            UrlImage(
                                imageUrl = game.catalog.coverUrl,
                                contentDescription = game.catalog.name,
                                fallbackLabel = game.catalog.name,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        Spacer(modifier = Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.catalog_profile_menu),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = game.catalog.name,
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.common_close))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    ProfileMenuOption(
                        text = stringResource(R.string.profile_status_favorites),
                        icon = if (isFavorite) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                        selected = isFavorite,
                        onClick = {
                            onFavorite(!isFavorite)
                            onDismiss()
                        }
                    )

                    Text(
                        text = stringResource(R.string.catalog_profile_status_title),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    ProfileGameStatus.entries.forEach { status ->
                        ProfileMenuOption(
                            text = stringResource(status.titleResId()),
                            selected = profileStatus == status,
                            onClick = { onStatus(status) }
                        )
                    }

                    if (profileStatus != null) {
                        OutlinedButton(
                            shape = neonButtonShape(),
                            onClick = onClearStatus,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.catalog_profile_clear_status))
                        }
                    }

                    TextButton(
                        onClick = onRemove,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = stringResource(R.string.catalog_profile_remove),
                            color = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileMenuOption(
    text: String,
    selected: Boolean,
    onClick: () -> Unit,
    icon: ImageVector? = null
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(22.dp)
                )
                Spacer(modifier = Modifier.width(12.dp))
            }
            Text(
                text = text,
                style = MaterialTheme.typography.titleMedium.copy(
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal
                ),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            if (selected) {
                Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            }
        }
    }
}

@Composable
private fun ProfileGridGameCard(
    game: ProfileCatalogGame,
    compact: Boolean,
    onClick: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier
) {
    val openClick = rememberDebouncedClick(onClick = onClick)
    var showProfileMenu by remember { mutableStateOf(false) }
    Surface(
        modifier = modifier,
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = openClick
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f))
            ) {
                UrlImage(
                    imageUrl = game.catalog.coverUrl,
                    contentDescription = game.catalog.name,
                    fallbackLabel = game.catalog.name,
                    modifier = Modifier.fillMaxSize()
                )
                IconButton(
                    onClick = { showProfileMenu = true },
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(4.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.MoreVert,
                        contentDescription = stringResource(R.string.catalog_profile_menu),
                        tint = Color.White
                    )
                }
            }
            ProfileGameText(
                game = game,
                compact = compact,
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 76.dp else 84.dp)
                    .padding(
                        horizontal = if (compact) CompactCardContentPadding else CardContentPadding,
                        vertical = if (compact) 8.dp else 10.dp
                    )
            )
        }
    }
    if (showProfileMenu) {
        ProfileGameCardMenuDialog(
            game = game,
            onDismiss = { showProfileMenu = false },
            onStatus = { status ->
                showProfileMenu = false
                onStatus(status)
            },
            onClearStatus = {
                showProfileMenu = false
                onClearStatus()
            },
            onFavorite = onFavorite,
            onRemove = {
                showProfileMenu = false
                onRemove()
            }
        )
    }
}

@Composable
private fun ProfileListGameCard(
    game: ProfileCatalogGame,
    onClick: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit
) {
    val openClick = rememberDebouncedClick(onClick = onClick)
    var showProfileMenu by remember { mutableStateOf(false) }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = openClick
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(width = 58.dp, height = 86.dp),
                shape = neonShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
            ) {
                UrlImage(
                    imageUrl = game.catalog.coverUrl,
                    contentDescription = game.catalog.name,
                    fallbackLabel = game.catalog.name,
                    modifier = Modifier.fillMaxSize()
                )
            }
            ProfileGameText(
                game = game,
                compact = false,
                modifier = Modifier.weight(1f)
            )
            IconButton(onClick = { showProfileMenu = true }) {
                Icon(Icons.Rounded.MoreVert, contentDescription = stringResource(R.string.catalog_profile_menu))
            }
        }
    }
    if (showProfileMenu) {
        ProfileGameCardMenuDialog(
            game = game,
            onDismiss = { showProfileMenu = false },
            onStatus = { status ->
                showProfileMenu = false
                onStatus(status)
            },
            onClearStatus = {
                showProfileMenu = false
                onClearStatus()
            },
            onFavorite = onFavorite,
            onRemove = {
                showProfileMenu = false
                onRemove()
            }
        )
    }
}

@Composable
private fun ProfileGameText(
    game: ProfileCatalogGame,
    compact: Boolean,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Text(
            text = game.catalog.name,
            style = if (compact) {
                MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium)
            } else {
                MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Medium)
            },
            minLines = 2,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.onSurface
        )
        Text(
            text = buildMeta(game),
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (game.catalog.genres.isNotEmpty()) {
            Text(
                text = game.catalog.genres.take(2).joinToString(" • "),
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

private fun buildMeta(game: ProfileCatalogGame): String {
    val parts = mutableListOf<String>()
    game.catalog.year?.let { parts += it.toString() }
    game.catalog.rating?.let { parts += String.format(Locale.US, "%.1f", it / 10f) }
    return parts.joinToString(" • ")
}

private fun ProfileGameStatus.titleResId(): Int {
    return when (this) {
        ProfileGameStatus.PLAYING -> R.string.profile_status_playing
        ProfileGameStatus.WANT_TO_PLAY -> R.string.profile_status_want_to_play
        ProfileGameStatus.COMPLETED -> R.string.profile_status_completed
        ProfileGameStatus.DROPPED -> R.string.profile_status_dropped
    }
}
