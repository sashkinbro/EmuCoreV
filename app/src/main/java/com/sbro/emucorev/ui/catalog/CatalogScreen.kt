package com.sbro.emucorev.ui.catalog

import android.annotation.SuppressLint
import androidx.compose.foundation.BorderStroke
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.FilterList
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.ProfileGameListRepository
import com.sbro.emucorev.data.ProfileGameStatus
import com.sbro.emucorev.data.VitaCompatibilityState
import com.sbro.emucorev.data.VitaCompatibilitySummary
import com.sbro.emucorev.data.VitaCatalogEntry
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.UrlImage
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.useMultiColumnLayout
import kotlinx.coroutines.launch
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("DefaultLocale", "FrequentlyChangingValue", "ConfigurationScreenWidthHeight")
@Composable
fun CatalogScreen(
    onGameClick: (Long) -> Unit,
    onBackClick: () -> Unit,
    viewModel: CatalogViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val configuration = LocalConfiguration.current
    val context = LocalContext.current
    val useDenseCards = configuration.useMultiColumnLayout()
    val gridState = rememberLazyGridState()
    val scope = rememberCoroutineScope()
    val profileRepository = remember(context) { ProfileGameListRepository(context.applicationContext) }
    val showScrollToTop = gridState.firstVisibleItemIndex > 2 || gridState.firstVisibleItemScrollOffset > 900
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    var showGenreMenu by remember { mutableStateOf(false) }
    var showYearMenu by remember { mutableStateOf(false) }
    var showRatingMenu by remember { mutableStateOf(false) }
    var showSearchPanel by remember { mutableStateOf(false) }
    var showFiltersPanel by remember { mutableStateOf(false) }
    var profileStatuses by remember { mutableStateOf<Map<Long, ProfileGameStatus?>>(emptyMap()) }
    var favoriteGames by remember { mutableStateOf<Map<Long, Boolean>>(emptyMap()) }
    val hasActiveFilters = uiState.selectedGenre != null || uiState.selectedYear != null || uiState.minRating != null
    val isSearchPanelVisible = showSearchPanel || uiState.query.isNotBlank()
    val isFiltersPanelVisible = showFiltersPanel || hasActiveFilters
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    fun refreshProfileStatuses() {
        val entries = profileRepository.loadEntries()
        profileStatuses = entries.associate { it.igdbId to it.status }
        favoriteGames = entries.associate { it.igdbId to it.isFavorite }
    }

    DisposableEffect(viewModel) {
        viewModel.onScreenStart()
        refreshProfileStatuses()
        onDispose { viewModel.onScreenStop() }
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

            !uiState.hasCatalog -> {
                CatalogMessageCard(
                    title = stringResource(R.string.catalog_empty_title),
                    body = stringResource(R.string.catalog_empty_body),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = ScreenHorizontalPadding, vertical = topInset)
                        .align(Alignment.TopCenter)
                )
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
                        ScreenTopBar(
                            title = stringResource(R.string.catalog_title),
                            onBackClick = guardedBackClick,
                            actions = {
                                CatalogTopBarAction(
                                    icon = Icons.Rounded.Search,
                                    contentDescription = stringResource(R.string.catalog_toggle_search),
                                    selected = isSearchPanelVisible,
                                    onClick = { showSearchPanel = !showSearchPanel }
                                )
                                CatalogTopBarAction(
                                    icon = Icons.Rounded.FilterList,
                                    contentDescription = stringResource(R.string.catalog_toggle_filters),
                                    selected = isFiltersPanelVisible,
                                    onClick = { showFiltersPanel = !showFiltersPanel }
                                )
                            }
                        )
                    }

                    item(span = { GridItemSpan(maxLineSpan) }) {
                        AnimatedVisibility(
                            visible = isSearchPanelVisible,
                            enter = fadeIn(animationSpec = tween(160)) + expandVertically(animationSpec = tween(220)),
                            exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
                        ) {
                            CatalogSearchPanel(
                                query = uiState.query,
                                onQueryChange = viewModel::updateQuery
                            )
                        }
                    }

                    item(span = { GridItemSpan(maxLineSpan) }) {
                        AnimatedVisibility(
                            visible = isFiltersPanelVisible,
                            enter = fadeIn(animationSpec = tween(160)) + expandVertically(animationSpec = tween(220)),
                            exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
                        ) {
                            CatalogFiltersPanel(
                                hasActiveFilters = hasActiveFilters,
                                selectedGenre = uiState.selectedGenre,
                                selectedYear = uiState.selectedYear,
                                minRating = uiState.minRating,
                                availableGenres = uiState.availableGenres,
                                availableYears = uiState.availableYears,
                                showGenreMenu = showGenreMenu,
                                showYearMenu = showYearMenu,
                                showRatingMenu = showRatingMenu,
                                onGenreMenu = { showGenreMenu = it },
                                onYearMenu = { showYearMenu = it },
                                onRatingMenu = { showRatingMenu = it },
                                onGenre = viewModel::updateGenre,
                                onYear = viewModel::updateYear,
                                onMinRating = viewModel::updateMinRating,
                                onClear = viewModel::clearFilters
                            )
                        }
                    }

                    if (uiState.results.isEmpty()) {
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            CatalogMessageCard(
                                title = stringResource(R.string.catalog_no_results_title),
                                body = stringResource(R.string.catalog_no_results_body),
                                modifier = Modifier.fillMaxWidth()
                            )
                        }
                    } else {
                        items(uiState.results.size, key = { index -> uiState.results[index].igdbId }) { index ->
                            val game = uiState.results[index]
                            LaunchedEffect(index, uiState.results.size, uiState.hasMore, uiState.isLoadingMore) {
                                viewModel.loadMoreIfNeeded(index)
                            }
                            CatalogGameCard(
                                game = game,
                                onClick = { onGameClick(game.igdbId) },
                                compact = useDenseCards,
                                profileStatus = profileStatuses[game.igdbId],
                                isFavorite = favoriteGames[game.igdbId] == true,
                                onProfileStatusChange = { status ->
                                    profileRepository.setStatus(game.igdbId, status)
                                    refreshProfileStatuses()
                                },
                                onClearProfileStatus = {
                                    profileRepository.clearStatus(game.igdbId)
                                    refreshProfileStatuses()
                                },
                                onFavoriteChange = { favorite ->
                                    profileRepository.setFavorite(game.igdbId, favorite)
                                    refreshProfileStatuses()
                                }
                            )
                        }
                        if (uiState.isLoadingMore) {
                            item(span = { GridItemSpan(maxLineSpan) }) {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 16.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    CircularProgressIndicator(modifier = Modifier.size(28.dp), strokeWidth = 2.dp)
                                }
                            }
                        }
                    }
                }

                ScrollToTopButton(
                    visible = showScrollToTop,
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(end = 16.dp, bottom = 24.dp),
                    onClick = { scope.launch { gridState.animateScrollToItem(0) } }
                )
            }
        }
    }
}

