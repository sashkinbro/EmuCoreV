package com.sbro.emucorev.ui.hub

import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Newspaper
import androidx.compose.material.icons.rounded.PlayCircle
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.rounded.Bookmark
import androidx.compose.material.icons.automirrored.rounded.MenuBook
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material.icons.rounded.Explore
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Storefront
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.foundation.clickable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.data.hub.HubKind
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreSnapshot
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.common.shimmer
import com.sbro.emucorev.ui.store.StoreShelfCard
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.Surface
import com.sbro.emucorev.ui.theme.style.TextButton

/** Content of the Discover shelves, derived once from the synced items. */
internal class DiscoverFeed(
    val hero: HubItem?,
    val news: List<HubItem>,
    val videos: List<HubItem>,
    val history: List<HubItem>,
    val manuals: List<HubItem>
) {
    val isEmpty: Boolean get() = hero == null && news.isEmpty() && videos.isEmpty() && history.isEmpty() && manuals.isEmpty()
}

private const val SHELF_LIMIT = 12

internal fun buildDiscoverFeed(items: List<HubItem>): DiscoverFeed {
    fun newest(kind: HubKind) = items.filter { it.kind == kind }.sortedByDescending { it.publishedAt }
    val news = newest(HubKind.NEWS)
    val hero = news.filter(HubItem::featured).firstOrNull()
        ?: news.firstOrNull()
        ?: items.maxByOrNull { it.publishedAt }
    return DiscoverFeed(
        hero = hero,
        news = news.filter { it.id != hero?.id }.take(SHELF_LIMIT),
        videos = newest(HubKind.VIDEOS).filter { it.id != hero?.id }.take(SHELF_LIMIT),
        history = newest(HubKind.HISTORY).filter { it.id != hero?.id }.take(SHELF_LIMIT),
        manuals = newest(HubKind.MANUALS).filter { it.id != hero?.id }.take(SHELF_LIMIT)
    )
}

@Composable
private fun HubSection.shelfIcon(): androidx.compose.ui.graphics.vector.ImageVector = when (this) {
    HubSection.NEWS -> androidx.compose.material.icons.Icons.Rounded.Newspaper
    HubSection.VIDEOS -> androidx.compose.material.icons.Icons.Rounded.PlayCircle
    HubSection.HISTORY -> androidx.compose.material.icons.Icons.Rounded.History
    HubSection.MANUALS -> androidx.compose.material.icons.Icons.AutoMirrored.Rounded.MenuBook
    HubSection.SAVED -> androidx.compose.material.icons.Icons.Rounded.Bookmark
}

@Composable
internal fun HubSection.title(): String = stringResource(
    when (this) {
        HubSection.NEWS -> R.string.hub_tab_news
        HubSection.VIDEOS -> R.string.hub_tab_videos
        HubSection.HISTORY -> R.string.hub_tab_history
        HubSection.MANUALS -> R.string.hub_tab_manuals
        HubSection.SAVED -> R.string.hub_tab_favorites
    }
)

