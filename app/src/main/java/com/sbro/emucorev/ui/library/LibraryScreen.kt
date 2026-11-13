package com.sbro.emucorev.ui.library

import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Constraints
import android.widget.Toast
import com.sbro.emucorev.data.CoverArtRepository
import com.sbro.emucorev.ui.common.prefetchGameCoverArt
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.EaseIn
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.tween
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.interaction.MutableInteractionSource
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
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
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.items
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.SortByAlpha
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.ViewAgenda
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.QueryStats
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.ViewModule
import androidx.compose.material.icons.rounded.ViewCarousel
import com.sbro.emucorev.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.LibraryLayoutMode
import com.sbro.emucorev.ui.common.CustomizationBackground
import com.sbro.emucorev.ui.common.GameCoverArt
import com.sbro.emucorev.ui.common.GameCoverAspectRatio
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.ScreenTopBarSurface
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.LocalCustomizationSettings
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LibraryScreen(
    onLaunchGame: (String) -> Unit,
    onOpenSaveManager: (String) -> Unit,
    onOpenSaveStates: (String) -> Unit,
    onOpenGameManager: (String) -> Unit,
    onOpenPlayTime: (String) -> Unit,
    onOpenAchievements: (String) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    shelfMode: Boolean = false,
    onShelfModeChanged: (Boolean) -> Unit = {},
    viewModel: LibraryViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val windowInfo = LocalWindowInfo.current
    val density = LocalDensity.current
    val customization = LocalCustomizationSettings.current
    val neon = LocalNeonTheme.current
    val refreshClick = rememberDebouncedClick(onClick = viewModel::refresh)
    val preferences = remember(context) { AppPreferences(context) }
    val layoutMode by remember(preferences) { preferences.libraryLayoutModeFlow }
        .collectAsState(initial = preferences.libraryLayoutMode)
    var searchExpanded by rememberSaveable { mutableStateOf(false) }
    var deleteGameId by remember { mutableStateOf<String?>(null) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val gameCountSubtitle = pluralStringResource(
        R.plurals.library_game_count,
        uiState.items.size,
        uiState.items.size
    )
    val deleteGameLabel = stringResource(R.string.detail_delete_game)
    val manageSaveStatesLabel = stringResource(R.string.savestate_manager_open_for_game)
    val manageGameSettingsLabel = stringResource(R.string.game_manager_open_for_game)
    val playTimeLabel = stringResource(R.string.play_time_open_for_game)
    val achievementsLabel = stringResource(R.string.achievements_open_for_game)
    val addShortcutLabel = stringResource(R.string.library_add_shortcut)
    val shortcutRequestedMessage = stringResource(R.string.library_shortcut_requested)
    val shortcutUnsupportedMessage = stringResource(R.string.library_shortcut_unsupported)
    val shortcutFailedMessage = stringResource(R.string.library_shortcut_failed)
    val deleteGameConfirmTitle = stringResource(R.string.detail_delete_game_confirm_title)
    val deleteGameConfirmBody = stringResource(R.string.detail_delete_game_confirm_body)
    val deleteGameFailedMessage = stringResource(R.string.detail_delete_game_failed)
    var measuredContentWidthPx by remember { mutableIntStateOf(0) }
    val containerWidthDp = with(density) { (measuredContentWidthPx.takeIf { it > 0 } ?: windowInfo.containerSize.width).toDp() }
    val isLandscape = windowInfo.containerSize.width > windowInfo.containerSize.height
    val gridColumns = remember(containerWidthDp, customization.coverSizePercent, isLandscape) {
        LibraryGridSizing.columnsForWidth(
            containerWidthDp.value,
            customization.coverSizePercent,
            isLandscape = isLandscape
        )
    }
    BackHandler(enabled = shelfMode) { onShelfModeChanged(false) }
    val libraryState = rememberLazyListState()
    val coverPrefetchWindow = remember(uiState.items, layoutMode, gridColumns) {
        val rowSize = if (layoutMode == LibraryLayoutMode.LIST) 1 else gridColumns.coerceAtLeast(1)
        LibraryCoverPrefetchWindow(uiState.items.chunked(rowSize).map { row ->
            LibraryCoverPrefetchRow(row.first().titleId, row.map { it.titleId })
        })
    }
    val coverArt = remember(context) { CoverArtRepository.get(context) }
    val coverStyle by coverArt.style.collectAsState()
    val prefetchCover: suspend (String) -> Unit = remember(context, uiState.items, uiState.artworkVersion, coverStyle) {
        val gamesById = uiState.items.associateBy { it.titleId }
        val load: suspend (String) -> Unit = { titleId ->
            gamesById[titleId]?.let { game ->
                prefetchGameCoverArt(context, titleId = game.titleId, fallbackIconPath = game.iconPath,
                    styleOverride = coverStyle, artworkVersion = uiState.artworkVersion)
            }
        }
        load
    }
    LibraryCoverPrefetch(libraryState, coverPrefetchWindow,
        enabled = !uiState.isLoading && !shelfMode, artworkVersion = uiState.artworkVersion,
        load = prefetchCover)

    val gameContextMenu: @Composable (com.sbro.emucorev.data.InstalledVitaGame, Boolean, () -> Unit) -> Unit = { game, expanded, dismiss ->
        LibraryGameContextMenu(
        expanded = expanded,
        onDismiss = dismiss,
        addShortcutLabel = addShortcutLabel,
        onAddShortcut = {
            val message = when (GameShortcutInstaller.requestPinnedShortcut(context, game.titleId, game.title, game.iconPath)) {
                GameShortcutInstaller.Result.Requested -> shortcutRequestedMessage
                GameShortcutInstaller.Result.Unsupported -> shortcutUnsupportedMessage
                GameShortcutInstaller.Result.Failed -> shortcutFailedMessage
            }
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
            dismiss()
        },
        manageGameSettingsLabel = manageGameSettingsLabel,
        onManageGameSettings = {
            onOpenGameManager(game.titleId)
            dismiss()
        },
        playTimeLabel = playTimeLabel,
        onOpenPlayTime = {
            onOpenPlayTime(game.titleId)
            dismiss()
        },
        achievementsLabel = achievementsLabel,
        onOpenAchievements = {
            onOpenAchievements(game.titleId)
            dismiss()
        },
        manageSaveStatesLabel = manageSaveStatesLabel,
        onOpenSaveStates = {
            onOpenSaveStates(game.titleId)
            dismiss()
        },
        deleteGameLabel = deleteGameLabel,
        onDeleteGame = {
            deleteGameId = game.titleId
            dismiss()
        }
    )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .onSizeChanged { measuredContentWidthPx = it.width }
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
        AnimatedContent(
            targetState = shelfMode,
            transitionSpec = {
                if (targetState) {
                    (slideInHorizontally(initialOffsetX = { it / 5 }, animationSpec = tween(440, easing = EaseOut)) +
                        fadeIn(tween(360, easing = EaseOut)) + scaleIn(initialScale = 0.97f, animationSpec = tween(440, easing = EaseOut))) togetherWith
                        (slideOutHorizontally(targetOffsetX = { -it / 8 }, animationSpec = tween(300, easing = EaseIn)) +
                            fadeOut(tween(260, easing = EaseIn)) + scaleOut(targetScale = 0.985f, animationSpec = tween(300, easing = EaseIn)))
                } else {
                    (slideInHorizontally(initialOffsetX = { -it / 5 }, animationSpec = tween(420, easing = EaseOut)) +
                        fadeIn(tween(350, easing = EaseOut)) + scaleIn(initialScale = 0.985f, animationSpec = tween(420, easing = EaseOut))) togetherWith
                        (slideOutHorizontally(targetOffsetX = { it / 5 }, animationSpec = tween(340, easing = EaseIn)) +
                            fadeOut(tween(290, easing = EaseIn)) + scaleOut(targetScale = 0.97f, animationSpec = tween(340, easing = EaseIn)))
                }
            },
            label = "library-mode",
            modifier = Modifier.fillMaxSize()
        ) { showShelf ->
            if (showShelf && uiState.items.isNotEmpty()) {
                LibraryShelfMode(
                    games = uiState.items,
                    artworkVersion = uiState.artworkVersion,
                    initialTitleId = uiState.recentTitleId,
                    topInset = 0.dp,
                    bottomInset = 0.dp,
                    horizontalInset = ScreenHorizontalPadding,
                    onExitShelfMode = { onShelfModeChanged(false) },
                    onGameClick = { onLaunchGame(it.titleId) },
                    contextMenu = gameContextMenu
                )
            } else {
        LazyColumn(
            state = libraryState,
            modifier = Modifier
                .fillMaxSize()
                .navigationBarsPadding(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            Column {
                ScreenTopBarSurface {
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (onMenuClick != null) {
                            NavigationMenuButton(
                                onClick = onMenuClick,
                                modifier = Modifier.padding(end = if (neon) 12.dp else 14.dp)
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.nav_library),
                                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                text = gameCountSubtitle,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                    LibraryHeaderActions(
                        isRefreshing = uiState.isLoading || uiState.isSorting,
                        searchExpanded = searchExpanded,
                        sortOption = uiState.sortOption,
                        layoutMode = layoutMode,
                        onRefresh = refreshClick,
                        onToggleSearch = {
                            searchExpanded = !searchExpanded
                            if (!searchExpanded) viewModel.updateQuery("")
                        },
                        onSortSelected = viewModel::updateSortOption,
                        onShowShelf = { onShelfModeChanged(true) },
                        onToggleLayout = {
                            preferences.libraryLayoutMode = if (layoutMode == LibraryLayoutMode.LIST) LibraryLayoutMode.GRID else LibraryLayoutMode.LIST
                        }
                    )
                }

                AnimatedVisibility(
                    visible = searchExpanded,
                    enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
                    exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
                ) {
                    Column {
                        Spacer(modifier = Modifier.height(14.dp))
                        OutlinedTextField(
                            value = uiState.query,
                            onValueChange = viewModel::updateQuery,
                            modifier = Modifier.fillMaxWidth(),
                            placeholder = { Text(stringResource(R.string.library_search_hint)) },
                            leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                            trailingIcon = if (uiState.query.isNotEmpty()) {
                                {
                                    IconButton(onClick = { viewModel.updateQuery("") }) {
                                        Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.catalog_search_clear))
                                    }
                                }
                            } else null,
                            singleLine = true,
                            shape = neonShape(20.dp),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
                                unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.55f),
                                focusedContainerColor = MaterialTheme.colorScheme.surface,
                                unfocusedContainerColor = MaterialTheme.colorScheme.surface
                            )
                        )
                    }
                }
            }
        }

        if (uiState.isLoading) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 48.dp),
                    contentAlignment = Alignment.Center
                ) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }
        } else if (uiState.items.isEmpty()) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 360.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        modifier = Modifier.widthIn(max = 520.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(18.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.library_empty_title),
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onBackground,
                            textAlign = TextAlign.Center
                        )
                        Text(
                            text = stringResource(R.string.library_empty_body),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f),
                            textAlign = TextAlign.Center
                        )
                        Button(shape = neonButtonShape(), onClick = refreshClick) {
                            Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(stringResource(R.string.library_refresh))
                        }
                    }
                }
            }
        } else if (layoutMode == LibraryLayoutMode.LIST) {
            items(uiState.items, key = { it.titleId }) { game ->
                val selectGameClick = rememberDebouncedClick { onLaunchGame(game.titleId) }
                val shape = neonShape(16.dp)
                var menuExpanded by remember(game.titleId) { mutableStateOf(false) }
                val cardInteraction = remember(game.titleId) { MutableInteractionSource() }
                val pressed by cardInteraction.collectIsPressedAsState()
                val pressScale by animateFloatAsState(if (pressed) 0.985f else 1f, tween(100), label = "library-list-press")
                Box {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .graphicsLayer { scaleX = pressScale; scaleY = pressScale }
                            .clip(shape)
                            .gamepadFocusableCard(shape = shape, interactionSource = cardInteraction, addFocusTarget = false, showIdleBorder = false)
                            .combinedClickable(
                                interactionSource = cardInteraction,
                                indication = null,
                                onClick = selectGameClick,
                                onLongClick = { menuExpanded = true }
                            ),
                        shape = shape,
                        color = MaterialTheme.colorScheme.surface,
                        tonalElevation = 0.dp,
                        shadowElevation = 0.dp
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 9.dp),
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            verticalAlignment = Alignment.Top
                        ) {
                            LibraryGameArtwork(
                                titleId = game.titleId,
                                path = game.iconPath,
                                artworkVersion = uiState.artworkVersion,
                                title = game.title,
                                modifier = Modifier.width(52.dp),
                                artworkAspectRatio = 2f / 3f
                            )
                            Column(
                                modifier = Modifier.weight(1f).heightIn(min = 78.dp),
                                verticalArrangement = Arrangement.spacedBy(3.dp)
                            ) {
                                Text(
                                    text = game.title,
                                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = game.titleId,
                                    color = MaterialTheme.colorScheme.primary
                                )
                                Text(
                                    text = game.version ?: stringResource(R.string.common_not_available),
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                    gameContextMenu(game, menuExpanded) { menuExpanded = false }
                }
            }
        } else {
            items(
                items = uiState.items.chunked(gridColumns),
                key = { row -> row.firstOrNull()?.titleId ?: row.hashCode().toString() }
            ) { rowItems ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    rowItems.forEach { game ->
                        val selectGameClick = rememberDebouncedClick { onLaunchGame(game.titleId) }
                        val shape = RectangleShape
                        var menuExpanded by remember(game.titleId) { mutableStateOf(false) }
                        var is3dCover by remember(game.titleId, game.iconPath, uiState.artworkVersion, coverStyle) {
                            mutableStateOf(false)
                        }
                        val cardInteraction = remember(game.titleId) { MutableInteractionSource() }
                        val pressed by cardInteraction.collectIsPressedAsState()
                        val pressScale by animateFloatAsState(if (pressed) 0.96f else 1f, tween(100), label = "library-grid-press")
                        Box(modifier = Modifier.weight(1f)) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .graphicsLayer { scaleX = pressScale; scaleY = pressScale }
                                    .gamepadFocusableCard(shape = shape, interactionSource = cardInteraction, addFocusTarget = false, showIdleBorder = !is3dCover)
                                    .combinedClickable(
                                        interactionSource = cardInteraction,
                                        indication = null,
                                        onClick = selectGameClick,
                                        onLongClick = { menuExpanded = true }
                                    ),
                                shape = shape,
                                color = if (is3dCover) Color.Transparent else MaterialTheme.colorScheme.surface,
                                tonalElevation = 0.dp,
                                shadowElevation = 0.dp,
                                border = if (!is3dCover && MaterialTheme.colorScheme.background.luminance() > 0.5f) BorderStroke(1.5.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.34f)) else null
                            ) {
                                GameCoverArt(
                                    titleId = game.titleId,
                                    fallbackIconPath = game.iconPath,
                                    artworkVersion = uiState.artworkVersion,
                                    contentDescription = game.title,
                                    fallbackTitle = game.title,
                                    onPresentationChanged = { is3dCover = it },
                                    modifier = Modifier.fillMaxWidth().aspectRatio(GameCoverAspectRatio)
                                )
                            }
                            gameContextMenu(game, menuExpanded) { menuExpanded = false }
                        }
                    }
                    repeat(gridColumns - rowItems.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
        }
    }
        }
    }

    if (deleteGameId != null) {
        AlertDialog(
            onDismissRequest = { deleteGameId = null },
            title = { Text(deleteGameConfirmTitle) },
            text = { Text(deleteGameConfirmBody) },
            confirmButton = {
                TextButton(
                    onClick = {
                        val targetGameId = deleteGameId
                        if (targetGameId != null) {
                            viewModel.deleteInstalledGame(targetGameId) { deleted ->
                                if (!deleted) {
                                    Toast.makeText(context, deleteGameFailedMessage, Toast.LENGTH_SHORT).show()
                                }
                            }
                        }
                        deleteGameId = null
                    }
                ) {
                    Text(deleteGameLabel)
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteGameId = null }) {
                    Text(stringResource(R.string.install_dialog_close))
                }
            }
        )
    }
}