@Composable
private fun CatalogTopBarAction(
    icon: ImageVector,
    contentDescription: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier.size(48.dp),
        shape = RoundedCornerShape(16.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
        } else {
            MaterialTheme.colorScheme.surface
        },
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.26f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.20f)
            }
        )
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = icon,
                contentDescription = contentDescription,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.size(24.dp)
            )
        }
    }
}

@Composable
private fun CatalogSearchPanel(
    query: String,
    onQueryChange: (String) -> Unit
) {
    OutlinedTextField(
        value = query,
        onValueChange = onQueryChange,
        modifier = Modifier.fillMaxWidth(),
        placeholder = { Text(text = stringResource(R.string.catalog_search_hint)) },
        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
        trailingIcon = {
            if (query.isNotBlank()) {
                val clearClick = rememberDebouncedClick { onQueryChange("") }
                Surface(
                    shape = RoundedCornerShape(999.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                    onClick = clearClick
                ) {
                    Box(
                        modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 8.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Close,
                            contentDescription = stringResource(R.string.catalog_search_clear),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }
        },
        singleLine = true,
        shape = RoundedCornerShape(20.dp),
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
            unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f),
            focusedContainerColor = MaterialTheme.colorScheme.surface,
            unfocusedContainerColor = MaterialTheme.colorScheme.surface
        )
    )
}

@Composable
private fun CatalogFiltersPanel(
    hasActiveFilters: Boolean,
    selectedGenre: String?,
    selectedYear: Int?,
    minRating: Float?,
    availableGenres: List<String>,
    availableYears: List<Int>,
    showGenreMenu: Boolean,
    showYearMenu: Boolean,
    showRatingMenu: Boolean,
    onGenreMenu: (Boolean) -> Unit,
    onYearMenu: (Boolean) -> Unit,
    onRatingMenu: (Boolean) -> Unit,
    onGenre: (String?) -> Unit,
    onYear: (Int?) -> Unit,
    onMinRating: (Float?) -> Unit,
    onClear: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                text = stringResource(R.string.catalog_filters_title),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterMenuChip(
                    label = stringResource(R.string.catalog_filter_genre),
                    value = selectedGenre ?: stringResource(R.string.catalog_filter_all_genres),
                    expanded = showGenreMenu,
                    onExpandedChange = onGenreMenu
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.catalog_filter_all_genres)) },
                        onClick = {
                            onGenreMenu(false)
                            onGenre(null)
                        }
                    )
                    availableGenres.forEach { genre ->
                        DropdownMenuItem(
                            text = { Text(genre) },
                            onClick = {
                                onGenreMenu(false)
                                onGenre(genre)
                            }
                        )
                    }
                }

                FilterMenuChip(
                    label = stringResource(R.string.catalog_filter_year),
                    value = selectedYear?.toString() ?: stringResource(R.string.catalog_filter_all_years),
                    expanded = showYearMenu,
                    onExpandedChange = onYearMenu
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.catalog_filter_all_years)) },
                        onClick = {
                            onYearMenu(false)
                            onYear(null)
                        }
                    )
                    availableYears.forEach { year ->
                        DropdownMenuItem(
                            text = { Text(year.toString()) },
                            onClick = {
                                onYearMenu(false)
                                onYear(year)
                            }
                        )
                    }
                }

                FilterMenuChip(
                    label = stringResource(R.string.catalog_filter_rating),
                    value = minRating?.let(::formatRatingFilter) ?: stringResource(R.string.catalog_filter_any_rating),
                    expanded = showRatingMenu,
                    onExpandedChange = onRatingMenu
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.catalog_filter_any_rating)) },
                        onClick = {
                            onRatingMenu(false)
                            onMinRating(null)
                        }
                    )
                    listOf(60f, 70f, 80f, 90f).forEach { rating ->
                        DropdownMenuItem(
                            text = { Text(formatRatingFilter(rating)) },
                            onClick = {
                                onRatingMenu(false)
                                onMinRating(rating)
                            }
                        )
                    }
                }

                if (hasActiveFilters) {
                    TextButton(onClick = onClear) {
                        Text(text = stringResource(R.string.catalog_filters_clear))
                    }
                }
            }
        }
    }
}

