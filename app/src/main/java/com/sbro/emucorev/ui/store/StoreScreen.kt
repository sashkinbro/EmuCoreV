package com.sbro.emucorev.ui.store

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import com.sbro.emucorev.ui.hub.SegmentedSwitch
import com.sbro.emucorev.ui.hub.ShelfHeader
import com.sbro.emucorev.ui.hub.enterRise
import com.sbro.emucorev.ui.hub.pressScale
import com.sbro.emucorev.ui.hub.rememberMotionEnabled
import com.sbro.emucorev.ui.hub.expandIntoHorizontalPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material.icons.rounded.WorkspacePremium
import androidx.compose.material.icons.rounded.Redeem
import androidx.compose.material.icons.rounded.Category
import androidx.compose.material.icons.automirrored.rounded.Sort
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudOff
import androidx.compose.material.icons.rounded.FilterList
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SearchOff
import androidx.compose.material.icons.rounded.Storefront
import androidx.compose.material.icons.rounded.TextFields
import androidx.compose.material.icons.rounded.Wallpaper
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.store.InstalledStoreItem
import com.sbro.emucorev.data.store.StoreCategory
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreSnapshot
import com.sbro.emucorev.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.appScreenTopPadding
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.common.tvFocusGroup
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.DropdownMenu
import com.sbro.emucorev.ui.theme.style.FilledTonalButton
import com.sbro.emucorev.ui.theme.style.Surface
import com.sbro.emucorev.ui.theme.style.TextButton
import kotlinx.coroutines.delay

internal val StoreTab.icon: ImageVector
    get() = when (this) {
        StoreTab.THEMES -> Icons.Rounded.Palette
        StoreTab.WALLPAPERS -> Icons.Rounded.Wallpaper
        StoreTab.SOUNDS -> Icons.Rounded.GraphicEq
        StoreTab.CONTROLS -> Icons.Rounded.Gamepad
        StoreTab.FONTS -> Icons.Rounded.TextFields
    }

@Composable
internal fun StoreTab.label(): String = stringResource(
    when (this) {
        StoreTab.THEMES -> R.string.store_tab_themes
        StoreTab.WALLPAPERS -> R.string.store_tab_wallpapers
        StoreTab.SOUNDS -> R.string.store_tab_sounds
        StoreTab.CONTROLS -> R.string.store_tab_controls
        StoreTab.FONTS -> R.string.store_tab_fonts
    }
)

@Composable
private fun StoreSort.label(): String = stringResource(
    when (this) {
        StoreSort.FEATURED -> R.string.store_sort_featured
        StoreSort.NEWEST -> R.string.store_sort_newest
        StoreSort.NAME -> R.string.store_sort_name
    }
)

@Composable
private fun StoreAccessFilter.label(): String = stringResource(
    when (this) {
        StoreAccessFilter.ALL -> R.string.hub_theme_filter_all
        StoreAccessFilter.FREE -> R.string.hub_theme_filter_free
        StoreAccessFilter.PRO -> R.string.hub_theme_filter_pro
        StoreAccessFilter.INSTALLED -> R.string.hub_theme_installed
        StoreAccessFilter.LANGUAGE -> R.string.store_filter_my_language
    }
)

internal const val STORE_SHELF_NEW = "new"
internal const val STORE_SHELF_PRO = "pro"
internal const val STORE_SHELF_FREE = "free"
internal const val STORE_SHELF_CATEGORY_PREFIX = "cat:"

@Stable
class StoreFilters(initialTab: StoreTab) {
    var tab by mutableStateOf(initialTab)
        private set
    var access by mutableStateOf(StoreAccessFilter.ALL)
    var sort by mutableStateOf(StoreSort.FEATURED)
    var categoryId by mutableStateOf<String?>(null)

    /** Key of the shelf opened through "See all"; null on the Store home. */
    var shelf by mutableStateOf<String?>(null)
        private set

