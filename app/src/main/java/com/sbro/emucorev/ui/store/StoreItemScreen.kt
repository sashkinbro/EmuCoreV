package com.sbro.emucorev.ui.store

import android.app.Activity
import android.text.format.Formatter
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.Storefront
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.store.InstalledStoreItem
import com.sbro.emucorev.data.store.StoreFontPacks
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreKind
import com.sbro.emucorev.data.store.StoreSoundPacks
import com.sbro.emucorev.data.store.StoreSnapshot
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.appScreenTopPadding
import com.sbro.emucorev.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.hub.HubImageViewer
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.Button
import com.sbro.emucorev.ui.theme.style.FilledTonalButton
import com.sbro.emucorev.ui.theme.style.OutlinedButton
import com.sbro.emucorev.ui.theme.style.Surface
import com.sbro.emucorev.ui.theme.style.TextButton

@Composable
fun StoreItemScreen(
    itemId: String,
    onBackClick: () -> Unit,
    viewModel: StoreViewModel = viewModel()
) {
    val snapshot by viewModel.catalog.collectAsState()
    val installed by viewModel.installed.collectAsState()
    val applied by viewModel.appliedIds.collectAsState()
    val actions by viewModel.actions.collectAsState()
    val isPro by viewModel.isPro.collectAsState()
    val proPrice by viewModel.proPrice.collectAsState()
    val preview by viewModel.preview.collectAsState()
    val locale = viewModel.locale
    val snackbarHost = remember { SnackbarHostState() }
    var showProSheet by rememberSaveable { mutableStateOf(false) }
    var viewerIndex by rememberSaveable { mutableStateOf<Int?>(null) }

    LaunchedEffect(viewModel) {
        viewModel.messages.collect { message ->
            snackbarHost.currentSnackbarData?.dismiss()
            snackbarHost.showSnackbar(message)
        }
    }
    LaunchedEffect(isPro) { if (isPro) showProSheet = false }

    // A preview never outlives the item page or the app being in the foreground.
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner, itemId, viewModel) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_STOP) viewModel.stopPreview()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            viewModel.stopPreview()
        }
    }

    val item = snapshot?.catalog?.items?.firstOrNull { it.id == itemId }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues())
    ) {
        // Text without an explicit color must read on the screen background, not default to black.
        androidx.compose.runtime.CompositionLocalProvider(androidx.compose.material3.LocalContentColor provides MaterialTheme.colorScheme.onBackground) {
        if (item == null) {
            Column(
                modifier = Modifier.fillMaxSize().padding(top = appScreenTopPadding(), start = 12.dp, end = 12.dp)
            ) {
                ScreenTopBar(
                    title = stringResource(R.string.store_title),
                    onBackClick = onBackClick,
                    modifier = Modifier.padding(top = 12.dp)
                )
                StoreMessageState(
                    icon = Icons.Rounded.ErrorOutline,
                    title = stringResource(R.string.store_item_not_found),
                    body = ""
                )
            }
        } else {
            val installedItem = installed[item.id]?.takeIf { it.kind == item.kind }
            val status = viewModel.status(item, installed, applied)
            val action = actions[item.id]
            val locked = item.requiresPro && !isPro
            val details: @Composable () -> Unit = {
                StoreDetails(
                    item = item,
                    installed = installedItem,
                    status = status,
                    action = action,
                    locked = locked,
                    locale = locale,
                    categoryLabels = snapshot?.catalog?.categories.orEmpty()
                        .filter { it.id in item.categories }
                        .map { it.label(locale) },
                    preview = preview.takeIf { it.itemId == item.id } ?: SoundPreview(),
                    onPreview = { part -> viewModel.togglePreview(item, installedItem, part) },
                    onInstall = { viewModel.install(item) },
                    onApply = { if (locked) showProSheet = true else viewModel.apply(item) },
                    onRemove = { viewModel.remove(item) },
                    onGetPro = { showProSheet = true }
                )
            }
            val gallery: @Composable (Modifier) -> Unit = { modifier ->
                StoreGallery(
                    item = item,
                    snapshot = snapshot,
                    locale = locale,
                    onOpenViewer = { viewerIndex = it },
                    modifier = modifier
                )
            }
            BoxWithConstraints(Modifier.fillMaxSize()) {
                val wide = maxWidth >= 840.dp
                val bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
                if (wide) {
                    Column(Modifier.fillMaxSize().padding(top = appScreenTopPadding(), start = 12.dp, end = 12.dp)) {
                        ScreenTopBar(
                            title = item.displayName(locale),
                            onBackClick = onBackClick,
                            modifier = Modifier.padding(top = 12.dp, bottom = 8.dp)
                        )
                        Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                            Box(Modifier.weight(1f).verticalScroll(rememberScrollState())) {
                                gallery(Modifier.fillMaxWidth())
                            }
                            Box(
                                Modifier
                                    .weight(1f)
                                    .verticalScroll(rememberScrollState())
                                    .padding(bottom = bottom + 24.dp)
                            ) { details() }
                        }
                    }
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(top = appScreenTopPadding(), start = 12.dp, end = 12.dp, bottom = bottom + 24.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        ScreenTopBar(
                            title = item.displayName(locale),
                            onBackClick = onBackClick,
                            modifier = Modifier.padding(top = 12.dp)
                        )
                        gallery(Modifier.fillMaxWidth())
                        details()
                    }
                }
            }

            viewerIndex?.let { index ->
                val snap = snapshot
                if (snap != null) {
                    HubImageViewer(
                        title = item.displayName(locale),
                        images = (listOf(item.cover) + item.gallery).map { it.imageSpec(snap) },
                        startIndex = index,
                        onDismiss = { viewerIndex = null }
                    )
                }
            }
            if (showProSheet) {
                StoreProSheet(
                    price = proPrice,
                    onDismiss = { showProSheet = false },
                    viewModel = viewModel
                )
            }
        }
        SnackbarHost(
            hostState = snackbarHost,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .navigationBarsPadding()
                .padding(12.dp)
        )
        }
    }
}