@Composable
private fun FilterMenuChip(
    modifier: Modifier = Modifier,
    label: String,
    value: String,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    content: @Composable () -> Unit
) {
    Box {
        Surface(
            modifier = modifier,
            shape = RoundedCornerShape(999.dp),
            color = MaterialTheme.colorScheme.surface,
            onClick = { onExpandedChange(true) }
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "$label: $value",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { onExpandedChange(false) }
        ) {
            content()
        }
    }
}

@Composable
private fun CatalogGameCard(
    game: VitaCatalogEntry,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
    profileStatus: ProfileGameStatus? = null,
    isFavorite: Boolean = false,
    onProfileStatusChange: (ProfileGameStatus) -> Unit = {},
    onClearProfileStatus: () -> Unit = {},
    onFavoriteChange: (Boolean) -> Unit = {}
) {
    val openClick = rememberDebouncedClick(onClick = onClick)
    var showProfileMenu by remember { mutableStateOf(false) }
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
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
                    imageUrl = game.coverUrl,
                    contentDescription = game.name,
                    fallbackLabel = game.name,
                    modifier = Modifier.fillMaxSize()
                )
                game.compatibility?.let { compatibility ->
                    CompatibilityBadge(
                        compatibility = compatibility,
                        compact = compact,
                        modifier = Modifier
                            .align(Alignment.TopStart)
                            .padding(10.dp)
                    )
                }
                profileStatus?.let { status ->
                    Surface(
                        modifier = Modifier
                            .align(Alignment.BottomStart)
                            .padding(10.dp),
                        shape = RoundedCornerShape(999.dp),
                        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.92f)
                    ) {
                        Text(
                            text = stringResource(status.titleResId()),
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }
                }
                if (isFavorite) {
                    Surface(
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(10.dp),
                        shape = RoundedCornerShape(999.dp),
                        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.92f)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Favorite,
                            contentDescription = stringResource(R.string.profile_status_favorites),
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier
                                .padding(7.dp)
                                .size(15.dp)
                        )
                    }
                }
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(6.dp)
                ) {
                    IconButton(
                        onClick = { showProfileMenu = true },
                        modifier = Modifier.size(36.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.MoreVert,
                            contentDescription = stringResource(R.string.catalog_profile_menu),
                            tint = Color.White
                        )
                    }
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 76.dp else 84.dp)
                    .padding(
                        horizontal = if (compact) CompactCardContentPadding else CardContentPadding,
                        vertical = if (compact) 8.dp else 10.dp
                    ),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = game.name,
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
                if (game.genres.isNotEmpty()) {
                    Text(
                        text = game.genres.take(2).joinToString(" • "),
                        style = MaterialTheme.typography.labelSmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
    if (showProfileMenu) {
        CatalogProfileDialog(
            game = game,
            profileStatus = profileStatus,
            isFavorite = isFavorite,
            onDismiss = { showProfileMenu = false },
            onStatus = { status ->
                showProfileMenu = false
                onProfileStatusChange(status)
            },
            onClearStatus = {
                showProfileMenu = false
                onClearProfileStatus()
            },
            onFavorite = { favorite ->
                onFavoriteChange(favorite)
            }
        )
    }
}

@Composable
private fun CatalogProfileDialog(
    game: VitaCatalogEntry,
    profileStatus: ProfileGameStatus?,
    isFavorite: Boolean,
    onDismiss: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit
) {
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
                shape = RoundedCornerShape(28.dp),
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
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                        ) {
                            UrlImage(
                                imageUrl = game.coverUrl,
                                contentDescription = game.name,
                                fallbackLabel = game.name,
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
                                text = game.name,
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

                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        color = if (isFavorite) {
                            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f)
                        } else {
                            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
                        },
                        border = BorderStroke(
                            1.dp,
                            if (isFavorite) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                        ),
                        onClick = { onFavorite(!isFavorite) }
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = if (isFavorite) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(22.dp)
                            )
                            Spacer(modifier = Modifier.width(12.dp))
                            Text(
                                text = stringResource(R.string.profile_status_favorites),
                                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                                color = MaterialTheme.colorScheme.onSurface,
                                modifier = Modifier.weight(1f)
                            )
                            if (isFavorite) {
                                Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                            }
                        }
                    }

                    Text(
                        text = stringResource(R.string.catalog_profile_status_title),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    ProfileGameStatus.entries.forEach { status ->
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(18.dp),
                            color = if (profileStatus == status) {
                                MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f)
                            } else {
                                MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
                            },
                            border = BorderStroke(
                                1.dp,
                                if (profileStatus == status) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                                else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                            ),
                            onClick = { onStatus(status) }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = stringResource(status.titleResId()),
                                    style = MaterialTheme.typography.titleMedium,
                                    color = MaterialTheme.colorScheme.onSurface,
                                    modifier = Modifier.weight(1f)
                                )
                                if (profileStatus == status) {
                                    Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                                }
                            }
                        }
                    }

                    if (profileStatus != null) {
                        OutlinedButton(
                            onClick = onClearStatus,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.catalog_profile_clear_status))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CatalogMessageCard(
    title: String,
    body: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ScrollToTopButton(
    visible: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    val guardedClick = rememberDebouncedClick(onClick = onClick)
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(tween(180)) + scaleIn(tween(180)),
        exit = fadeOut(tween(140)) + scaleOut(tween(140)),
        modifier = modifier
    ) {
        Surface(
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
            onClick = guardedClick
        ) {
            Box(
                modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.KeyboardArrowUp,
                    contentDescription = stringResource(R.string.common_back),
                    tint = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.size(22.dp)
                )
            }
        }
    }
}

