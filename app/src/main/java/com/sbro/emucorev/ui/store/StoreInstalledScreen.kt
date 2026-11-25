package com.sbro.emucorev.ui.store

import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreKind
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.appScreenTopPadding
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.hub.pressScale
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.Button
import com.sbro.emucorev.ui.theme.style.OutlinedButton
import com.sbro.emucorev.ui.theme.style.Surface

/** One installed Store item; [item] is null once it has left the catalog, so it can be removed but not applied. */
private class InstalledEntry(val id: String, val kind: StoreKind, val item: StoreItem?)

@Composable
fun StoreInstalledScreen(
    onBackClick: () -> Unit,
    onOpenItem: (String) -> Unit,
    viewModel: StoreViewModel = viewModel()
) {
    val snapshot by viewModel.catalog.collectAsState()
    val installed by viewModel.installed.collectAsState()
    val applied by viewModel.appliedIds.collectAsState()
    val actions by viewModel.actions.collectAsState()
    val isPro by viewModel.isPro.collectAsState()
    val proPrice by viewModel.proPrice.collectAsState()
    val locale = viewModel.locale
    val snackbarHost = remember { SnackbarHostState() }
    var showProSheet by rememberSaveable { mutableStateOf(false) }

    LaunchedEffect(viewModel) {
        viewModel.messages.collect { message ->
            snackbarHost.currentSnackbarData?.dismiss()
            snackbarHost.showSnackbar(message)
        }
    }
    LaunchedEffect(isPro) { if (isPro) showProSheet = false }

    val catalogItems = snapshot?.catalog?.items.orEmpty()
    val groups = remember(installed, catalogItems, locale) {
        val byId = catalogItems.associateBy { it.id }
        StoreTab.entries.mapNotNull { tab ->
            val entries = installed.values
                .filter { it.kind == tab.kind }
                .map { InstalledEntry(it.id, it.kind, byId[it.id]?.takeIf { item -> item.kind == it.kind }) }
                .sortedWith(compareBy<InstalledEntry, String>(String.CASE_INSENSITIVE_ORDER) { it.item?.displayName(locale) ?: it.id })
            if (entries.isEmpty()) null else tab to entries
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues())
    ) {
        // Text without an explicit color must read on the screen background, not default to black.
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onBackground) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = appScreenTopPadding(), start = 12.dp, end = 12.dp)
            ) {
                ScreenTopBar(
                    title = stringResource(R.string.store_installed_title),
                    onBackClick = onBackClick,
                    modifier = Modifier.padding(top = 12.dp, bottom = 8.dp)
                )
                if (groups.isEmpty()) {
                    StoreMessageState(
                        icon = Icons.Rounded.Inventory2,
                        title = stringResource(R.string.store_installed_empty_title),
                        body = stringResource(R.string.store_installed_empty_body)
                    )
                } else {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = PaddingValues(bottom = 24.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        groups.forEach { (tab, entries) ->
                            item(key = "header-${tab.key}") {
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    Icon(tab.icon, contentDescription = null, modifier = Modifier.size(20.dp), tint = MaterialTheme.colorScheme.primary)
                                    Text(
                                        tab.label(),
                                        style = MaterialTheme.typography.titleMedium,
                                        fontWeight = FontWeight.Bold
                                    )
                                }
                            }
                            items(entries, key = { it.id }) { entry ->
                                val item = entry.item
                                val action = actions[entry.id]
                                val isApplied = entry.id in applied
                                val status = item?.let { viewModel.status(it, installed, applied) }
                                val locked = item != null && item.requiresPro && !isPro
                                InstalledRow(
                                    entry = entry,
                                    tab = tab,
                                    snapshot = snapshot,
                                    locale = locale,
                                    applied = isApplied,
                                    status = status,
                                    busy = action != null,
                                    progress = action != null,
                                    onOpen = { onOpenItem(entry.id) },
                                    onApply = {
                                        if (item != null) {
                                            if (locked) showProSheet = true else viewModel.apply(item)
                                        }
                                    },
                                    onRemove = { viewModel.remove(entry.id) },
                                    modifier = Modifier.widthIn(max = 720.dp).fillMaxWidth()
                                )
                            }
                        }
                    }
                }
            }
            if (showProSheet) {
                StoreProSheet(
                    price = proPrice,
                    onDismiss = { showProSheet = false },
                    viewModel = viewModel
                )
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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun InstalledRow(
    entry: InstalledEntry,
    tab: StoreTab,
    snapshot: com.sbro.emucorev.data.store.StoreSnapshot?,
    locale: String,
    applied: Boolean,
    status: StoreItemStatus?,
    busy: Boolean,
    progress: Boolean,
    onOpen: () -> Unit,
    onApply: () -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier
) {
    val item = entry.item
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)
    val open = rememberDebouncedClick(onClick = onOpen)
    val apply = rememberDebouncedClick(onClick = onApply)
    val remove = rememberDebouncedClick(onClick = onRemove)
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
        Row(
            modifier = Modifier.fillMaxWidth().padding(10.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            Box(Modifier.size(72.dp).clip(neonShape(12.dp))) {
                if (item != null) {
                    StoreImage(
                        file = item.cover,
                        snapshot = snapshot,
                        swatch = item.swatch,
                        modifier = Modifier.fillMaxSize(),
                        alignment = Alignment.TopCenter
                    )
                } else {
                    Box(
                        modifier = Modifier.fillMaxSize().background(swatchBrush(emptyList())),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(tab.icon, contentDescription = null, modifier = Modifier.size(28.dp), tint = MaterialTheme.colorScheme.primary)
                    }
                }
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(
                        item?.displayName(locale) ?: entry.id,
                        modifier = Modifier.weight(1f, fill = false),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    if (item?.requiresPro == true) ProBadge()
                }
                val label = when {
                    applied -> stringResource(R.string.hub_theme_applied)
                    status == StoreItemStatus.UPDATE || status == StoreItemStatus.NEEDS_NEWER_APP -> status.label()
                    else -> null
                }
                if (label != null) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (applied) {
                            Icon(Icons.Rounded.CheckCircle, contentDescription = null, modifier = Modifier.size(14.dp), tint = MaterialTheme.colorScheme.primary)
                        }
                        Text(
                            label,
                            style = MaterialTheme.typography.labelMedium,
                            color = if (status == StoreItemStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                            else MaterialTheme.colorScheme.primary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                if (progress) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    val compact = PaddingValues(horizontal = 14.dp, vertical = 4.dp)
                    if (item != null) {
                        Button(
                            onClick = apply,
                            enabled = !busy && !applied && status != StoreItemStatus.NEEDS_NEWER_APP,
                            shape = neonButtonShape(),
                            contentPadding = compact
                        ) {
                            Text(
                                stringResource(if (applied) R.string.hub_theme_applied else R.string.hub_theme_action_apply),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                    OutlinedButton(onClick = remove, enabled = !busy, shape = neonButtonShape(), contentPadding = compact) {
                        Text(stringResource(R.string.hub_theme_action_remove), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
    }
}