@Composable
private fun StoreGallery(
    item: StoreItem,
    snapshot: StoreSnapshot?,
    locale: String,
    onOpenViewer: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val images = remember(item) { listOf(item.cover) + item.gallery }
    val pagerState = rememberPagerState(pageCount = { images.size })
    BoxWithConstraints(modifier = modifier) {
        val ratio = item.cover.ratio(default = 9f / 16f, min = 0.4f, max = 2.4f)
        val height = (maxWidth / ratio).coerceIn(200.dp, 440.dp)
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            HorizontalPager(
                state = pagerState,
                pageSpacing = 10.dp,
                modifier = Modifier.fillMaxWidth().height(height)
            ) { page ->
                val file = images[page]
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .clip(neonShape(20.dp))
                        .clickable { onOpenViewer(page) }
                ) {
                    StoreImage(
                        file = file,
                        snapshot = snapshot,
                        swatch = item.swatch,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit,
                        contentDescription = item.displayName(locale)
                    )
                }
            }
            if (images.size > 1) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    repeat(images.size) { index ->
                        val current = pagerState.currentPage == index
                        Box(
                            Modifier
                                .size(if (current) 8.dp else 6.dp)
                                .clip(CircleShape)
                                .background(
                                    if (current) MaterialTheme.colorScheme.primary
                                    else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.25f)
                                )
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun StoreDetails(
    item: StoreItem,
    installed: InstalledStoreItem?,
    status: StoreItemStatus,
    action: StoreAction?,
    locked: Boolean,
    locale: String,
    categoryLabels: List<String>,
    preview: SoundPreview,
    onPreview: (String) -> Unit,
    onInstall: () -> Unit,
    onApply: () -> Unit,
    onRemove: () -> Unit,
    onGetPro: () -> Unit
) {
    val context = LocalContext.current
    val busy = action != null
    Column(
        modifier = Modifier.fillMaxWidth().widthIn(max = 720.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                item.displayName(locale),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.weight(1f, fill = false)
            )
            if (item.requiresPro) ProBadge()
        }
        val meta = buildList {
            add(stringResource(R.string.store_meta_version, item.version))
            if (item.sizeBytes > 0) add(Formatter.formatShortFileSize(context, item.sizeBytes))
        }.joinToString(" · ")
        Text(meta, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f))
        status.label()?.let { label ->
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                if (status == StoreItemStatus.APPLIED) {
                    Icon(Icons.Rounded.CheckCircle, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary)
                }
                Text(
                    label,
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold,
                    color = if (status == StoreItemStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                    else MaterialTheme.colorScheme.primary
                )
            }
        }
        if (categoryLabels.isNotEmpty()) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                categoryLabels.forEach { label ->
                    Surface(
                        shape = neonChipShape(),
                        color = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                    ) {
                        Text(label, modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp), style = MaterialTheme.typography.labelMedium)
                    }
                }
            }
        }
        item.displayDescription(locale).takeIf { it.isNotBlank() }?.let {
            Text(it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f))
        }
        if (item.kind == StoreKind.FONT) {
            val scripts = item.scripts()
            if (scripts.isNotEmpty()) {
                StoreChipGroup(
                    heading = stringResource(R.string.store_font_supports),
                    labels = scripts.filterNot { it == "latin-ext" }.map { scriptLabel(it) }
                )
            }
            if (installed != null) StoreFontSample(installed)
        }
        if (item.kind == StoreKind.SOUNDS) {
            // What is really in the installed pack; before installing, the catalog's contains:* tags.
            val parts = remember(installed, item) {
                installed?.let { StoreSoundPacks.contents(context, it.id) } ?: item.contents().toSet()
            }
            val labels = listOf("music", "click", "menu", "achievement").filter { it in parts }
            if (labels.isNotEmpty()) {
                StoreChipGroup(
                    heading = stringResource(R.string.store_pack_contains),
                    labels = labels.map { soundPartLabel(it) }
                )
                SoundPreviewRow(parts = labels, preview = preview, enabled = !busy, onPreview = onPreview)
            }
        }
        if (locked) {
            val gold = LocalThemeExtras.current.gold
            Surface(
                shape = neonShape(16.dp),
                color = gold.copy(alpha = 0.12f),
                border = BorderStroke(1.dp, gold.copy(alpha = 0.5f))
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Icon(Icons.Rounded.Star, contentDescription = null, tint = gold)
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(
                            stringResource(R.string.store_pro_banner_title),
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            stringResource(R.string.store_pro_banner_body),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    TextButton(onClick = onGetPro, shape = neonButtonShape()) { Text(stringResource(R.string.settings_pro_buy)) }
                }
            }
        }

        val needsNewer = status == StoreItemStatus.NEEDS_NEWER_APP
        val isInstalled = installed != null
        Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
            when {
                needsNewer -> {
                    StoreActionButton(stringResource(R.string.hub_theme_requires_newer_app), enabled = false, primary = true) {}
                    Text(
                        stringResource(R.string.store_requires_newer_body),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }
                !isInstalled -> StoreActionButton(
                    stringResource(R.string.hub_theme_action_install),
                    enabled = !busy, primary = true, onClick = onInstall
                )
                status == StoreItemStatus.UPDATE -> {
                    StoreActionButton(
                        stringResource(R.string.hub_theme_action_update),
                        enabled = !busy, primary = true, onClick = onInstall
                    )
                    StoreActionButton(
                        stringResource(R.string.hub_theme_action_apply),
                        enabled = !busy, primary = false, onClick = onApply
                    )
                }
                status == StoreItemStatus.APPLIED -> StoreActionButton(
                    stringResource(R.string.hub_theme_applied),
                    enabled = false, primary = true
                ) {}
                else -> StoreActionButton(
                    stringResource(R.string.hub_theme_action_apply),
                    enabled = !busy, primary = true, onClick = onApply
                )
            }
            if (action != null) StoreActionProgress(action)
            if (isInstalled) {
                StoreActionButton(
                    stringResource(R.string.hub_theme_action_remove),
                    enabled = !busy, primary = false, onClick = onRemove
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun StoreChipGroup(heading: String, labels: List<String>) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            heading,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f)
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            labels.forEach { label ->
                Surface(
                    shape = neonChipShape(),
                    color = MaterialTheme.colorScheme.secondaryContainer,
                    contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                ) {
                    Text(label, modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp), style = MaterialTheme.typography.labelMedium)
                }
            }
        }
    }
}