private fun formatRatingFilter(value: Float): String {
    return String.format(Locale.US, "%.1f+", value / 10f)
}

private fun buildMeta(game: VitaCatalogEntry): String {
    val parts = mutableListOf<String>()
    game.year?.let { parts += it.toString() }
    game.rating?.let { parts += String.format(Locale.US, "%.1f", it / 10f) }
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

@Composable
private fun CompatibilityBadge(
    compatibility: VitaCompatibilitySummary,
    compact: Boolean,
    modifier: Modifier = Modifier
) {
    val background = compatibilityContainerColor(compatibility.state)
    val content = compatibilityContentColor(compatibility.state)
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(999.dp),
        color = background,
        shadowElevation = 6.dp
    ) {
        Text(
            text = stringResource(compatibility.state.labelResId()),
            modifier = Modifier.padding(
                horizontal = if (compact) 9.dp else 11.dp,
                vertical = if (compact) 6.dp else 7.dp
            ),
            style = if (compact) MaterialTheme.typography.labelSmall else MaterialTheme.typography.labelMedium,
            color = content,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1
        )
    }
}

@Composable
private fun compatibilityContainerColor(state: VitaCompatibilityState): Color {
    return when (state) {
        VitaCompatibilityState.PLAYABLE -> Color(0xEAF1FAEE)
        VitaCompatibilityState.INGAME_MORE -> Color(0xEAEDF8F6)
        VitaCompatibilityState.INGAME_LESS -> Color(0xEAEAF3FD)
        VitaCompatibilityState.MENU -> Color(0xEAEAF8FB)
        VitaCompatibilityState.INTRO -> Color(0xEAFDF4E3)
        VitaCompatibilityState.BOOTABLE -> Color(0xEAFDF0E2)
        VitaCompatibilityState.NOTHING -> Color(0xEAFCEAEA)
        VitaCompatibilityState.UNKNOWN -> Color(0xEAF2F3F5)
    }
}

