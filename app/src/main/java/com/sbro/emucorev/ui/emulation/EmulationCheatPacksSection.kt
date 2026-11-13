package com.sbro.emucorev.ui.emulation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.cheats.CheatCatalogCard
import com.sbro.emucorev.ui.cheats.CheatCatalogEntry
import com.sbro.emucorev.ui.cheats.CheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.cheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.visibleCheatPacks
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape

/** Compact X-style browser, using Vita IDs/formats and existing V/localized copy. */
@Composable
internal fun EmulationCheatPacksSection(
    state: EmulationCheatPacksState,
    hasInstalledCheats: Boolean,
    installedEntryId: String?,
    refreshing: Boolean,
    onLoad: (Boolean) -> Unit,
    onDownload: (CheatCatalogEntry) -> Unit
) {
    val target = state.operation.target
    var expanded by rememberSaveable(target?.titleId) { mutableStateOf(!hasInstalledCheats) }
    var showOtherVersions by rememberSaveable(target?.titleId) { mutableStateOf(false) }
    val entries = visibleCheatPacks(state.catalog, target?.titleId.orEmpty(), target?.version, showOtherVersions)
    val allForTitle = state.catalog.filter { it.titleId.equals(target?.titleId, true) }
    val hasOtherVersions = allForTitle.any {
        cheatVersionCompatibility(target?.version, it.version) == CheatVersionCompatibility.MISMATCH
    }
    val busy = state.operation.busy || state.identityLoading || refreshing
    val uriHandler = LocalUriHandler.current
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
    ) {
        Column(
            Modifier.animateContentSize().padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.CloudDownload, null, tint = MaterialTheme.colorScheme.primary)
                Text(
                    stringResource(R.string.cheat_catalog_title),
                    modifier = Modifier.weight(1f).padding(start = 10.dp),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                IconButton(onClick = { onLoad(true) }, enabled = !state.loading && !busy) {
                    Icon(Icons.Rounded.Refresh, stringResource(R.string.library_refresh))
                }
                IconButton(onClick = { expanded = !expanded }) {
                    Icon(
                        if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                        stringResource(if (expanded) R.string.detail_show_less else R.string.detail_show_more)
                    )
                }
            }
            if (state.operation.busy) {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                    Text(stringResource(R.string.common_loading), style = MaterialTheme.typography.bodySmall)
                }
            }
            state.operation.feedback?.let { feedback ->
                Text(
                    stringResource(when (feedback) {
                        CheatPackFeedback.INSTALLED -> R.string.cheat_catalog_download_success
                        CheatPackFeedback.FAILED -> R.string.cheat_catalog_download_failed
                        CheatPackFeedback.INCOMPATIBLE -> R.string.cheat_catalog_version_mismatch
                    }),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (feedback == CheatPackFeedback.INSTALLED) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.error
                )
            }
            AnimatedVisibility(expanded, enter = expandVertically() + fadeIn(), exit = shrinkVertically() + fadeOut()) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(target?.titleId.orEmpty(), style = MaterialTheme.typography.labelLarge)
                    Text(
                        stringResource(R.string.cheat_catalog_game_version,
                            target?.version?.takeIf(String::isNotBlank) ?: stringResource(R.string.cheat_catalog_unknown_version)),
                        style = MaterialTheme.typography.bodySmall
                    )
                    Text(stringResource(R.string.cheat_catalog_description), style = MaterialTheme.typography.bodySmall)
                    if (hasOtherVersions) FilterChip(
                        shape = neonChipShape(),
                        selected = showOtherVersions,
                        onClick = { showOtherVersions = !showOtherVersions },
                        label = { Text(stringResource(R.string.cheat_catalog_show_other_versions)) }
                    )
                    when {
                        state.loading || state.identityLoading -> CircularProgressIndicator(Modifier.size(22.dp))
                        entries.isEmpty() -> {
                            Text(stringResource(when {
                                state.failed -> R.string.onboarding_firmware_download_failed
                                allForTitle.isNotEmpty() -> R.string.cheat_catalog_no_version
                                else -> R.string.cheat_catalog_empty
                            }), style = MaterialTheme.typography.bodySmall)
                            TextButton(onClick = { onLoad(true) }, enabled = !busy) {
                                Text(stringResource(R.string.cheat_catalog_retry))
                            }
                        }
                        else -> entries.forEach { entry ->
                            Column {
                                CheatCatalogCard(
                                    entry = entry,
                                    installed = entry.id == installedEntryId,
                                    hasInstalledPack = hasInstalledCheats,
                                    compatibility = cheatVersionCompatibility(target?.version, entry.version),
                                    busy = busy,
                                    onDownload = { onDownload(entry) },
                                    modifier = Modifier
                                )
                                if (entry.sourceUrl.startsWith("https://")) TextButton(
                                    onClick = { runCatching { uriHandler.openUri(entry.sourceUrl) } }
                                ) {
                                    Text(entry.authors.ifBlank { entry.sourceUrl }, maxLines = 1,
                                        overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                                    Icon(Icons.AutoMirrored.Rounded.OpenInNew, null, Modifier.size(16.dp))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