/** Full-width pill switch between Discover and Store. */
@Composable
internal fun HubModeSwitch(
    mode: HubMode,
    onSelected: (HubMode) -> Unit,
    selectedFocusRequester: FocusRequester,
    horizontalPadding: Dp
) {
    SegmentedSwitch(
        count = HubMode.entries.size,
        selectedIndex = mode.ordinal,
        onSelected = { onSelected(HubMode.entries[it]) },
        segmentHeight = 40.dp,
        modifier = Modifier.padding(horizontal = horizontalPadding, vertical = 8.dp),
        selectedFocusRequester = selectedFocusRequester
    ) { index, selected ->
        val entry = HubMode.entries[index]
        Row(
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (entry == HubMode.DISCOVER) Icons.Rounded.Explore else Icons.Rounded.Storefront,
                contentDescription = null,
                modifier = Modifier.size(18.dp)
            )
            Text(
                text = stringResource(if (entry == HubMode.DISCOVER) R.string.hub_mode_discover else R.string.store_title),
                modifier = Modifier.padding(start = 8.dp),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

private fun HubItem.thumbSpec(): HubImageSpec? = heroThumbnailUrl?.let {
    HubImageSpec(url = it, sha256 = heroThumbnailSha256, bytes = heroThumbnailBytes, contentDescription = title)
}

/** Hero + shelves of the Discover feed, emitted into the host grid. */
internal fun LazyGridScope.discoverContent(
    feed: DiscoverFeed,
    storeItems: List<StoreItem>,
    storeSnapshot: StoreSnapshot?,
    storeLocale: String,
    compact: Boolean,
    horizontalPadding: Dp,
    onOpenItem: (HubItem) -> Unit,
    onSeeAll: (HubSection) -> Unit,
    onSeeAllStore: () -> Unit,
    onOpenStoreItem: (String) -> Unit
) {
    feed.hero?.let { hero ->
        item(key = "discover-hero", span = { GridItemSpan(maxLineSpan) }) {
            Box(Modifier.animateItem().enterRise("discover-hero")) {
                DiscoverHeroCard(hero, compact, onOpen = { onOpenItem(hero) })
            }
        }
    }
    shelf(HubSection.NEWS, feed.news, compact, horizontalPadding, onOpenItem, onSeeAll)
    shelf(HubSection.VIDEOS, feed.videos, compact, horizontalPadding, onOpenItem, onSeeAll)
    shelf(HubSection.HISTORY, feed.history, compact, horizontalPadding, onOpenItem, onSeeAll)
    shelf(HubSection.MANUALS, feed.manuals, compact, horizontalPadding, onOpenItem, onSeeAll)
    if (storeItems.isNotEmpty()) {
        item(key = "shelf-store-header", span = { GridItemSpan(maxLineSpan) }) {
            Box(Modifier.animateItem().enterRise("shelf-store-header", 4)) {
                ShelfHeader(stringResource(R.string.hub_new_in_store), onSeeAll = onSeeAllStore, icon = androidx.compose.material.icons.Icons.Rounded.Storefront)
            }
        }
        item(key = "shelf-store-row", span = { GridItemSpan(maxLineSpan) }) {
            LazyRow(
                modifier = Modifier.animateItem().fillMaxWidth().expandIntoHorizontalPadding(horizontalPadding).enterRise("shelf-store-row", 4),
                contentPadding = PaddingValues(horizontal = horizontalPadding),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                items(storeItems, key = { it.id }) { item ->
                    StoreShelfCard(
                        item = item,
                        snapshot = storeSnapshot,
                        locale = storeLocale,
                        onClick = { onOpenStoreItem(item.id) },
                        modifier = Modifier.width(140.dp)
                    )
                }
            }
        }
    }
}

private fun LazyGridScope.shelf(
    section: HubSection,
    items: List<HubItem>,
    compact: Boolean,
    horizontalPadding: Dp,
    onOpenItem: (HubItem) -> Unit,
    onSeeAll: (HubSection) -> Unit
) {
    if (items.isEmpty()) return
    item(key = "shelf-${section.name}-header", span = { GridItemSpan(maxLineSpan) }) {
        Box(Modifier.animateItem().enterRise("shelf-${section.name}-header", section.ordinal)) {
            ShelfHeader(section.title(), onSeeAll = { onSeeAll(section) }, icon = section.shelfIcon())
        }
    }
    item(key = "shelf-${section.name}-row", span = { GridItemSpan(maxLineSpan) }) {
        LazyRow(
            modifier = Modifier.animateItem().fillMaxWidth().expandIntoHorizontalPadding(horizontalPadding)
                .enterRise("shelf-${section.name}-row", section.ordinal),
            contentPadding = PaddingValues(horizontal = horizontalPadding),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(items, key = HubItem::id) { item ->
                if (item.kind == HubKind.VIDEOS) {
                    VideoShelfCard(item, onOpen = { onOpenItem(item) })
                } else {
                    ArticleShelfCard(item, onOpen = { onOpenItem(item) })
                }
            }
        }
    }
}

@Composable
internal fun ShelfHeader(
    title: String,
    onSeeAll: () -> Unit,
    icon: androidx.compose.ui.graphics.vector.ImageVector? = null,
    accent: Color? = null
) {
    // Shelf titles carry a tinted icon tile and an accent "See all" pill so sections read as sections,
    // not as loose text between rows of cards.
    val tint = readableAccentOnBackground(accent ?: MaterialTheme.colorScheme.primary)
    val fade = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.12f)
    Column(Modifier.fillMaxWidth()) {
    // Divider: a thin line in the shelf colour fading out, so each section has an edge instead of floating.
    Box(
        Modifier
            .padding(top = 14.dp)
            .fillMaxWidth()
            .height(1.dp)
            .background(
                androidx.compose.ui.graphics.Brush.horizontalGradient(
                    0f to tint.copy(alpha = 0.7f),
                    0.35f to fade,
                    1f to Color.Transparent
                )
            )
    )
    Row(
        modifier = Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (icon != null) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(neonShape(11.dp))
                    .background(tint.copy(alpha = 0.16f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(22.dp))
            }
        }
        Text(
            text = title,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.ExtraBold,
            color = MaterialTheme.colorScheme.onBackground,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
        Row(
            modifier = Modifier
                .clip(neonShape(999.dp))
                .background(tint.copy(alpha = 0.14f))
                .clickable(onClick = onSeeAll)
                .padding(start = 12.dp, end = 6.dp, top = 6.dp, bottom = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = stringResource(R.string.hub_see_all),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = tint
            )
            Icon(
                imageVector = androidx.compose.material.icons.Icons.AutoMirrored.Rounded.KeyboardArrowRight,
                contentDescription = null,
                tint = tint,
                modifier = Modifier.size(18.dp)
            )
        }
    }
    }
}

@Composable
private fun DiscoverHeroCard(item: HubItem, compact: Boolean, onOpen: () -> Unit) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(if (compact) 18.dp else 22.dp)
    val open = rememberDebouncedClick(onClick = onOpen)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .widthIn(max = 720.dp)
            .pressScale(interactionSource)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Box(modifier = Modifier.fillMaxWidth().aspectRatio(if (compact) 16f / 10f else 16f / 8f)) {
            HubImage(spec = item.thumbSpec(), modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            Box(
                Modifier.matchParentSize().background(
                    Brush.verticalGradient(
                        colorStops = arrayOf(
                            0f to Color.Black.copy(alpha = 0.08f),
                            0.45f to Color.Transparent,
                            1f to Color.Black.copy(alpha = 0.78f)
                        )
                    )
                )
            )
            Surface(
                modifier = Modifier.align(Alignment.TopStart).padding(12.dp),
                shape = neonShape(999.dp),
                color = Color.Black.copy(alpha = 0.55f)
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(5.dp)
                ) {
                    if (item.featured) {
                        Icon(Icons.Rounded.AutoAwesome, contentDescription = null, tint = Color.White, modifier = Modifier.size(14.dp))
                    }
                    Text(
                        text = stringResource(
                            when (item.kind) {
                                HubKind.NEWS -> R.string.hub_tab_news
                                HubKind.VIDEOS -> R.string.hub_tab_videos
                                HubKind.HISTORY -> R.string.hub_tab_history
                                HubKind.MANUALS -> R.string.hub_tab_manuals
                            }
                        ),
                        color = Color.White,
                        style = MaterialTheme.typography.labelMedium
                    )
                }
            }
            Column(
                modifier = Modifier.align(Alignment.BottomStart).padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = item.title,
                    style = if (compact) MaterialTheme.typography.titleMedium else MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = item.displayDate(),
                    style = MaterialTheme.typography.labelMedium,
                    color = Color.White.copy(alpha = 0.82f)
                )
            }
        }
    }
}

