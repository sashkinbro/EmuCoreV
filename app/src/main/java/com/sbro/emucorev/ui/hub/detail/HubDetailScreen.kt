package com.sbro.emucorev.ui.hub.detail

import android.app.Application
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.Bookmark
import androidx.compose.material.icons.rounded.BookmarkBorder
import androidx.compose.material.icons.rounded.CalendarMonth
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.FormatQuote
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.WarningAmber
import com.sbro.emucorev.ui.theme.style.Button
import com.sbro.emucorev.ui.theme.style.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.hub.HubArticle
import com.sbro.emucorev.data.hub.HubAssetEntity
import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.appScreenTopPadding
import com.sbro.emucorev.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorev.ui.common.shimmer
import com.sbro.emucorev.ui.hub.HubImage
import com.sbro.emucorev.ui.hub.HubImageSpec
import com.sbro.emucorev.ui.hub.HubImageViewer
import com.sbro.emucorev.ui.hub.HubScrollToTopButton
import com.sbro.emucorev.ui.hub.HubVideoPlayer
import com.sbro.emucorev.ui.hub.displayDate
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonButtonShape

@Composable
fun HubDetailScreen(
    contentId: String,
    onBackClick: () -> Unit,
    onOpenArticle: (String) -> Unit
) {
    val context = LocalContext.current
    val application = context.applicationContext as Application
    val factory = remember(application, contentId) { HubDetailViewModel.Factory(application, contentId) }
    val viewModel: HubDetailViewModel = viewModel(key = "hub-detail-$contentId", factory = factory)
    val uiState by viewModel.uiState.collectAsState()
    var minimumSkeletonVisible by remember(contentId) { mutableStateOf(true) }
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()

    LaunchedEffect(contentId) {
        delay(MINIMUM_DETAIL_SKELETON_DURATION_MS)
        minimumSkeletonVisible = false
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues())
    ) {
        when {
            minimumSkeletonVisible || uiState.isLoading -> HubDetailStateScreen(
                topInset = topInset,
                bottomInset = bottomInset,
                onBackClick = onBackClick,
                loading = true,
                onRetry = viewModel::retry
            )
            uiState.error != null || uiState.article == null -> HubDetailStateScreen(
                topInset = topInset,
                bottomInset = bottomInset,
                onBackClick = onBackClick,
                loading = false,
                onRetry = viewModel::retry
            )
            else -> HubArticleContent(
                article = uiState.article!!,
                topInset = topInset,
                bottomInset = bottomInset,
                onBackClick = onBackClick,
                onToggleFavorite = viewModel::toggleFavorite,
                isUpdatingFavorite = uiState.isUpdatingFavorite,
                relatedItems = uiState.relatedItems,
                onOpenArticle = onOpenArticle
            )
        }
    }
}

private const val MINIMUM_DETAIL_SKELETON_DURATION_MS = 500L