    fun selectTab(value: StoreTab) {
        if (value != tab) {
            tab = value
            clear()
        }
    }

    internal fun restore(access: StoreAccessFilter, sort: StoreSort, categoryId: String?, shelf: String?) {
        this.access = access
        this.sort = sort
        this.categoryId = categoryId
        this.shelf = shelf
    }

    /** Opens the full grid of a shelf: its subset becomes the starting filter, adjustable from the control row. */
    fun openShelf(key: String) {
        clear()
        when {
            key == STORE_SHELF_NEW -> sort = StoreSort.NEWEST
            key == STORE_SHELF_PRO -> access = StoreAccessFilter.PRO
            key == STORE_SHELF_FREE -> access = StoreAccessFilter.FREE
            key.startsWith(STORE_SHELF_CATEGORY_PREFIX) -> categoryId = key.removePrefix(STORE_SHELF_CATEGORY_PREFIX)
        }
        shelf = key
    }

    /** Back from a shelf grid to the Store home; false when no shelf was open. */
    fun closeShelf(): Boolean {
        if (shelf == null) return false
        clear()
        return true
    }

    fun clear() {
        access = StoreAccessFilter.ALL
        sort = StoreSort.FEATURED
        categoryId = null
        shelf = null
    }

    val modeKey: String get() = "${tab.name}:${access.name}:${sort.name}:$categoryId:$shelf"
}

@Composable
fun rememberStoreFilters(initialTab: String?): StoreFilters = rememberSaveable(
    initialTab,
    saver = listSaver(
        save = { listOf(it.tab.name, it.access.name, it.sort.name, it.categoryId.orEmpty(), it.shelf.orEmpty()) },
        restore = {
            StoreFilters(StoreTab.valueOf(it[0])).apply {
                restore(
                    StoreAccessFilter.valueOf(it[1]),
                    StoreSort.valueOf(it[2]),
                    it[3].ifEmpty { null },
                    it[4].ifEmpty { null }
                )
            }
        }
    )
) { StoreFilters(StoreTab.fromKey(initialTab)) }

/** One horizontal shelf of the Store home: [key] is a STORE_SHELF_* constant or a category key. */
@Stable
class StoreShelfData internal constructor(val key: String, val items: List<StoreItem>)

@Stable
class StoreContentModel internal constructor(
    val viewModel: StoreViewModel,
    val filters: StoreFilters,
    val query: String,
    val snapshot: StoreSnapshot?,
    val installed: Map<String, InstalledStoreItem>,
    val applied: Set<String>,
    val refreshing: Boolean,
    val offline: Boolean,
    val tabItems: List<StoreItem>,
    val visible: List<StoreItem>,
    val categories: List<StoreCategory>,
    val featured: List<StoreItem>,
    val shelves: List<StoreShelfData>
) {
    /** The full grid replaces the home shelves while searching or inside a "See all" shelf. */
    val showGrid: Boolean get() = query.isNotBlank() || filters.shelf != null
}

