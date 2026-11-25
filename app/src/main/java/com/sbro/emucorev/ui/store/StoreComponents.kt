package com.sbro.emucorev.ui.store

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Storefront
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import com.sbro.emucorev.R
import com.sbro.emucorev.data.hub.HubRepository
import com.sbro.emucorev.data.store.StoreFile
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreSnapshot
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.hub.HubImageSpec
import com.sbro.emucorev.ui.hub.pressScale
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.FilledTonalButton
import com.sbro.emucorev.ui.theme.style.Surface

/** Opens the Store on a tab key ("themes", "wallpapers", "sounds", "controls", "fonts"); null where there is no Store. */
val LocalOpenStore = staticCompositionLocalOf<((String?) -> Unit)?> { null }

/** Compact "Get more in Store" button used by the Settings sections; renders nothing without a Store host. */
@Composable
fun StoreEntryButton(tab: String, modifier: Modifier = Modifier) {
    val open = LocalOpenStore.current ?: return
    Row(modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)) {
        FilledTonalButton(
            onClick = { open(tab) },
            shape = neonButtonShape(),
            colors = androidx.compose.material3.ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.primaryContainer, contentColor = MaterialTheme.colorScheme.onPrimaryContainer),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
        ) {
            Icon(Icons.Rounded.Storefront, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(
                stringResource(R.string.store_get_more),
                style = MaterialTheme.typography.labelLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

internal fun parseSwatch(values: List<String>): List<Color> = values.mapNotNull { value ->
    runCatching { Color(android.graphics.Color.parseColor(value)) }.getOrNull()
}

internal fun swatchBrush(colors: List<Color>): Brush = when {
    colors.isEmpty() -> Brush.linearGradient(listOf(Color(0xFF2A2A33), Color(0xFF15151A)))
    colors.size == 1 -> Brush.linearGradient(listOf(colors[0], colors[0].copy(alpha = 0.6f)))
    else -> Brush.linearGradient(colors)
}

internal fun StoreFile.ratio(default: Float, min: Float = 0.5f, max: Float = 2.2f): Float =
    if (width > 0 && height > 0) (width.toFloat() / height).coerceIn(min, max) else default

internal fun StoreFile.imageSpec(snapshot: StoreSnapshot, description: String? = null) = HubImageSpec(
    url = snapshot.url(this),
    sha256 = sha256,
    bytes = bytes,
    contentDescription = description
)

/**
 * Catalog image loaded the verified way (HubRepository.cachedAsset: hash + size checked, disk cached),
 * over a swatch gradient with a soft shimmer until the picture arrives.
 */
@Composable
internal fun StoreImage(
    file: StoreFile,
    snapshot: StoreSnapshot?,
    swatch: List<String>,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop,
    alignment: Alignment = Alignment.Center,
    contentDescription: String? = null
) {
    val colors = remember(swatch) { parseSwatch(swatch) }
    var loaded by remember(file.sha256) { mutableStateOf(false) }
    Box(modifier = modifier.background(swatchBrush(colors))) {
        if (snapshot != null) {
            val context = LocalContext.current
            val repository = remember(context) { HubRepository.get(context) }
            val url = snapshot.url(file)
            val model by produceState<Any?>(initialValue = null, url, file.sha256, file.bytes) {
                value = runCatching { repository.cachedAsset(url, file.sha256, file.bytes) }.getOrElse { url }
            }
            if (model != null) {
                AsyncImage(
                    model = ImageRequest.Builder(context).data(model).crossfade(true).build(),
                    contentDescription = contentDescription,
                    contentScale = contentScale,
                    alignment = alignment,
                    onSuccess = { loaded = true },
                    modifier = Modifier.matchParentSize()
                )
            }
        }
        if (!loaded) {
            val transition = rememberInfiniteTransition(label = "store-shimmer")
            val alpha by transition.animateFloat(
                initialValue = 0.04f,
                targetValue = 0.2f,
                animationSpec = infiniteRepeatable(tween(900), RepeatMode.Reverse),
                label = "store-shimmer-alpha"
            )
            Box(Modifier.matchParentSize().background(Color.White.copy(alpha = alpha)))
        }
    }
}

@Composable
internal fun ProBadge(modifier: Modifier = Modifier) {
    val gold = LocalThemeExtras.current.gold
    Surface(
        modifier = modifier,
        shape = neonShape(8.dp),
        // Opaque: a translucent black turned gray on light cards and the gold text washed out.
        color = Color(0xFF17140F),
        contentColor = gold
    ) {
        Text(
            "PRO",
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
internal fun StoreItemStatus.label(): String? = when (this) {
    StoreItemStatus.NONE -> null
    StoreItemStatus.INSTALLED -> stringResource(R.string.hub_theme_installed)
    StoreItemStatus.APPLIED -> stringResource(R.string.hub_theme_applied)
    StoreItemStatus.UPDATE -> stringResource(R.string.hub_theme_update_available)
    StoreItemStatus.NEEDS_NEWER_APP -> stringResource(R.string.hub_theme_requires_newer_app)
}

@Composable
internal fun StoreItemCard(
    item: StoreItem,
    snapshot: StoreSnapshot?,
    status: StoreItemStatus,
    locale: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    val open = rememberDebouncedClick(onClick = onClick)
    Surface(
        modifier = modifier
            .fillMaxWidth()
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
            Box {
                StoreImage(
                    file = item.cover,
                    snapshot = snapshot,
                    swatch = item.swatch,
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(item.cover.ratio(default = 9f / 16f)),
                    alignment = Alignment.TopCenter
                )
                if (item.requiresPro) ProBadge(Modifier.align(Alignment.BottomEnd).padding(6.dp))
            }
            Column(
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                // PRO sits on the cover's bottom corner (the top collided with preview title bars), so names keep full width.
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(
                        item.displayName(locale),
                        modifier = Modifier.weight(1f, fill = false),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                // Category under the name tells what the item is ("Desktops", "Consoles", "Anime"...).
                val categoryLabel = snapshot?.catalog?.categories
                    ?.firstOrNull { it.kind == item.kind && it.id == item.categories.firstOrNull() }?.label(locale)
                if (status.label() == null && categoryLabel != null) {
                    Text(
                        categoryLabel,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                status.label()?.let { label ->
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (status == StoreItemStatus.APPLIED) {
                            Icon(
                                Icons.Rounded.CheckCircle,
                                contentDescription = null,
                                modifier = Modifier.size(14.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                        Text(
                            label,
                            style = MaterialTheme.typography.labelMedium,
                            color = if (status == StoreItemStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                            else MaterialTheme.colorScheme.primary,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

/** Compact cover + name card for shelves; [coverRatio] is width/height of the cover, [status] adds the installed/applied line. */
@Composable
internal fun StoreShelfCard(
    item: StoreItem,
    snapshot: StoreSnapshot?,
    locale: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    coverRatio: Float = 1f,
    status: StoreItemStatus = StoreItemStatus.NONE
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)
    val open = rememberDebouncedClick(onClick = onClick)
    Surface(
        modifier = modifier
            .pressScale(interactionSource)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 1.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            Box {
                StoreImage(
                    file = item.cover,
                    snapshot = snapshot,
                    swatch = item.swatch,
                    modifier = Modifier.fillMaxWidth().aspectRatio(coverRatio),
                    alignment = Alignment.TopCenter
                )
                if (item.requiresPro) ProBadge(Modifier.align(Alignment.BottomEnd).padding(6.dp))
            }
            Column(
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                // PRO sits on the cover's bottom corner (the top collided with preview title bars), so names keep full width.
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(
                        item.displayName(locale),
                        modifier = Modifier.weight(1f, fill = false),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                // Category under the name tells what the item is ("Desktops", "Consoles", "Anime"...).
                val categoryLabel = snapshot?.catalog?.categories
                    ?.firstOrNull { it.kind == item.kind && it.id == item.categories.firstOrNull() }?.label(locale)
                if (status.label() == null && categoryLabel != null) {
                    Text(
                        categoryLabel,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                status.label()?.let { label ->
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (status == StoreItemStatus.APPLIED) {
                            Icon(
                                Icons.Rounded.CheckCircle,
                                contentDescription = null,
                                modifier = Modifier.size(12.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                        Text(
                            label,
                            style = MaterialTheme.typography.labelSmall,
                            color = if (status == StoreItemStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                            else MaterialTheme.colorScheme.primary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

/** Placeholder card shown while the first catalog sync is running. */
@Composable
internal fun StoreSkeletonCard(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "store-skeleton")
    val alpha by transition.animateFloat(
        initialValue = 0.35f,
        targetValue = 0.8f,
        animationSpec = infiniteRepeatable(tween(800), RepeatMode.Reverse),
        label = "store-skeleton-alpha"
    )
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))
    ) {
        Column {
            Box(
                Modifier
                    .fillMaxWidth()
                    .aspectRatio(0.75f)
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = alpha * 0.6f))
            )
            Box(
                Modifier
                    .padding(10.dp)
                    .size(width = 100.dp, height = 12.dp)
                    .clip(neonShape(6.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = alpha * 0.6f))
            )
        }
    }
}

@Composable
internal fun StoreMessageState(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    body: String,
    modifier: Modifier = Modifier,
    action: @Composable (() -> Unit)? = null
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 36.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(44.dp), tint = MaterialTheme.colorScheme.primary)
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center
            )
            Text(
                body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center
            )
            action?.invoke()
        }
    }
}
