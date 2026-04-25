package com.sbro.emucorev.ui.home

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesomeMotion
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Games
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.GradientEnd
import com.sbro.emucorev.ui.theme.GradientStart
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HomeScreen(
    onOpenSetup: () -> Unit,
    onOpenLibrary: () -> Unit,
    onOpenCatalog: () -> Unit,
    onLaunchGame: (String) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: HomeViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    val folderPickerFailedMessage = stringResource(R.string.folder_picker_failed)

    val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        runCatching {
            viewModel.onPackagesFolderSelected(uri)
        }.onFailure {
            Toast.makeText(context, folderPickerFailedMessage, Toast.LENGTH_SHORT).show()
        }
    }
    val openLibraryClick = rememberDebouncedClick {
        viewModel.refresh()
        onOpenLibrary()
    }
    val openSetupClick = rememberDebouncedClick(onClick = onOpenSetup)
    val openCatalogClick = rememberDebouncedClick {
        viewModel.refresh()
        onOpenCatalog()
    }
    val choosePackagesClick = rememberDebouncedClick { folderPicker.launch(null) }
    val refreshClick = rememberDebouncedClick(onClick = viewModel::refresh)
    val packagesFolderLabel = uiState.packagesFolderLabel

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        if (uiState.isLoading) {
            Box(modifier = Modifier.matchParentSize(), contentAlignment = Alignment.Center) {
                PremiumLoadingAnimation(size = 80.dp)
            }
            return@Box
        }

        LazyColumn(
            modifier = Modifier.matchParentSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset,
                bottom = ScreenContentBottomPadding
            ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (onMenuClick != null) {
                            NavigationMenuButton(
                                onClick = onMenuClick,
                                modifier = Modifier.padding(end = 14.dp)
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.app_name_emucorev),
                                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onBackground
                            )
                        }
                    }
                    IconButton(onClick = refreshClick) {
                        Icon(
                            imageVector = Icons.Rounded.AutoAwesomeMotion,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            }

            item {
                Surface(
                    shape = RoundedCornerShape(32.dp),
                    color = MaterialTheme.colorScheme.surface
                ) {
                    Column(
                        modifier = Modifier
                            .background(
                                Brush.linearGradient(
                                    colors = listOf(
                                        GradientStart.copy(alpha = 0.14f),
                                        GradientEnd.copy(alpha = 0.08f)
                                    )
                                )
                            )
                            .padding(CardContentPadding),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.home_title),
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold)
                        )
                        Text(
                            text = stringResource(R.string.home_subtitle),
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            HomeStatPill(
                                icon = Icons.Rounded.Games,
                                label = stringResource(R.string.home_installed_count, uiState.installedCount)
                            )
                            HomeStatPill(
                                icon = Icons.Rounded.AutoAwesomeMotion,
                                label = stringResource(R.string.home_catalog_count, uiState.catalogCount)
                            )
                            HomeStatPill(
                                icon = Icons.Rounded.Storage,
                                label = stringResource(R.string.home_storage_short)
                            )
                        }
                        Text(
                            text = stringResource(R.string.home_storage_path, uiState.storagePath),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            FilledTonalButton(onClick = openSetupClick) {
                                Icon(Icons.Rounded.Memory, contentDescription = null)
                                Text(stringResource(R.string.home_open_setup))
                            }
                            FilledTonalButton(onClick = openLibraryClick) {
                                Icon(Icons.Rounded.SportsEsports, contentDescription = null)
                                Text(stringResource(R.string.home_open_library))
                            }
                            FilledTonalButton(onClick = openCatalogClick) {
                                Icon(Icons.Rounded.AutoAwesomeMotion, contentDescription = null)
                                Text(stringResource(R.string.home_open_catalog))
                            }
                        }
                    }
                }
            }

            item {
                if (packagesFolderLabel == null) {
                    EmptySetupState(onChoosePackages = choosePackagesClick)
                } else {
                    SectionCard(title = stringResource(R.string.settings_packages_folder)) {
                        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            Text(
                                text = packagesFolderLabel,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(12.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Button(onClick = choosePackagesClick) {
                                    Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                                    Text(stringResource(R.string.home_choose_packages))
                                }
                            }
                        }
                    }
                }
            }

            if (uiState.featuredGames.isNotEmpty()) {
                item {
                    SectionCard(title = stringResource(R.string.home_recent_titles_title)) {
                        LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            items(uiState.featuredGames, key = { it.titleId }) { game ->
                                FeaturedGameCard(
                                    game = game,
                                    onClick = { onLaunchGame(game.titleId) }
                                )
                            }
                        }
                    }
                }
            } else {
                item {
                    SectionCard(title = stringResource(R.string.home_empty_library_title)) {
                        Text(
                            text = stringResource(R.string.home_empty_library_body),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }


        }
    }
}

@Composable
private fun EmptySetupState(
    onChoosePackages: () -> Unit
) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 360.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.widthIn(max = 520.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(100.dp)
                    .background(
                        Brush.linearGradient(
                            colors = listOf(
                                GradientStart.copy(alpha = 0.15f),
                                GradientEnd.copy(alpha = 0.15f)
                            )
                        ),
                        RoundedCornerShape(28.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.SportsEsports,
                    contentDescription = null,
                    modifier = Modifier.size(48.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }

            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.home_empty_library_title),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold)
                )
                Text(
                    text = stringResource(R.string.home_empty_library_body),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }

            FilledTonalButton(onClick = onChoosePackages) {
                Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                Text(stringResource(R.string.home_choose_packages))
            }

            StatusCard(
                icon = Icons.Rounded.CheckCircle,
                title = stringResource(R.string.settings_packages_folder),
                isReady = false
            )
        }
    }
}

@Composable
private fun StatusCard(
    icon: ImageVector,
    title: String,
    isReady: Boolean
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = if (isReady) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
        } else {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.25f)
        },
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(CardContentPadding),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .background(
                        if (isReady) MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                        else MaterialTheme.colorScheme.error.copy(alpha = 0.15f),
                        RoundedCornerShape(10.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = if (isReady) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = if (isReady) stringResource(R.string.onboarding_status_ready)
                    else stringResource(R.string.onboarding_status_pick_folder),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (isReady) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
                )
            }
        }
    }
}

@Composable
private fun HomeStatPill(
    icon: ImageVector,
    label: String
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun FeaturedGameCard(
    game: InstalledVitaGame,
    onClick: () -> Unit
) {
    val guardedClick = rememberDebouncedClick(onClick = onClick)
    Surface(
        modifier = Modifier.width(156.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = guardedClick
    ) {
        Column {
            LocalImage(
                path = game.iconPath,
                contentDescription = game.title,
                fallbackLabel = game.title,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(106.dp)
            )
            Column(
                modifier = Modifier.padding(CardContentPadding),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = game.titleId,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}