@Composable
private fun ArticleShelfCard(item: HubItem, onOpen: () -> Unit) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)
    val open = rememberDebouncedClick(onClick = onOpen)
    Surface(
        modifier = Modifier
            .width(200.dp)
            .pressScale(interactionSource)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 1.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            HubImage(
                spec = item.thumbSpec(),
                modifier = Modifier.fillMaxWidth().aspectRatio(16f / 10f),
                contentScale = ContentScale.Crop
            )
            Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = item.title,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    minLines = 2,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = item.displayDate(),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
private fun VideoShelfCard(item: HubItem, onOpen: () -> Unit) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)
    val open = rememberDebouncedClick(onClick = onOpen)
    Surface(
        modifier = Modifier
            .width(240.dp)
            .pressScale(interactionSource)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 1.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            Box(Modifier.fillMaxWidth().aspectRatio(16f / 9f)) {
                HubImage(spec = item.thumbSpec(), modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                Surface(
                    modifier = Modifier.align(Alignment.Center),
                    shape = CircleShape,
                    color = Color.Black.copy(alpha = 0.58f)
                ) {
                    Icon(
                        Icons.Rounded.PlayArrow,
                        contentDescription = stringResource(R.string.hub_watch_video),
                        tint = Color.White,
                        modifier = Modifier.padding(10.dp).size(24.dp)
                    )
                }
            }
            Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = item.title,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    minLines = 2,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = item.channelTitle ?: item.displayDate(),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

/** Loading placeholder for the Discover feed: a hero block and one shelf row. */
internal fun LazyGridScope.discoverSkeleton(compact: Boolean, horizontalPadding: Dp) {
    item(key = "discover-skeleton-hero", span = { GridItemSpan(maxLineSpan) }) {
        Box(
            Modifier
                .fillMaxWidth()
                .widthIn(max = 720.dp)
                .aspectRatio(if (compact) 16f / 10f else 16f / 8f)
                .clip(neonShape(if (compact) 18.dp else 22.dp))
                .shimmer()
        )
    }
    item(key = "discover-skeleton-title", span = { GridItemSpan(maxLineSpan) }) {
        Box(Modifier.padding(top = 14.dp).width(140.dp).height(20.dp).clip(neonShape(8.dp)).shimmer())
    }
    item(key = "discover-skeleton-row", span = { GridItemSpan(maxLineSpan) }) {
        Row(
            modifier = Modifier.fillMaxWidth().expandIntoHorizontalPadding(horizontalPadding).padding(horizontal = horizontalPadding),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            repeat(3) {
                Box(Modifier.width(200.dp).height(150.dp).clip(neonShape(16.dp)).shimmer())
            }
        }
    }
}