@Composable
fun rememberStoreContentModel(
    viewModel: StoreViewModel,
    filters: StoreFilters,
    query: String
): StoreContentModel {
    val snapshot by viewModel.catalog.collectAsState()
    val installed by viewModel.installed.collectAsState()
    val applied by viewModel.appliedIds.collectAsState()
    val refreshing by viewModel.refreshing.collectAsState()
    val offline by viewModel.offline.collectAsState()
    val locale = viewModel.locale
    val tab = filters.tab
    val access = filters.access
    val sort = filters.sort
    val categoryId = filters.categoryId

    val items = snapshot?.catalog?.items.orEmpty()
    val tabItems = remember(items, tab) { items.filter { it.kind == tab.kind } }
    val visible = remember(items, tab, access, categoryId, query, sort, installed, locale) {
        filterStoreItems(items, tab.kind, access, categoryId, query, sort, installed.keys, locale, viewModel.languageScript)
    }
    val categories = remember(snapshot, tab) {
        val used = tabItems.flatMapTo(mutableSetOf()) { it.categories }
        snapshot?.catalog?.categories.orEmpty().filter { it.kind == tab.kind && it.id in used }
    }
    val featured = remember(tabItems) { tabItems.filter { it.featured }.take(6) }
    val shelves = remember(tabItems, categories) {
        val featuredFirst = compareByDescending<StoreItem> { it.featured }
            .thenByDescending { it.publishedAt }
            .thenBy { it.id }
        buildList {
            add(StoreShelfData(STORE_SHELF_NEW, tabItems.sortedWith(compareByDescending<StoreItem> { it.publishedAt }.thenBy { it.id })))
            add(StoreShelfData(STORE_SHELF_PRO, tabItems.filter { it.requiresPro }.sortedWith(featuredFirst)))
            add(StoreShelfData(STORE_SHELF_FREE, tabItems.filter { !it.requiresPro }.sortedWith(featuredFirst)))
            categories.forEach { category ->
                add(
                    StoreShelfData(
                        STORE_SHELF_CATEGORY_PREFIX + category.id,
                        tabItems.filter { category.id in it.categories }.sortedWith(featuredFirst)
                    )
                )
            }
        }.filter { it.items.isNotEmpty() }
    }
    return StoreContentModel(
        viewModel, filters, query, snapshot, installed, applied, refreshing, offline,
        tabItems, visible, categories, featured, shelves
    )
}

private fun storeShelfIcon(key: String): androidx.compose.ui.graphics.vector.ImageVector = when (key) {
    STORE_SHELF_NEW -> Icons.Rounded.AutoAwesome
    STORE_SHELF_PRO -> Icons.Rounded.WorkspacePremium
    STORE_SHELF_FREE -> Icons.Rounded.Redeem
    else -> Icons.Rounded.Category
}

@Composable
private fun storeShelfAccent(key: String): Color? = when (key) {
    STORE_SHELF_PRO -> com.sbro.emucorev.ui.theme.LocalThemeExtras.current.gold
    STORE_SHELF_FREE -> com.sbro.emucorev.ui.theme.LocalThemeExtras.current.success
    STORE_SHELF_NEW -> null
    else -> MaterialTheme.colorScheme.tertiary
}

@Composable
private fun storeShelfTitle(key: String, categories: List<StoreCategory>, locale: String): String = when (key) {
    STORE_SHELF_NEW -> stringResource(R.string.store_shelf_new)
    STORE_SHELF_PRO -> stringResource(R.string.store_shelf_pro_picks)
    STORE_SHELF_FREE -> stringResource(R.string.hub_theme_filter_free)
    else -> categories.firstOrNull { STORE_SHELF_CATEGORY_PREFIX + it.id == key }?.label(locale)
        ?: stringResource(R.string.store_title)
}

/** Name of the opened "See all" shelf for the host header; null on the Store home. */
@Composable
fun StoreContentModel.shelfTitle(): String? =
    filters.shelf?.let { storeShelfTitle(it, categories, viewModel.locale) }

private const val STORE_SHELF_LIMIT = 12