@Composable
private fun HubArticleContent(
    article: HubArticle,
    topInset: androidx.compose.ui.unit.Dp,
    bottomInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onToggleFavorite: () -> Unit,
    isUpdatingFavorite: Boolean,
    relatedItems: List<HubItem>,
    onOpenArticle: (String) -> Unit
) {
    val uriHandler = LocalUriHandler.current
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val showScrollToTop by remember {
        derivedStateOf {
            listState.firstVisibleItemIndex > 2 || listState.firstVisibleItemScrollOffset > 900
        }
    }
    val imageSpecs = remember(article) {
        article.assets.values.mapNotNull { asset -> asset.toDisplaySpec(article.document.heroAlt) }
    }
    val readingMinutes = remember(article.document.blocks) {
        estimateReadingMinutes(article.document.blocks)
    }
    var viewerIndex by rememberSaveable { mutableIntStateOf(-1) }
    var videoId by rememberSaveable { mutableStateOf<String?>(null) }

    Box(modifier = Modifier.fillMaxSize()) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                top = topInset,
                end = ScreenHorizontalPadding,
                bottom = bottomInset + 24.dp
            ),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
        item(key = "top-bar") {
            ScreenTopBar(
                title = stringResource(R.string.hub_title),
                onBackClick = onBackClick,
                titleMaxLines = 1,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 920.dp)
                    .padding(bottom = 8.dp),
                actions = {
                    IconButton(onClick = onToggleFavorite, enabled = !isUpdatingFavorite) {
                        Icon(
                            if (article.item.isFavorite) Icons.Rounded.Bookmark else Icons.Rounded.BookmarkBorder,
                            contentDescription = stringResource(if (article.item.isFavorite) R.string.hub_remove_saved else R.string.hub_save),
                            tint = if (article.item.isFavorite) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            )
        }
        item(key = "hero") {
            Column(
                modifier = Modifier.widthIn(max = 820.dp).fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                val heroSpec = article.item.heroDisplayUrl?.let {
                    HubImageSpec(
                        url = it,
                        sha256 = article.item.heroDisplaySha256,
                        bytes = article.item.heroDisplayBytes,
                        contentDescription = article.document.heroAlt ?: article.document.title,
                        downloadAllowed = article.item.imageDownloadAllowed,
                        attribution = article.item.heroAttribution,
                        sourceUrl = article.item.heroSourceUrl,
                        licenseUrl = article.item.heroLicenseUrl
                    )
                }
                if (heroSpec != null) {
                    Box(
                        modifier = Modifier
                            .widthIn(max = 760.dp)
                            .fillMaxWidth()
                            .aspectRatio(16f / 9f)
                            .clip(neonShape(26.dp))
                            .background(MaterialTheme.colorScheme.surfaceContainerHigh)
                            .clickable {
                                viewerIndex = imageSpecs.indexOfFirst { it.url == heroSpec.url }.coerceAtLeast(0)
                            }
                    ) {
                        HubImage(
                            spec = heroSpec,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop
                        )
                        Box(
                            modifier = Modifier
                                .matchParentSize()
                                .background(
                                    Brush.verticalGradient(
                                        0f to Color.Transparent,
                                        0.68f to Color.Transparent,
                                        1f to Color.Black.copy(alpha = 0.62f)
                                    )
                                )
                        )
                        article.item.categories
                            .mapNotNull(article.document.categoryLabels::get)
                            .firstOrNull()
                            ?.let { category ->
                                Surface(
                                    modifier = Modifier
                                        .align(Alignment.BottomStart)
                                        .padding(14.dp),
                                    shape = neonShape(999.dp),
                                    color = Color.Black.copy(alpha = 0.62f),
                                    contentColor = Color.White
                                ) {
                                    Text(
                                        text = category,
                                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                                        style = MaterialTheme.typography.labelMedium,
                                        fontWeight = FontWeight.SemiBold
                                    )
                                }
                            }
                    }
                }
                Column(
                    modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text(
                        text = article.document.title,
                        modifier = Modifier.fillMaxWidth(),
                        style = MaterialTheme.typography.headlineMedium,
                        fontWeight = FontWeight.ExtraBold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    HubArticleMetaRow(article.item, readingMinutes)
                    val categoryLabels = article.item.categories
                        .mapNotNull(article.document.categoryLabels::get)
                        .drop(if (heroSpec != null) 1 else 0)
                    if (categoryLabels.isNotEmpty()) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            categoryLabels.forEach { label -> HubTag(label) }
                        }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                }
            }
        }

        itemsIndexed(article.document.blocks, key = { index, _ -> "block-$index" }) { _, block ->
            Box(modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth()) {
                HubArticleBlock(
                    block = block,
                    article = article,
                    onOpenImage = { assetId ->
                        val assetUrl = article.assets[assetId]?.displayUrl
                        viewerIndex = imageSpecs.indexOfFirst { it.url == assetUrl }.coerceAtLeast(0)
                    },
                    onOpenVideo = { videoId = it },
                    onOpenLink = { url -> runCatching { uriHandler.openUri(url) } }
                )
            }
        }

        if (article.item.sources.isNotEmpty()) {
            item(key = "sources-title") {
                Column(
                    modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth().padding(top = 18.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                    Text(
                        text = stringResource(R.string.hub_sources),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 10.dp)
                    )
                }
            }
            items(article.item.sources, key = { it.url }) { source ->
                Surface(
                    modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth().padding(vertical = 4.dp),
                    shape = neonShape(16.dp),
                    color = MaterialTheme.colorScheme.surfaceContainer,
                    onClick = { runCatching { uriHandler.openUri(source.url) } }
                ) {
                    Row(modifier = Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                        Column(modifier = Modifier.weight(1f).padding(horizontal = 12.dp)) {
                            Text(
                                text = source.id?.let(article.document.sourceLabels::get).takeUnless { it.isNullOrBlank() } ?: source.title,
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(source.publisher, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }

        if (relatedItems.isNotEmpty()) {
            item(key = "related-title") {
                Column(
                    modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth().padding(top = 22.dp, bottom = 6.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                    Text(
                        text = stringResource(R.string.hub_related_materials),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
            items(relatedItems, key = { "related-${it.id}" }) { item ->
                HubRelatedMaterialCard(
                    item = item,
                    onClick = { onOpenArticle(item.id) }
                )
            }
        }
        }

        HubScrollToTopButton(
            visible = showScrollToTop,
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 16.dp, bottom = 16.dp + bottomInset),
            onClick = { scope.launch { listState.animateScrollToItem(0) } }
        )
    }

    if (viewerIndex >= 0 && imageSpecs.isNotEmpty()) {
        HubImageViewer(
            title = article.document.title,
            images = imageSpecs,
            startIndex = viewerIndex,
            onDismiss = { viewerIndex = -1 }
        )
    }
    videoId?.let { id ->
        HubVideoPlayer(youtubeId = id, title = article.document.title, onDismiss = { videoId = null })
    }
}

@Composable
private fun HubRelatedMaterialCard(
    item: HubItem,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .widthIn(max = 760.dp)
            .fillMaxWidth()
            .padding(vertical = 5.dp),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        contentColor = MaterialTheme.colorScheme.onSurface,
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            val imageUrl = item.heroThumbnailUrl ?: item.heroDisplayUrl
            if (imageUrl != null) {
                HubImage(
                    spec = HubImageSpec(
                        url = imageUrl,
                        sha256 = item.heroThumbnailSha256 ?: item.heroDisplaySha256,
                        bytes = item.heroThumbnailBytes ?: item.heroDisplayBytes,
                        contentDescription = item.title,
                        downloadAllowed = false
                    ),
                    modifier = Modifier
                        .width(128.dp)
                        .aspectRatio(16f / 9f)
                        .clip(neonShape(13.dp)),
                    contentScale = ContentScale.Crop
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Text(
                    text = item.title,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2
                )
                Text(
                    text = item.summary,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2
                )
            }
        }
    }
}

@Composable
private fun HubArticleBlock(
    block: JsonObject,
    article: HubArticle,
    onOpenImage: (String) -> Unit,
    onOpenVideo: (String) -> Unit,
    onOpenLink: (String) -> Unit
) {
    when (block.type()) {
        "paragraph" -> Text(
            text = block.string("text"),
            style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 28.sp),
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.fillMaxWidth().padding(vertical = 9.dp)
        )
        "heading" -> Text(
            text = block.string("text"),
            style = if (block.int("level") >= 2) MaterialTheme.typography.titleLarge else MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.fillMaxWidth().padding(top = 22.dp, bottom = 7.dp)
        )
        "quote" -> Surface(
            modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp),
            shape = neonShape(18.dp),
            color = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.72f),
            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
        ) {
            Row(modifier = Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Icon(Icons.Rounded.FormatQuote, contentDescription = null, tint = MaterialTheme.colorScheme.secondary)
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(block.string("text"), style = MaterialTheme.typography.bodyLarge, fontStyle = FontStyle.Italic)
                    block.stringOrNull("attribution")?.let {
                        Text(it, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSecondaryContainer)
                    }
                }
            }
        }
        "list" -> Column(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
            block.array("items").forEach { element ->
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("\u2022", style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.primary)
                    Text(element.jsonPrimitive.content, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurface)
                }
            }
        }
        "callout" -> {
            val warning = block.string("style") == "warning"
            Surface(
                modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp),
                shape = neonShape(18.dp),
                color = if (warning) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f),
                contentColor = if (warning) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Row(modifier = Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Icon(if (warning) Icons.Rounded.WarningAmber else Icons.Rounded.Info, contentDescription = null)
                    Text(block.string("text"), style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
        "image" -> {
            val assetId = block.string("assetId")
            val asset = article.assets[assetId]
            if (asset != null) {
                Column(modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(16f / 9f)
                            .clickable { onOpenImage(assetId) },
                        shape = neonShape(20.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        HubImage(
                            spec = asset.toDisplaySpec(block.stringOrNull("alt")),
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit
                        )
                    }
                    (block.stringOrNull("caption") ?: block.stringOrNull("text"))?.let { caption ->
                        Text(caption, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    asset.attribution?.let { attribution ->
                        Text(attribution, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }
        "gallery" -> {
            val ids = block.array("assetIds").map { it.jsonPrimitive.content }
            Column(modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    items(ids, key = { it }) { assetId ->
                        article.assets[assetId]?.let { asset ->
                            Surface(
                                modifier = Modifier
                                    .width(300.dp)
                                    .aspectRatio(16f / 9f)
                                    .clickable { onOpenImage(assetId) },
                                shape = neonShape(20.dp),
                                color = MaterialTheme.colorScheme.surfaceContainerHigh
                            ) {
                                HubImage(
                                    spec = asset.toDisplaySpec(null),
                                    modifier = Modifier.fillMaxSize(),
                                    contentScale = ContentScale.Crop
                                )
                            }
                        }
                    }
                }
                block.stringOrNull("text")?.let { caption ->
                    Text(
                        text = caption,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
        "youtube" -> {
            val providerId = block.string("providerId")
            Surface(
                modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp),
                shape = neonShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceContainer,
                onClick = { onOpenVideo(providerId) }
            ) {
                Row(modifier = Modifier.padding(18.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(34.dp), tint = MaterialTheme.colorScheme.primary)
                    Text(
                        block.stringOrNull("label") ?: stringResource(R.string.hub_watch_video),
                        modifier = Modifier.padding(start = 12.dp),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        }
        "link" -> FilledTonalButton(
            shape = neonButtonShape(),
            onClick = { onOpenLink(block.string("url")) },
            modifier = Modifier.padding(vertical = 8.dp)
        ) {
            Icon(Icons.Rounded.Link, contentDescription = null)
            Text(block.stringOrNull("label") ?: stringResource(R.string.hub_open_source), modifier = Modifier.padding(start = 8.dp))
        }
    }
}

@Composable
private fun HubTag(text: String) {
    Surface(
        shape = neonShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Text(text, modifier = Modifier.padding(horizontal = 11.dp, vertical = 6.dp), style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun HubArticleMetaRow(item: HubItem, readingMinutes: Int) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        HubMetaPill(
            icon = Icons.Rounded.CalendarMonth,
            text = item.displayDate()
        )
        HubMetaPill(
            icon = Icons.Rounded.Schedule,
            text = stringResource(R.string.hub_reading_time, readingMinutes)
        )
        if (item.sources.isNotEmpty()) {
            HubMetaPill(
                icon = Icons.Rounded.Link,
                text = stringResource(R.string.hub_source_count, item.sources.size)
            )
        }
        if (item.featured) {
            HubMetaPill(
                icon = Icons.Rounded.Star,
                text = stringResource(R.string.hub_featured)
            )
        }
    }
}

@Composable
private fun HubMetaPill(icon: androidx.compose.ui.graphics.vector.ImageVector, text: String) {
    Surface(
        shape = neonShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(15.dp))
            Text(text, style = MaterialTheme.typography.labelMedium)
        }
    }
}

private fun estimateReadingMinutes(blocks: List<JsonObject>): Int {
    val words = blocks.sumOf(JsonElement::wordCount)
    return ((words + 219) / 220).coerceAtLeast(1)
}

private fun JsonElement.wordCount(): Int = when (this) {
    is JsonObject -> values.sumOf(JsonElement::wordCount)
    is JsonArray -> sumOf(JsonElement::wordCount)
    is JsonPrimitive -> contentOrNull
        ?.trim()
        ?.split(Regex("\\s+"))
        ?.count(String::isNotBlank)
        ?: 0
}

@Composable
private fun HubDetailStateScreen(
    topInset: androidx.compose.ui.unit.Dp,
    bottomInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    loading: Boolean,
    onRetry: () -> Unit
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(ScreenHorizontalPadding, topInset, ScreenHorizontalPadding, bottomInset + 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item(key = "top-bar") {
            ScreenTopBar(
                title = stringResource(R.string.hub_title),
                onBackClick = onBackClick,
                modifier = Modifier.fillMaxWidth().widthIn(max = 920.dp).padding(bottom = 8.dp)
            )
        }
        item(key = "state") {
            if (loading) {
                HubDetailSkeleton()
            } else {
                HubDetailError(onRetry)
            }
        }
    }
}

@Composable
private fun HubDetailSkeleton() {
    Column(modifier = Modifier.widthIn(max = 760.dp).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Box(Modifier.fillMaxWidth().aspectRatio(16f / 9f).shimmer())
        Box(Modifier.fillMaxWidth(0.86f).height(34.dp).shimmer())
        Box(Modifier.fillMaxWidth().height(68.dp).shimmer())
        repeat(4) { Box(Modifier.fillMaxWidth().height(58.dp).shimmer()) }
    }
}

@Composable
private fun HubDetailError(onRetry: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxWidth().height(360.dp).padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Rounded.ErrorOutline, contentDescription = null, modifier = Modifier.size(52.dp), tint = MaterialTheme.colorScheme.error)
        Text(
            stringResource(R.string.hub_article_load_failed),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(vertical = 14.dp)
        )
        Button(
            shape = neonButtonShape(),onClick = onRetry) { Text(stringResource(R.string.hub_retry)) }
    }
}

private fun HubAssetEntity.toDisplaySpec(alt: String?): HubImageSpec? {
    val url = displayUrl ?: thumbnailUrl ?: return null
    val hash = displaySha256 ?: thumbnailSha256
    val bytes = displayBytes ?: thumbnailBytes
    return HubImageSpec(
        url = url,
        sha256 = hash,
        bytes = bytes,
        contentDescription = alt,
        downloadAllowed = downloadAllowed,
        attribution = attribution,
        sourceUrl = sourceUrl,
        licenseUrl = licenseUrl
    )
}

private fun JsonObject.type(): String = string("type")
private fun JsonObject.string(key: String): String = get(key)?.jsonPrimitive?.contentOrNull.orEmpty()
private fun JsonObject.stringOrNull(key: String): String? = string(key).takeIf(String::isNotBlank)
private fun JsonObject.int(key: String): Int = get(key)?.jsonPrimitive?.intOrNull ?: 0
private fun JsonObject.array(key: String): JsonArray = get(key)?.jsonArray ?: JsonArray(emptyList())