@Composable
private fun LibraryHeaderActions(
    isRefreshing: Boolean,
    searchExpanded: Boolean,
    sortOption: LibrarySortOption,
    layoutMode: LibraryLayoutMode,
    onRefresh: () -> Unit,
    onToggleSearch: () -> Unit,
    onSortSelected: (LibrarySortOption) -> Unit,
    onShowShelf: () -> Unit,
    onToggleLayout: () -> Unit
) {
    var showLibraryOptions by remember { mutableStateOf(false) }
    val optionsDescription = stringResource(R.string.library_options)
    val menuMaxHeight = with(LocalDensity.current) {
        (LocalWindowInfo.current.containerSize.height.toDp() - 24.dp).coerceAtLeast(240.dp)
    }
    Surface(
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.50f))
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box {
                IconButton(
                    modifier = Modifier.size(42.dp).semantics { contentDescription = optionsDescription },
                    onClick = { showLibraryOptions = true }
                ) {
                    if (isRefreshing) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(19.dp),
                            strokeWidth = 2.dp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        Icon(
                            imageVector = Icons.Rounded.MoreVert,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                DropdownMenu(
                    expanded = showLibraryOptions,
                    onDismissRequest = { showLibraryOptions = false },
                    modifier = Modifier.widthIn(min = 248.dp, max = 310.dp)
                        .heightIn(max = menuMaxHeight).compensateDropdownMenuPadding(),
                    shape = neonShape(20.dp),
                    containerColor = MaterialTheme.colorScheme.surface,
                    tonalElevation = 0.dp,
                    shadowElevation = 12.dp,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
                ) {
                    Spacer(Modifier.height(8.dp))
                    LibraryGameContextMenuItem(
                        text = stringResource(R.string.library_refresh),
                        icon = Icons.Rounded.Refresh,
                        emphasized = true,
                        enabled = !isRefreshing,
                        onClick = { showLibraryOptions = false; onRefresh() }
                    )
                    LibraryGameContextMenuItem(
                        text = stringResource(R.string.library_search_hint),
                        icon = Icons.Rounded.Search,
                        selected = searchExpanded,
                        onClick = { showLibraryOptions = false; onToggleSearch() }
                    )
                    LibraryGameContextMenuDivider()
                    LibrarySortOption.entries.forEach { option ->
                        val label = when (option) {
                            LibrarySortOption.TITLE_ASC -> R.string.library_sort_title_asc
                            LibrarySortOption.TITLE_DESC -> R.string.library_sort_title_desc
                            LibrarySortOption.RECENT_DESC -> R.string.library_sort_recent_desc
                            LibrarySortOption.RECENT_ASC -> R.string.library_sort_recent_asc
                            LibrarySortOption.SIZE_DESC -> R.string.library_sort_size_desc
                            LibrarySortOption.SIZE_ASC -> R.string.library_sort_size_asc
                        }
                        LibraryGameContextMenuItem(
                            text = stringResource(label),
                            icon = when (option) {
                                LibrarySortOption.TITLE_ASC, LibrarySortOption.TITLE_DESC -> Icons.Rounded.SortByAlpha
                                LibrarySortOption.RECENT_DESC, LibrarySortOption.RECENT_ASC -> Icons.Rounded.History
                                LibrarySortOption.SIZE_DESC, LibrarySortOption.SIZE_ASC -> Icons.Rounded.Storage
                            },
                            selected = sortOption == option,
                            onClick = { showLibraryOptions = false; onSortSelected(option) }
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                }
            }
            LibraryHeaderActionDivider()
            IconButton(modifier = Modifier.size(42.dp), onClick = onShowShelf) {
                Icon(
                    imageVector = Icons.Rounded.ViewCarousel,
                    contentDescription = stringResource(R.string.library_view_shelf),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            LibraryHeaderActionDivider()
            IconButton(modifier = Modifier.size(42.dp), onClick = onToggleLayout) {
                Icon(
                    imageVector = if (layoutMode == LibraryLayoutMode.LIST) Icons.Rounded.ViewModule else Icons.Rounded.ViewAgenda,
                    contentDescription = stringResource(if (layoutMode == LibraryLayoutMode.LIST) R.string.library_view_grid else R.string.library_view_list),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

// Match X: let menu items scroll through the entire surface instead of fixed 8dp bands.
private fun Modifier.compensateDropdownMenuPadding(): Modifier = layout { measurable, constraints ->
    val padding = 8.dp.roundToPx()
    val childMaxHeight = if (constraints.hasBoundedHeight) constraints.maxHeight + padding * 2 else Constraints.Infinity
    val placeable = measurable.measure(constraints.copy(minHeight = 0, maxHeight = childMaxHeight))
    layout(placeable.width, (placeable.height - padding * 2).coerceAtLeast(0)) {
        placeable.place(0, -padding)
    }
}

@Composable
private fun LibraryHeaderActionDivider() {
    Box(
        modifier = Modifier
            .width(1.dp)
            .height(20.dp)
            .background(MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    )
}

@Composable
private fun LibraryGameContextMenu(
    expanded: Boolean,
    onDismiss: () -> Unit,
    addShortcutLabel: String,
    onAddShortcut: () -> Unit,
    manageGameSettingsLabel: String,
    onManageGameSettings: () -> Unit,
    playTimeLabel: String,
    onOpenPlayTime: () -> Unit,
    achievementsLabel: String,
    onOpenAchievements: () -> Unit,
    manageSaveStatesLabel: String,
    onOpenSaveStates: () -> Unit,
    deleteGameLabel: String,
    onDeleteGame: () -> Unit
) {
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismiss,
        modifier = Modifier.widthIn(min = 248.dp, max = 310.dp).compensateDropdownMenuPadding(),
        shape = neonShape(20.dp),
        containerColor = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 12.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
    ) {
        Spacer(Modifier.height(8.dp))
        LibraryGameContextMenuItem(
            text = addShortcutLabel,
            icon = Icons.Rounded.Add,
            onClick = onAddShortcut
        )
        LibraryGameContextMenuDivider()
        LibraryGameContextMenuItem(
            text = manageGameSettingsLabel,
            icon = Icons.Rounded.Tune,
            onClick = onManageGameSettings
        )
        LibraryGameContextMenuItem(
            text = playTimeLabel,
            icon = Icons.Rounded.QueryStats,
            onClick = onOpenPlayTime
        )
        LibraryGameContextMenuItem(
            text = achievementsLabel,
            icon = Icons.Rounded.EmojiEvents,
            onClick = onOpenAchievements
        )
        LibraryGameContextMenuItem(
            text = manageSaveStatesLabel,
            icon = Icons.Rounded.Save,
            onClick = onOpenSaveStates
        )
        LibraryGameContextMenuDivider()
        LibraryGameContextMenuItem(
            text = deleteGameLabel,
            icon = Icons.Rounded.Delete,
            onClick = onDeleteGame,
            destructive = true
        )
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun LibraryGameContextMenuItem(
    text: String,
    icon: ImageVector,
    onClick: () -> Unit,
    destructive: Boolean = false,
    emphasized: Boolean = false,
    enabled: Boolean = true,
    selected: Boolean = false
) {
    val contentAlpha = if (enabled) 1f else 0.38f
    val itemColor = when {
        destructive -> MaterialTheme.colorScheme.error
        emphasized -> MaterialTheme.colorScheme.primary
        else -> MaterialTheme.colorScheme.onSurface
    }.copy(alpha = contentAlpha)
    val iconContainerColor = if (destructive) {
        MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.78f)
    } else if (emphasized) {
        MaterialTheme.colorScheme.primaryContainer
    } else {
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.68f)
    }
    val iconColor = when {
        destructive -> MaterialTheme.colorScheme.onErrorContainer
        emphasized -> MaterialTheme.colorScheme.onPrimaryContainer
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }.copy(alpha = contentAlpha)

    DropdownMenuItem(
        text = {
            Text(
                text = text,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = if (emphasized) FontWeight.SemiBold else FontWeight.Medium),
                color = itemColor,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        },
        onClick = onClick,
        enabled = enabled,
        leadingIcon = {
            Box(
                modifier = Modifier
                    .size(34.dp)
                    .clip(neonShape(11.dp))
                    .background(iconContainerColor.copy(alpha = iconContainerColor.alpha * contentAlpha)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(19.dp),
                    tint = iconColor
                )
            }
        },
        trailingIcon = if (selected) {
            {
                Icon(Icons.Rounded.CheckCircle, contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(18.dp))
            }
        } else null,
        contentPadding = PaddingValues(horizontal = 12.dp)
    )
}

@Composable
private fun LibraryGameContextMenuDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(horizontal = 14.dp, vertical = 5.dp),
        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
    )
}

@Composable
private fun LibraryGameArtwork(
    titleId: String,
    path: String?,
    artworkVersion: Long,
    title: String,
    modifier: Modifier = Modifier,
    artworkAspectRatio: Float = 2f / 3f
) {
    Surface(
        modifier = modifier,
        shape = RectangleShape,
        color = Color.Transparent
    ) {
        GameCoverArt(
            titleId = titleId,
            fallbackIconPath = path,
            artworkVersion = artworkVersion,
            contentDescription = title,
            fallbackTitle = title,
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(artworkAspectRatio)
        )
    }
}