/** Store body (kind switch, offline banner, carousel and shelves, or the shelf grid) emitted into a host grid; no header of its own. */
fun LazyGridScope.storeContent(
    model: StoreContentModel,
    onOpenItem: (String) -> Unit,
    horizontalPadding: androidx.compose.ui.unit.Dp = 0.dp
) {
    val filters = model.filters
    val tab = filters.tab
    val snapshot = model.snapshot
    val locale = model.viewModel.locale
    if (filters.shelf == null) {
        item(key = "store-tabs", span = { GridItemSpan(maxLineSpan) }) {
            StoreKindSwitch(selected = tab, onSelected = filters::selectTab)
        }
    }
    if (model.offline && snapshot != null) {
        item(key = "store-offline", span = { GridItemSpan(maxLineSpan) }) {
            Surface(
                shape = neonShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                contentColor = MaterialTheme.colorScheme.onSurfaceVariant
            ) {
                Text(
                    stringResource(R.string.hub_offline_cached),
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                    style = MaterialTheme.typography.labelMedium
                )
            }
        }
    }
    when {
        snapshot == null && model.refreshing -> {
            items(6, key = { "store-skeleton-$it" }) { StoreSkeletonCard() }
        }
        snapshot == null -> {
            item(key = "store-unavailable", span = { GridItemSpan(maxLineSpan) }) {
                StoreMessageState(
                    icon = Icons.Rounded.CloudOff,
                    title = stringResource(R.string.store_unavailable_title),
                    body = stringResource(R.string.store_unavailable_body)
                ) {
                    FilledTonalButton(onClick = { model.viewModel.refresh(force = true) }, shape = neonButtonShape(), colors = androidx.compose.material3.ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.primaryContainer, contentColor = MaterialTheme.colorScheme.onPrimaryContainer)) {
                        Text(stringResource(R.string.store_retry))
                    }
                }
            }
        }
        model.tabItems.isEmpty() -> {
            item(key = "store-coming-soon", span = { GridItemSpan(maxLineSpan) }) {
                StoreMessageState(
                    icon = tab.icon,
                    title = stringResource(R.string.store_coming_soon_title),
                    body = stringResource(R.string.store_coming_soon_body)
                )
            }
        }
        !model.showGrid -> {
            if (model.featured.isNotEmpty()) {
                item(key = "store-featured-${tab.key}", span = { GridItemSpan(maxLineSpan) }) {
                    Box(Modifier.animateItem().enterRise("store-featured-${tab.key}")) {
                        StoreFeaturedCarousel(
                            items = model.featured,
                            snapshot = snapshot,
                            locale = locale,
                            onOpen = { onOpenItem(it.id) }
                        )
                    }
                }
            }
            val tall = tab == StoreTab.THEMES || tab == StoreTab.WALLPAPERS
            model.shelves.forEachIndexed { shelfIndex, shelf ->
                item(key = "store-shelf-${tab.key}-${shelf.key}-header", span = { GridItemSpan(maxLineSpan) }) {
                    Box(Modifier.animateItem().enterRise("store-shelf-${tab.key}-${shelf.key}-header", shelfIndex + 1)) {
                        ShelfHeader(
                            title = storeShelfTitle(shelf.key, model.categories, locale),
                            onSeeAll = { filters.openShelf(shelf.key) },
                            icon = storeShelfIcon(shelf.key),
                            accent = storeShelfAccent(shelf.key)
                        )
                    }
                }
                item(key = "store-shelf-${tab.key}-${shelf.key}-row", span = { GridItemSpan(maxLineSpan) }) {
                    LazyRow(
                        modifier = Modifier.animateItem().fillMaxWidth().expandIntoHorizontalPadding(horizontalPadding)
                            .enterRise("store-shelf-${tab.key}-${shelf.key}-row", shelfIndex + 1),
                        contentPadding = PaddingValues(horizontal = horizontalPadding),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        items(shelf.items.take(STORE_SHELF_LIMIT), key = { it.id }) { item ->
                            StoreShelfCard(
                                item = item,
                                snapshot = snapshot,
                                locale = locale,
                                onClick = { onOpenItem(item.id) },
                                modifier = Modifier.animateItem().width(if (tall) 130.dp else 200.dp),
                                coverRatio = if (tall) 9f / 16f else 16f / 9f,
                                status = model.viewModel.status(item, model.installed, model.applied)
                            )
                        }
                    }
                }
            }
        }
        else -> {
            item(key = "store-controls", span = { GridItemSpan(maxLineSpan) }) {
                Box(Modifier.animateItem().enterRise("store-controls")) {
                    StoreControlsRow(
                        accessOptions = StoreAccessFilter.entries.filter { it != StoreAccessFilter.LANGUAGE || tab == StoreTab.FONTS },
                        access = filters.access,
                        onAccess = { filters.access = it },
                        sort = filters.sort,
                        onSort = { filters.sort = it }
                    )
                }
            }
            if (model.visible.isEmpty()) {
                item(key = "store-no-results", span = { GridItemSpan(maxLineSpan) }) {
                    StoreMessageState(
                        icon = Icons.Rounded.SearchOff,
                        title = stringResource(R.string.store_no_results),
                        body = ""
                    ) {
                        TextButton(onClick = { filters.clear() }) { Text(stringResource(R.string.store_clear_filters)) }
                    }
                }
            } else {
                items(model.visible, key = { "store-${it.id}" }) { item ->
                    StoreItemCard(
                        item = item,
                        snapshot = snapshot,
                        status = model.viewModel.status(item, model.installed, model.applied),
                        locale = locale,
                        onClick = { onOpenItem(item.id) },
                        modifier = Modifier.animateItem().enterRise("store-${item.id}")
                    )
                }
            }
        }
    }
}

