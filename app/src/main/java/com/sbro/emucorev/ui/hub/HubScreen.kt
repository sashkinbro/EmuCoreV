package com.sbro.emucorev.ui.hub

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.MenuBook
import androidx.compose.material.icons.rounded.Article
import androidx.compose.material.icons.rounded.Bookmark
import androidx.compose.material.icons.rounded.BookmarkBorder
import androidx.compose.material.icons.rounded.CalendarMonth
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FilterList
import androidx.compose.material.icons.rounded.HistoryEdu
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Newspaper
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.VideoLibrary
import com.sbro.emucorev.ui.theme.style.Button
import androidx.compose.material3.Checkbox
import com.sbro.emucorev.ui.theme.style.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import com.sbro.emucorev.ui.theme.style.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.Text
import com.sbro.emucorev.ui.theme.style.TextButton
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.rememberControllerConnected
import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.data.hub.HubKind
import com.sbro.emucorev.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorev.ui.common.RequestFocusOnResume
import com.sbro.emucorev.ui.common.StickyTabHeader
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.rememberStickyTabHeaderPinned
import com.sbro.emucorev.ui.common.stickyTabTopInset
import com.sbro.emucorev.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.common.shimmer
import com.sbro.emucorev.ui.common.tvFocusGroup
import com.sbro.emucorev.ui.common.tvGamepadFocusableCard
import com.sbro.emucorev.ui.common.appScreenTopPadding
import com.sbro.emucorev.ui.store.StoreViewModel
import com.sbro.emucorev.ui.store.rememberStoreContentModel
import com.sbro.emucorev.ui.store.rememberStoreFilters
import com.sbro.emucorev.ui.store.shelfTitle
import com.sbro.emucorev.ui.store.storeContent
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.Instant
import java.time.LocalDate
import java.time.YearMonth
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonShapeCorners
import com.sbro.emucorev.ui.theme.neon.neonButtonShape