@Composable
private fun compatibilityContentColor(state: VitaCompatibilityState): Color {
    return when (state) {
        VitaCompatibilityState.PLAYABLE -> Color(0xFF2E7D32)
        VitaCompatibilityState.INGAME_MORE -> Color(0xFF00897B)
        VitaCompatibilityState.INGAME_LESS -> Color(0xFF1E88E5)
        VitaCompatibilityState.MENU -> Color(0xFF00ACC1)
        VitaCompatibilityState.INTRO -> Color(0xFFF9A825)
        VitaCompatibilityState.BOOTABLE -> Color(0xFFFF8F00)
        VitaCompatibilityState.NOTHING -> Color(0xFFC62828)
        VitaCompatibilityState.UNKNOWN -> MaterialTheme.colorScheme.onSurfaceVariant
    }
}

private fun VitaCompatibilityState.labelResId(): Int {
    return when (this) {
        VitaCompatibilityState.UNKNOWN -> R.string.compatibility_unknown
        VitaCompatibilityState.NOTHING -> R.string.compatibility_nothing
        VitaCompatibilityState.BOOTABLE -> R.string.compatibility_bootable
        VitaCompatibilityState.INTRO -> R.string.compatibility_intro
        VitaCompatibilityState.MENU -> R.string.compatibility_menu
        VitaCompatibilityState.INGAME_LESS -> R.string.compatibility_ingame_less
        VitaCompatibilityState.INGAME_MORE -> R.string.compatibility_ingame_more
        VitaCompatibilityState.PLAYABLE -> R.string.compatibility_playable
    }
}