/** One equal segment per Store tab, icon above label, sharing the Hub's sliding-indicator switch. */
@Composable
private fun StoreKindSwitch(selected: StoreTab, onSelected: (StoreTab) -> Unit) {
    SegmentedSwitch(
        count = StoreTab.entries.size,
        selectedIndex = selected.ordinal,
        onSelected = { onSelected(StoreTab.entries[it]) },
        segmentHeight = 56.dp
    ) { index, isSelected ->
        val entry = StoreTab.entries[index]
        Column(
            modifier = Modifier.fillMaxSize().padding(horizontal = 2.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Icon(entry.icon, contentDescription = null, modifier = Modifier.size(20.dp))
            StoreTabLabel(entry.label(), bold = isSelected)
        }
    }
}

/**
 * Single-line tab label that shrinks (down to [MIN_TAB_LABEL_SCALE]) before it would clip, so five tabs fit on
 * a 360 dp phone in every locale; only then does it fall back to an ellipsis.
 */
@Composable
private fun StoreTabLabel(text: String, bold: Boolean) {
    var scale by remember(text) { mutableStateOf(1f) }
    val base = MaterialTheme.typography.labelSmall
    Text(
        text = text,
        modifier = Modifier.padding(top = 2.dp),
        style = base,
        fontSize = base.fontSize * scale,
        fontWeight = if (bold) FontWeight.Bold else FontWeight.Medium,
        maxLines = 1,
        softWrap = false,
        overflow = if (scale <= MIN_TAB_LABEL_SCALE) TextOverflow.Ellipsis else TextOverflow.Clip,
        onTextLayout = { layout ->
            if (layout.didOverflowWidth && scale > MIN_TAB_LABEL_SCALE) {
                scale = (scale - 0.06f).coerceAtLeast(MIN_TAB_LABEL_SCALE)
            }
        }
    )
}

private const val MIN_TAB_LABEL_SCALE = 0.7f

/** The single control row of a shelf grid: a Filter menu and a Sort menu as icon + label text buttons. */
@Composable
private fun StoreControlsRow(
    accessOptions: List<StoreAccessFilter>,
    access: StoreAccessFilter,
    onAccess: (StoreAccessFilter) -> Unit,
    sort: StoreSort,
    onSort: (StoreSort) -> Unit
) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        StoreMenuButton(
            icon = Icons.Rounded.FilterList,
            label = stringResource(R.string.store_filter),
            options = accessOptions,
            selected = access,
            optionLabel = { it.label() },
            onSelect = onAccess
        )
        StoreMenuButton(
            icon = Icons.AutoMirrored.Rounded.Sort,
            label = stringResource(R.string.store_sort),
            options = StoreSort.entries,
            selected = sort,
            optionLabel = { it.label() },
            onSelect = onSort
        )
    }
}