@Composable
fun HubScreen(
    onBackClick: () -> Unit,
    onOpenArticle: (String) -> Unit,
    onOpenStoreItem: (String) -> Unit,
    onOpenInstalled: () -> Unit,
    initialTab: String? = null,
    initialStoreTab: String? = null,
    backEnabled: Boolean = true,
    viewModel: HubViewModel = viewModel(),
    storeViewModel: StoreViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val storeFilters = rememberStoreFilters(initialStoreTab)
    val storeActive = uiState.mode == HubMode.STORE
    val section = uiState.section
    val listActive = !storeActive && (section != null || uiState.searchQuery.isNotBlank())
    val storeModel = rememberStoreContentModel(storeViewModel, storeFilters, if (storeActive) uiState.searchQuery else "")
    val storeRefreshing = storeModel.refreshing
    val refreshAll: () -> Unit = {
        viewModel.refresh(force = true)
        storeViewModel.refresh(force = true)
    }
    LaunchedEffect(initialTab) {
        if (initialTab == "store") viewModel.selectMode(HubMode.STORE)
    }
    // Back closes an open section / Saved list first; the detail overlay (backEnabled = false) keeps priority.
    BackHandler(enabled = backEnabled && section != null) { viewModel.closeSection() }
    // Inside a Store "See all" grid, back returns to the Store home.
    BackHandler(enabled = backEnabled && storeActive && storeFilters.shelf != null) { storeFilters.closeShelf() }
    val discoverFeed = remember(uiState.items) { buildDiscoverFeed(uiState.items) }
    // Where the user is, as an ordered key: forward navigation slides content in from the end, back from the start.
    val navKey = if (storeActive) 100 + storeFilters.tab.ordinal * 10 + (if (storeFilters.shelf != null) 5 else 0)
    else if (section != null) 1 else 0
    val previousNavKey = remember { intArrayOf(navKey) }
    val navDirection = remember(navKey) { (if (navKey >= previousNavKey[0]) 1 else -1).also { previousNavKey[0] = navKey } }
    val motionEnabled = rememberMotionEnabled()
    val shownKeys = remember(navKey) { HashSet<String>() }
    val motionState = remember(motionEnabled, navDirection, shownKeys) { HubMotionState(motionEnabled, navDirection, shownKeys) }
    val storeShelfItems = remember(storeModel.snapshot) {
        storeModel.snapshot?.catalog?.items.orEmpty().sortedByDescending { it.publishedAt }.take(12)
    }
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    var playingVideo by remember { mutableStateOf<HubItem?>(null) }
    var minimumSkeletonVisible by remember(viewModel) { mutableStateOf(!viewModel.hasShownInitialSkeleton) }
    val selectedTabFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = rememberControllerConnected()
    val topInset = appScreenTopPadding()
    val stickyTabInset = stickyTabTopInset()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val compact = LocalConfiguration.current.screenWidthDp < 600
    val horizontalPadding = if (compact) 12.dp else ScreenHorizontalPadding
    val scope = rememberCoroutineScope()

    LaunchedEffect(viewModel, minimumSkeletonVisible) {
        if (minimumSkeletonVisible) {
            delay(MINIMUM_SKELETON_DURATION_MS)
            viewModel.markInitialSkeletonShown()
            minimumSkeletonVisible = false
        }
    }

    ProvideGamepadShoulderActions(
        enabled = !searchVisible,
        onPrevious = viewModel::toggleMode,
        onNext = viewModel::toggleMode
    )
    LaunchedEffect(uiState.mode, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            withFrameNanos { }
            runCatching { selectedTabFocusRequester.requestFocus() }
        }
    }
    RequestFocusOnResume(
        focusRequester = selectedTabFocusRequester,
        enabled = shouldRequestGamepadFocus
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues())
    ) {
        // Text without an explicit color must read on the screen background, not default to black.
        androidx.compose.runtime.CompositionLocalProvider(androidx.compose.material3.LocalContentColor provides MaterialTheme.colorScheme.onBackground) {
        val gridState = rememberSaveable(saver = LazyGridState.Saver) { LazyGridState() }
        val hubTabsPinned = rememberStickyTabHeaderPinned(gridState, "hub-tabs")
        val listMode = "${uiState.mode.name}:${section?.name}:${uiState.sortOrder.name}:${uiState.productFilter.name}:${uiState.featuredOnly}:${storeFilters.modeKey}"
        var lastListMode by rememberSaveable { mutableStateOf(listMode) }
        val showScrollToTop by remember {
            derivedStateOf {
                gridState.firstVisibleItemIndex > 2 || gridState.firstVisibleItemScrollOffset > 900
            }
        }
        LaunchedEffect(listMode) {
            if (lastListMode != listMode) {
                gridState.scrollToItem(0)
                lastListMode = listMode
            }
        }
        PullToRefreshBox(
            isRefreshing = uiState.showRefreshIndicator || storeRefreshing,
            onRefresh = refreshAll,
            modifier = Modifier.fillMaxSize()
        ) {
            androidx.compose.runtime.CompositionLocalProvider(LocalHubMotion provides motionState) {
            LazyVerticalGrid(
                columns = if (storeActive) {
                    GridCells.Fixed(
                        ((LocalConfiguration.current.screenWidthDp - horizontalPadding.value * 2) / 180).toInt().coerceIn(2, 6)
                    )
                } else GridCells.Adaptive(300.dp),
                state = gridState,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = stickyTabInset),
                contentPadding = PaddingValues(
                    start = horizontalPadding,
                    end = horizontalPadding,
                    bottom = bottomInset + 18.dp
                ),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                item(key = "hub-header", span = { GridItemSpan(maxLineSpan) }) {
                    Column {
                        HubTopBar(
                            title = section?.title() ?: storeModel.shelfTitle().takeIf { storeActive } ?: stringResource(R.string.hub_title_short),
                            topInset = (topInset - stickyTabInset).coerceAtLeast(0.dp),
                            onBackClick = { if (!(storeActive && storeFilters.closeShelf()) && !viewModel.closeSection()) onBackClick() },
                            onSearchClick = {
                                searchVisible = !searchVisible
                                if (!searchVisible) viewModel.clearSearch()
                            },
                            onSavedClick = { viewModel.openSection(HubSection.SAVED) },
                            onInstalledClick = onOpenInstalled.takeIf { storeActive },
                            savedActive = section == HubSection.SAVED,
                            onRefresh = refreshAll,
                            refreshing = uiState.isRefreshing || storeRefreshing,
                            showFilters = listActive,
                            sortOrder = uiState.sortOrder,
                            productFilter = uiState.productFilter,
                            featuredOnly = uiState.featuredOnly,
                            hasActiveFilters = uiState.hasActiveFilters,
                            onSortOrderSelected = viewModel::setSortOrder,
                            onProductFilterSelected = viewModel::setProductFilter,
                            onFeaturedOnlyChanged = viewModel::setFeaturedOnly,
                            onClearFilters = viewModel::clearFilters
                        )
                        AnimatedVisibility(
                            visible = searchVisible,
                            enter = expandVertically(
                                animationSpec = tween(durationMillis = 220),
                                expandFrom = Alignment.Top
                            ) + fadeIn(animationSpec = tween(durationMillis = 180)),
                            exit = shrinkVertically(
                                animationSpec = tween(durationMillis = 190),
                                shrinkTowards = Alignment.Top
                            ) + fadeOut(animationSpec = tween(durationMillis = 140))
                        ) {
                            OutlinedTextField(
                                value = uiState.searchQuery,
                                onValueChange = viewModel::setSearchQuery,
                                modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp),
                                singleLine = true,
                                shape = neonShape(18.dp),
                                label = { Text(stringResource(R.string.hub_search)) },
                                placeholder = { Text(stringResource(R.string.hub_search_hint)) },
                                leadingIcon = {
                                    Icon(
                                        Icons.Rounded.Search,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                },
                                trailingIcon = {
                                    if (uiState.searchQuery.isNotEmpty()) {
                                        IconButton(onClick = viewModel::clearSearch) {
                                            Icon(
                                                Icons.Rounded.Close,
                                                contentDescription = stringResource(R.string.hub_close),
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                }
                            )
                        }
                    }
                }
                stickyHeader(key = "hub-tabs") {
                    StickyTabHeader(
                        showDivider = hubTabsPinned,
                        modifier = Modifier.expandIntoHorizontalPadding(horizontalPadding)
                    ) {
                        HubModeSwitch(
                            mode = uiState.mode,
                            onSelected = viewModel::selectMode,
                            selectedFocusRequester = selectedTabFocusRequester,
                            horizontalPadding = horizontalPadding
                        )
                    }
                }
                if (!storeActive && (uiState.isOffline || uiState.fallbackLocale != null)) {
                    item(key = "hub-status", span = { GridItemSpan(maxLineSpan) }) {
                        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            if (uiState.isOffline) {
                                HubStatusBanner(
                                    text = stringResource(R.string.hub_offline_cached),
                                    containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                                    contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                                )
                            } else if (uiState.fallbackLocale != null) {
                                HubStatusBanner(
                                    text = stringResource(R.string.hub_language_fallback),
                                    containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                    contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                        }
                    }
                }

                when {
                    storeActive -> storeContent(storeModel, onOpenStoreItem, horizontalPadding)
                    minimumSkeletonVisible || (uiState.isInitialLoading && uiState.items.isEmpty()) -> {
                        if (listActive) {
                            items(6, key = { "hub-skeleton-$it" }) {
                                HubSkeletonCard(compact = compact)
                            }
                        } else {
                            discoverSkeleton(compact, horizontalPadding)
                        }
                    }
                    uiState.error != null && uiState.items.isEmpty() -> {
                        item(key = "hub-error", span = { GridItemSpan(maxLineSpan) }) {
                            HubErrorState(onRetry = { viewModel.refresh(true) })
                        }
                    }
                    !listActive -> {
                        if (discoverFeed.isEmpty) {
                            item(key = "hub-empty", span = { GridItemSpan(maxLineSpan) }) {
                                HubEmptyState(
                                    section = null,
                                    searchActive = false,
                                    filterActive = false,
                                    onClearSearch = viewModel::clearSearch,
                                    onClearFilters = viewModel::clearFilters
                                )
                            }
                        } else {
                            val openItem: (HubItem) -> Unit = { item ->
                                if (item.kind == HubKind.VIDEOS && item.providerId != null) playingVideo = item
                                else onOpenArticle(item.id)
                            }
                            discoverContent(
                                feed = discoverFeed,
                                storeItems = storeShelfItems,
                                storeSnapshot = storeModel.snapshot,
                                storeLocale = storeViewModel.locale,
                                compact = compact,
                                horizontalPadding = horizontalPadding,
                                onOpenItem = openItem,
                                onSeeAll = viewModel::openSection,
                                onSeeAllStore = { viewModel.selectMode(HubMode.STORE) },
                                onOpenStoreItem = onOpenStoreItem
                            )
                        }
                    }
                    uiState.visibleItems.isEmpty() -> {
                        item(key = "hub-empty", span = { GridItemSpan(maxLineSpan) }) {
                            HubEmptyState(
                                section = section,
                                searchActive = uiState.searchQuery.isNotBlank(),
                                filterActive = uiState.hasActiveFilters,
                                onClearSearch = viewModel::clearSearch,
                                onClearFilters = viewModel::clearFilters
                            )
                        }
                    }
                    else -> {
                        items(items = uiState.visibleItems, key = HubItem::id) { item ->
                            Box(Modifier.animateItem().enterRise("hub-card-${item.id}")) {
                                HubCard(
                                    item = item,
                                    compact = compact,
                                    onOpen = {
                                        if (item.kind == HubKind.VIDEOS && item.providerId != null) playingVideo = item
                                        else onOpenArticle(item.id)
                                    },
                                    onToggleFavorite = { viewModel.toggleFavorite(item) }
                                )
                            }
                        }
                    }
                }
            }
            }

            HubScrollToTopButton(
                visible = showScrollToTop,
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(end = 16.dp, bottom = 16.dp + bottomInset),
                onClick = {
                    scope.launch { gridState.animateScrollToItem(0) }
                }
            )
        }
        }
    }

    playingVideo?.let { video ->
        video.providerId?.let { id ->
            HubVideoPlayer(youtubeId = id, title = video.title, onDismiss = { playingVideo = null })
        }
    }
}

private const val MINIMUM_SKELETON_DURATION_MS = 500L

@Composable
private fun HubTopBar(
    title: String,
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onSearchClick: () -> Unit,
    onSavedClick: () -> Unit,
    onInstalledClick: (() -> Unit)?,
    savedActive: Boolean,
    onRefresh: () -> Unit,
    refreshing: Boolean,
    showFilters: Boolean,
    sortOrder: HubSortOrder,
    productFilter: HubProductFilter,
    featuredOnly: Boolean,
    hasActiveFilters: Boolean,
    onSortOrderSelected: (HubSortOrder) -> Unit,
    onProductFilterSelected: (HubProductFilter) -> Unit,
    onFeaturedOnlyChanged: (Boolean) -> Unit,
    onClearFilters: () -> Unit
) {
    com.sbro.emucorev.ui.common.ScreenTopBar(
        title = title,
        onBackClick = onBackClick,
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                top = topInset,
                bottom = 2.dp
            ),
        actions = {
            IconButton(onClick = onSearchClick) {
                Icon(
                    Icons.Rounded.Search,
                    contentDescription = stringResource(R.string.hub_search),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (onInstalledClick != null) {
                IconButton(onClick = onInstalledClick) {
                    Icon(
                        Icons.Rounded.Inventory2,
                        contentDescription = stringResource(R.string.store_installed_title),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            IconButton(onClick = onSavedClick) {
                Icon(
                    if (savedActive) Icons.Rounded.Bookmark else Icons.Rounded.BookmarkBorder,
                    contentDescription = stringResource(R.string.hub_tab_favorites),
                    tint = if (savedActive) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (showFilters) HubFilterMenu(
                sortOrder = sortOrder,
                productFilter = productFilter,
                featuredOnly = featuredOnly,
                hasActiveFilters = hasActiveFilters,
                onSortOrderSelected = onSortOrderSelected,
                onProductFilterSelected = onProductFilterSelected,
                onFeaturedOnlyChanged = onFeaturedOnlyChanged,
                onClearFilters = onClearFilters
            )
            IconButton(onClick = onRefresh, enabled = !refreshing) {
                Icon(
                    Icons.Rounded.Refresh,
                    contentDescription = stringResource(R.string.hub_refresh),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    )
}

@Composable
private fun HubFilterMenu(
    sortOrder: HubSortOrder,
    productFilter: HubProductFilter,
    featuredOnly: Boolean,
    hasActiveFilters: Boolean,
    onSortOrderSelected: (HubSortOrder) -> Unit,
    onProductFilterSelected: (HubProductFilter) -> Unit,
    onFeaturedOnlyChanged: (Boolean) -> Unit,
    onClearFilters: () -> Unit
) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    Box {
        IconButton(onClick = { expanded = true }) {
            Box {
                Icon(
                    imageVector = Icons.Rounded.FilterList,
                    contentDescription = stringResource(R.string.hub_sort_and_filter),
                    tint = if (hasActiveFilters) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (hasActiveFilters) {
                    Box(
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .size(7.dp)
                            .background(MaterialTheme.colorScheme.primary, CircleShape)
                    )
                }
            }
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.widthIn(min = 280.dp),
            shape = neonShape(20.dp)
        ) {
            Text(
                text = stringResource(R.string.hub_sort_and_filter),
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = stringResource(R.string.hub_sort_by),
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.primary
            )
            HubSortOrder.entries.forEach { option ->
                DropdownMenuItem(
                    text = { Text(option.label()) },
                    leadingIcon = {
                        RadioButton(selected = sortOrder == option, onClick = null)
                    },
                    onClick = { onSortOrderSelected(option) }
                )
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            Text(
                text = stringResource(R.string.hub_filter_by),
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.primary
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.hub_featured_only)) },
                leadingIcon = { Checkbox(checked = featuredOnly, onCheckedChange = null) },
                onClick = { onFeaturedOnlyChanged(!featuredOnly) }
            )
            HubProductFilter.entries.forEach { option ->
                DropdownMenuItem(
                    text = { Text(option.label()) },
                    leadingIcon = {
                        RadioButton(selected = productFilter == option, onClick = null)
                    },
                    onClick = { onProductFilterSelected(option) }
                )
            }
            if (hasActiveFilters) {
                HorizontalDivider(modifier = Modifier.padding(top = 4.dp))
                DropdownMenuItem(
                    text = {
                        Text(
                            text = stringResource(R.string.hub_clear_filters),
                            color = MaterialTheme.colorScheme.primary,
                            fontWeight = FontWeight.SemiBold
                        )
                    },
                    onClick = onClearFilters
                )
            }
        }
    }
}

@Composable
private fun HubSortOrder.label(): String = when (this) {
    HubSortOrder.NEWEST_FIRST -> stringResource(R.string.hub_sort_newest)
    HubSortOrder.OLDEST_FIRST -> stringResource(R.string.hub_sort_oldest)
    HubSortOrder.TITLE_ASCENDING -> stringResource(R.string.hub_sort_title)
}

@Composable
private fun HubProductFilter.label(): String = when (this) {
    HubProductFilter.ALL -> stringResource(R.string.hub_filter_all_topics)
    HubProductFilter.EMUCOREX -> stringResource(R.string.hub_filter_emucorex)
    HubProductFilter.EMUCOREV -> stringResource(R.string.hub_filter_emucorev)
    HubProductFilter.PLAYSTATION_2 -> stringResource(R.string.hub_filter_ps2)
    HubProductFilter.OTHER_EMULATORS -> stringResource(R.string.hub_filter_other_emulators)
}

@Composable
internal fun HubScrollToTopButton(
    visible: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(tween(180)) + scaleIn(tween(180)),
        exit = fadeOut(tween(140)) + scaleOut(tween(140)),
        modifier = modifier
    ) {
        val shape = neonShape(18.dp)
        val interactionSource = remember { MutableInteractionSource() }
        Box(
            modifier = Modifier
                .gamepadFocusableCard(
                    shape = shape,
                    interactionSource = interactionSource,
                    addFocusTarget = false
                )
                .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.78f), shape)
                .clickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = onClick
                )
                .padding(horizontal = 14.dp, vertical = 14.dp),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Rounded.KeyboardArrowUp,
                contentDescription = stringResource(R.string.hub_scroll_to_top),
                tint = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.size(22.dp)
            )
        }
    }
}

internal fun Modifier.expandIntoHorizontalPadding(horizontalPadding: androidx.compose.ui.unit.Dp): Modifier =
    layout { measurable, constraints ->
        if (!constraints.hasBoundedWidth) {
            val placeable = measurable.measure(constraints)
            layout(placeable.width, placeable.height) { placeable.placeRelative(0, 0) }
        } else {
            val inset = horizontalPadding.roundToPx()
            val expandedWidth = constraints.maxWidth + inset * 2
            val placeable = measurable.measure(
                constraints.copy(minWidth = expandedWidth, maxWidth = expandedWidth)
            )
            layout(constraints.maxWidth, placeable.height) {
                placeable.placeRelative(-inset, 0)
            }
        }
    }

@Composable
private fun HubCard(item: HubItem, compact: Boolean, onOpen: () -> Unit, onToggleFavorite: () -> Unit) {
    val interactionSource = remember { MutableInteractionSource() }
    val cardRadius = if (compact) 18.dp else 22.dp
            val shape = neonShape(cardRadius)
    val open = rememberDebouncedClick(onClick = onOpen)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .widthIn(max = 560.dp)
            .pressScale(interactionSource)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(if (compact) 2f else 16f / 9f)
                    .clip(neonShapeCorners(topStart = cardRadius, topEnd = cardRadius))
            ) {
                HubImage(
                    spec = item.heroThumbnailUrl?.let {
                        HubImageSpec(
                            url = it,
                            sha256 = item.heroThumbnailSha256,
                            bytes = item.heroThumbnailBytes,
                            contentDescription = item.title
                        )
                    },
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Crop
                )
                Box(
                    modifier = Modifier.matchParentSize().background(
                        Brush.verticalGradient(
                            colorStops = arrayOf(
                                0f to Color.Black.copy(alpha = 0.06f),
                                0.52f to Color.Transparent,
                                1f to Color.Black.copy(alpha = 0.68f)
                            )
                        )
                    )
                )
                if (item.kind == HubKind.VIDEOS) {
                    Surface(
                        modifier = Modifier.align(Alignment.Center),
                        shape = CircleShape,
                        color = Color.Black.copy(alpha = 0.58f)
                    ) {
                        Icon(
                            Icons.Rounded.PlayArrow,
                            contentDescription = stringResource(R.string.hub_watch_video),
                            tint = Color.White,
                            modifier = Modifier.padding(if (compact) 10.dp else 14.dp).size(if (compact) 24.dp else 28.dp)
                        )
                    }
                }
                Surface(
                    modifier = Modifier.align(Alignment.BottomStart).padding(if (compact) 8.dp else 10.dp),
                    shape = neonShape(999.dp),
                    color = Color.Black.copy(alpha = 0.55f)
                ) {
                    Text(
                        text = item.kind.label(),
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                        color = Color.White,
                        style = if (compact) MaterialTheme.typography.labelSmall else MaterialTheme.typography.labelMedium
                    )
                }
            }
            Column(
                modifier = Modifier.padding(if (compact) 11.dp else 14.dp),
                verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)
            ) {
                Row(verticalAlignment = Alignment.Top) {
                    Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                        Text(
                            text = item.title,
                            style = if (compact) MaterialTheme.typography.titleSmall else MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = item.displayDate(),
                            style = if (compact) MaterialTheme.typography.labelSmall else MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                    IconButton(onClick = onToggleFavorite, modifier = Modifier.size(if (compact) 40.dp else 48.dp)) {
                        Icon(
                            imageVector = if (item.isFavorite) Icons.Rounded.Bookmark else Icons.Rounded.BookmarkBorder,
                            contentDescription = stringResource(if (item.isFavorite) R.string.hub_remove_saved else R.string.hub_save),
                            tint = if (item.isFavorite) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Text(
                    text = item.summary,
                    style = if (compact) MaterialTheme.typography.bodySmall else MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.78f),
                    maxLines = if (compact) 2 else 3,
                    overflow = TextOverflow.Ellipsis
                )
                if (item.channelTitle != null) {
                    Text(
                        text = item.channelTitle,
                        style = if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }
    }
}

@Composable
private fun HubSkeletonCard(compact: Boolean) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
                .clip(neonShape(if (compact) 18.dp else 22.dp))
            .background(MaterialTheme.colorScheme.surfaceContainerLow)
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(if (compact) 2f else 16f / 9f).shimmer())
        Column(Modifier.padding(if (compact) 11.dp else 14.dp), verticalArrangement = Arrangement.spacedBy(if (compact) 7.dp else 10.dp)) {
            Box(Modifier.fillMaxWidth(0.82f).height(20.dp).clip(neonShape(8.dp)).shimmer())
            Box(Modifier.fillMaxWidth(0.42f).height(14.dp).clip(neonShape(8.dp)).shimmer())
            Box(Modifier.fillMaxWidth().height(52.dp).clip(neonShape(10.dp)).shimmer())
        }
    }
}

@Composable
private fun HubErrorState(onRetry: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxWidth().height(360.dp).padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Rounded.ErrorOutline, contentDescription = null, modifier = Modifier.size(52.dp), tint = MaterialTheme.colorScheme.error)
        Text(
            stringResource(R.string.hub_load_failed),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(top = 14.dp, bottom = 12.dp)
        )
        Button(
            shape = neonButtonShape(),onClick = onRetry) { Text(stringResource(R.string.hub_retry)) }
    }
}

@Composable
internal fun HubEmptyState(
    section: HubSection?,
    searchActive: Boolean,
    filterActive: Boolean,
    onClearSearch: () -> Unit,
    onClearFilters: () -> Unit
) {
    val text = when {
        searchActive -> stringResource(R.string.hub_no_search_results)
        filterActive -> stringResource(R.string.hub_no_filter_results)
        else -> when (section) {
        null -> stringResource(R.string.hub_empty_news)
        HubSection.NEWS -> stringResource(R.string.hub_empty_news)
        HubSection.VIDEOS -> stringResource(R.string.hub_empty_videos)
        HubSection.HISTORY -> stringResource(R.string.hub_empty_history)
        HubSection.MANUALS -> stringResource(R.string.hub_empty_manuals)
        HubSection.SAVED -> stringResource(R.string.hub_empty_favorites)
        }
    }
    val icon = when {
        searchActive -> Icons.Rounded.Search
        filterActive -> Icons.Rounded.FilterList
        section == HubSection.VIDEOS -> Icons.Rounded.VideoLibrary
        section == HubSection.HISTORY -> Icons.Rounded.HistoryEdu
        section == HubSection.MANUALS -> Icons.AutoMirrored.Rounded.MenuBook
        section == HubSection.SAVED -> Icons.Rounded.BookmarkBorder
        else -> Icons.Rounded.Newspaper
    }
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .height(340.dp)
            .padding(vertical = 8.dp),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Box(modifier = Modifier.size(74.dp), contentAlignment = Alignment.Center) {
                    Icon(imageVector = icon, contentDescription = null, modifier = Modifier.size(34.dp))
                }
            }
            Text(
                text = stringResource(if (searchActive || filterActive) R.string.hub_no_matches else R.string.hub_empty_state_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .padding(top = 18.dp)
                    .fillMaxWidth()
                    .widthIn(max = 560.dp)
            )
            Text(
                text = text,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .padding(top = 8.dp)
                    .fillMaxWidth()
                    .widthIn(max = 560.dp),
                maxLines = 3
            )
            when {
                searchActive -> TextButton(onClick = onClearSearch, modifier = Modifier.padding(top = 10.dp)) {
                    Text(stringResource(R.string.hub_clear_search))
                }
                filterActive -> TextButton(onClick = onClearFilters, modifier = Modifier.padding(top = 10.dp)) {
                    Text(stringResource(R.string.hub_clear_filters))
                }
            }
        }
    }
}

@Composable
private fun HubStatusBanner(text: String, containerColor: Color, contentColor: Color) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        shape = neonShape(14.dp),
        color = containerColor
    ) {
        Text(text, modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp), color = contentColor, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun HubKind.label(): String = when (this) {
    HubKind.NEWS -> stringResource(R.string.hub_tab_news)
    HubKind.VIDEOS -> stringResource(R.string.hub_tab_videos)
    HubKind.HISTORY -> stringResource(R.string.hub_tab_history)
    HubKind.MANUALS -> stringResource(R.string.hub_tab_manuals)
}

internal fun HubItem.displayDate(): String = runCatching {
    when {
        kind == HubKind.HISTORY && datePrecision == "year" -> eventDate.orEmpty()
        kind == HubKind.HISTORY && datePrecision == "month" -> YearMonth.parse(eventDate).format(DateTimeFormatter.ofPattern("LLLL yyyy"))
        kind == HubKind.HISTORY -> LocalDate.parse(eventDate).format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM))
        else -> Instant.parse(publishedAt).atZone(java.time.ZoneId.systemDefault()).toLocalDate()
            .format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM).withLocale(Locale.getDefault()))
    }
}.getOrDefault(eventDate ?: publishedAt.substringBefore('T'))