/** Play/stop buttons, one per sound part; the active one shows stop (or a spinner while it downloads). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SoundPreviewRow(
    parts: List<String>,
    preview: SoundPreview,
    enabled: Boolean,
    onPreview: (String) -> Unit
) {
    val compactPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 12.dp, vertical = 6.dp)
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            stringResource(R.string.store_preview),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f)
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            parts.forEach { part ->
                val active = preview.part == part
                val label = soundPartLabel(part)
                val stateText = stringResource(if (active) R.string.store_preview_stop else R.string.store_preview_play)
                val content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit = {
                    if (active && preview.loading) {
                        if (preview.progress > 0f) {
                            CircularProgressIndicator(
                                progress = { preview.progress },
                                modifier = Modifier.size(18.dp),
                                strokeWidth = 2.dp
                            )
                        } else {
                            CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                        }
                    } else {
                        Icon(
                            if (active) Icons.Rounded.Stop else Icons.Rounded.PlayArrow,
                            contentDescription = stateText,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    Spacer(Modifier.size(6.dp))
                    Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.labelLarge)
                }
                if (active) {
                    FilledTonalButton(
                        onClick = { onPreview(part) },
                        shape = neonButtonShape(),
                        contentPadding = compactPadding,
                        content = content
                    )
                } else {
                    OutlinedButton(
                        onClick = { onPreview(part) },
                        enabled = enabled,
                        shape = neonButtonShape(),
                        contentPadding = compactPadding,
                        content = content
                    )
                }
            }
        }
    }
}

@Composable
private fun scriptLabel(script: String): String = when (script) {
    "latin" -> stringResource(R.string.store_script_latin)
    "cyrillic" -> stringResource(R.string.store_script_cyrillic)
    "greek" -> stringResource(R.string.store_script_greek)
    "vietnamese" -> stringResource(R.string.store_script_vietnamese)
    "arabic" -> stringResource(R.string.store_script_arabic)
    "cjk" -> stringResource(R.string.store_script_cjk)
    "devanagari" -> stringResource(R.string.store_script_devanagari)
    else -> script.replaceFirstChar { it.uppercase() }
}

@Composable
private fun soundPartLabel(part: String): String = stringResource(
    when (part) {
        "music" -> R.string.store_sound_music
        "click" -> R.string.store_sound_click
        "menu" -> R.string.store_sound_menu
        else -> R.string.store_sound_achievement
    }
)

/** Live text sample rendered with the installed font file. */
@Composable
private fun StoreFontSample(installed: InstalledStoreItem) {
    val context = LocalContext.current
    val family = remember(installed.id, installed.version, installed.directory) {
        StoreFontPacks.file(context, installed.id)?.let { file ->
            runCatching { com.sbro.emucorev.ui.theme.fileFontFamily(file) }.getOrNull()
        }
    } ?: return
    Surface(
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text("Aa Bb Cc 0123456789", style = MaterialTheme.typography.headlineSmall, fontFamily = family)
            Text(stringResource(R.string.store_font_sample), style = MaterialTheme.typography.bodyLarge, fontFamily = family)
        }
    }
}