@Composable
private fun <T> StoreMenuButton(
    icon: ImageVector,
    label: String,
    options: List<T>,
    selected: T,
    optionLabel: @Composable (T) -> String,
    onSelect: (T) -> Unit
) {
    var open by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { open = true }) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp))
            Text(label, modifier = Modifier.padding(start = 6.dp), maxLines = 1)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { option ->
                DropdownMenuItem(
                    text = {
                        Text(
                            optionLabel(option),
                            fontWeight = if (option == selected) FontWeight.Bold else FontWeight.Normal
                        )
                    },
                    onClick = {
                        open = false
                        onSelect(option)
                    }
                )
            }
        }
    }
}

@Composable
private fun StoreFeaturedCarousel(
    items: List<StoreItem>,
    snapshot: StoreSnapshot?,
    locale: String,
    onOpen: (StoreItem) -> Unit
) {
    val pagerState: PagerState = rememberPagerState(pageCount = { items.size })
    val motion = rememberMotionEnabled()
    // Auto-advance pauses while a finger is down on the carousel.
    var held by remember { mutableStateOf(false) }
    LaunchedEffect(items.size, motion) {
        if (items.size > 1) {
            while (true) {
                delay(5000)
                if (!held && !pagerState.isScrollInProgress) {
                    val next = (pagerState.currentPage + 1) % items.size
                    if (motion) pagerState.animateScrollToPage(next, animationSpec = tween(650, easing = FastOutSlowInEasing))
                    else pagerState.scrollToPage(next)
                }
            }
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            stringResource(R.string.store_featured),
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onBackground
        )
        Box(
            Modifier.pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Initial)
                        held = event.changes.any { it.pressed }
                    }
                }
            }
        ) {
            HorizontalPager(
                state = pagerState,
                pageSpacing = 10.dp,
                modifier = Modifier.fillMaxWidth().widthIn(max = 1000.dp)
            ) { page ->
                val item = items[page]
                val interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                val shape = neonShape(20.dp)
                val open = rememberDebouncedClick(onClick = { onOpen(item) })
                Surface(
                    onClick = open,
                    interactionSource = interactionSource,
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16f / 8f)
                        .pressScale(interactionSource)
                        .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
                    shape = shape,
                    color = MaterialTheme.colorScheme.surfaceContainerLow
                ) {
                    Box(Modifier.fillMaxSize()) {
                        StoreImage(
                            file = item.cover,
                            snapshot = snapshot,
                            swatch = item.swatch,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop,
                            alignment = Alignment.TopCenter
                        )
                        Box(
                            Modifier
                                .fillMaxSize()
                                .background(
                                    Brush.verticalGradient(
                                        // The cover art carries its own text (theme mock-ups, font samples), so the
                                        // name needs a solid band behind it rather than a light fade.
                                        0.35f to Color.Transparent,
                                        0.62f to Color.Black.copy(alpha = 0.72f),
                                        1f to Color.Black.copy(alpha = 0.92f)
                                    )
                                )
                        )
                        Row(
                            modifier = Modifier
                                .align(Alignment.BottomStart)
                                .padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                item.displayName(locale),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = Color.White,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, fill = false)
                            )
                            if (item.requiresPro) ProBadge()
                        }
                    }
                }
            }
            if (items.size > 1) {
                Row(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(14.dp),
                    horizontalArrangement = Arrangement.spacedBy(5.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    repeat(items.size) { index ->
                        val current = pagerState.currentPage == index
                        val dotWidth by animateDpAsState(
                            targetValue = if (current) 18.dp else 6.dp,
                            animationSpec = if (motion) tween(220) else snap(),
                            label = "dot-width"
                        )
                        val dotColor by animateColorAsState(
                            targetValue = Color.White.copy(alpha = if (current) 0.95f else 0.5f),
                            animationSpec = if (motion) tween(220) else snap(),
                            label = "dot-color"
                        )
                        Box(
                            Modifier
                                .size(width = dotWidth, height = 6.dp)
                                .clip(CircleShape)
                                .background(dotColor)
                        )
                    }
                }
            }
        }
    }
}