@Composable
private fun StoreActionButton(label: String, enabled: Boolean, primary: Boolean, onClick: () -> Unit) {
    val debounced = rememberDebouncedClick(onClick = onClick)
    if (primary) {
        Button(onClick = debounced, enabled = enabled, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) {
            Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    } else {
        OutlinedButton(onClick = debounced, enabled = enabled, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) {
            Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
private fun StoreActionProgress(action: StoreAction) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
        val label = when (action.phase) {
            StoreActionPhase.DOWNLOADING -> stringResource(R.string.store_action_downloading, (action.progress * 100).toInt())
            StoreActionPhase.INSTALLING -> stringResource(R.string.store_action_installing)
            StoreActionPhase.APPLYING -> stringResource(R.string.store_action_applying)
            StoreActionPhase.REMOVING -> stringResource(R.string.store_action_removing)
        }
        Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f))
        if (action.phase == StoreActionPhase.DOWNLOADING && action.progress > 0f) {
            LinearProgressIndicator(progress = { action.progress }, modifier = Modifier.fillMaxWidth())
        } else {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun StoreProSheet(price: String?, onDismiss: () -> Unit, viewModel: StoreViewModel) {
    val context = LocalContext.current
    val gold = LocalThemeExtras.current.gold
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        shape = neonShape(28.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(horizontal = 24.dp, vertical = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(Icons.Rounded.Star, contentDescription = null, tint = gold, modifier = Modifier.size(40.dp))
            Text(
                stringResource(R.string.store_pro_sheet_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center
            )
            Text(
                stringResource(R.string.store_pro_sheet_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
            Button(
                onClick = { (context as? Activity)?.let(viewModel::purchasePro) },
                shape = neonButtonShape(),
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    listOfNotNull(stringResource(R.string.settings_pro_buy), price).joinToString(" · "),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            TextButton(onClick = onDismiss, shape = neonButtonShape()) { Text(stringResource(R.string.pro_reminder_later)) }
            Spacer(Modifier.height(8.dp))
        }
    }
}
