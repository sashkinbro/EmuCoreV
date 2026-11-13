package com.sbro.emucorev.ui.detail

import com.sbro.emucorev.ui.common.shimmer

import com.sbro.emucorev.ui.common.controllerFocus
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.ContextWrapper
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.provider.MediaStore
import android.util.LruCache
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
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
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import com.sbro.emucorev.ui.theme.neon.neonPillShape
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CalendarToday
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ConfirmationNumber
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Extension
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.VideoLibrary
import com.sbro.emucorev.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.clickable
import androidx.compose.ui.platform.LocalLayoutDirection
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.sbro.emucorev.ui.common.AppDialog as Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.YouTubePlayer
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.listeners.AbstractYouTubePlayerListener
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.options.IFramePlayerOptions
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.views.YouTubePlayerView
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.core.VitaLaunchBridge.LaunchResult
import com.sbro.emucorev.data.EcvCompatibilityUiRun
import com.sbro.emucorev.ui.common.EcvCompatibilitySummaryChip
import com.sbro.emucorev.ui.common.EcvCompatibilityResults
import com.sbro.emucorev.ui.common.GameCoverArt
import com.sbro.emucorev.ui.common.GameCoverAspectRatio
import com.sbro.emucorev.ui.common.ImmersiveMode
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.useMultiColumnLayout
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("SetJavaScriptEnabled", "ConfigurationScreenWidthHeight")
@Composable
fun GameDetailScreen(
    titleId: String?,
    igdbId: Long?,
    onBack: () -> Unit,
    onOpenSaveManager: (String) -> Unit = {},
    onOpenSaveStates: (String) -> Unit = {},
    onInstallDlc: (String) -> Unit = {},
    viewModel: GameDetailViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val launchScope = rememberCoroutineScope()
    val preparingLaunch by VitaLaunchBridge.isPreparingLaunch.collectAsState()
    val configuration = LocalConfiguration.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val backClick = rememberDebouncedClick(onClick = onBack)
    val launchFailedMessage = stringResource(R.string.game_launch_failed)
    val launchRequiresFirmwareMessage = stringResource(R.string.game_launch_requires_firmware)
    val launchRequiresFirmwareUpdateMessage = stringResource(R.string.game_launch_requires_firmware_update)
    val deleteGameLabel = stringResource(R.string.detail_delete_game)
    val installDlcLabel = stringResource(R.string.detail_install_dlc)
    val manageSaveDataLabel = stringResource(R.string.save_manager_open_for_game)
    val manageSaveStatesLabel = stringResource(R.string.savestate_manager_open_for_game)
    val deleteGameConfirmTitle = stringResource(R.string.detail_delete_game_confirm_title)
    val deleteGameConfirmBody = stringResource(R.string.detail_delete_game_confirm_body)
    val deleteGameFailedMessage = stringResource(R.string.detail_delete_game_failed)
    val horizontalInset = ScreenHorizontalPadding
    val useTwoPaneHeader = configuration.useMultiColumnLayout()
    val contentMaxWidth = if (useTwoPaneHeader) 980.dp else 760.dp
    val heroMaxWidth = if (useTwoPaneHeader) 170.dp else 240.dp
    var selectedScreenshotIndex by rememberSaveable { mutableIntStateOf(-1) }
    var selectedVideoIndex by rememberSaveable { mutableIntStateOf(-1) }
    var showOverflowMenu by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }

    LaunchedEffect(titleId, igdbId) {
        viewModel.load(titleId, igdbId)
    }

    val game = uiState.game
    val catalog = uiState.catalogEntry
    var is3dCover by remember(game?.titleId, if (game == null) catalog?.igdbId else null) {
        mutableStateOf(false)
    }
    val compatibility = uiState.compatibility
    val displayTitle = game?.title ?: catalog?.name ?: stringResource(R.string.common_not_available)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
            .verticalScroll(rememberScrollState())
    ) {
        Spacer(modifier = Modifier.height(topInset))
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .widthIn(max = contentMaxWidth)
                .align(Alignment.CenterHorizontally)
                .padding(horizontal = horizontalInset, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            NavigationBackButton(
                onClick = backClick,
                contentColor = MaterialTheme.colorScheme.onSurface
            )
            Box {
                if (game != null) {
                    IconButton(onClick = { showOverflowMenu = true }) {
                        Icon(
                            imageVector = Icons.Rounded.MoreVert,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    DropdownMenu(
                        expanded = showOverflowMenu,
                        onDismissRequest = { showOverflowMenu = false }
                    ) {
                        DropdownMenuItem(
                            text = { Text(manageSaveDataLabel) },
                            onClick = {
                                showOverflowMenu = false
                                onOpenSaveManager(game.titleId)
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Save,
                                    contentDescription = null
                                )
                            }
                        )
                        DropdownMenuItem(
                            text = { Text(manageSaveStatesLabel) },
                            onClick = {
                                showOverflowMenu = false
                                onOpenSaveStates(game.titleId)
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.History,
                                    contentDescription = null
                                )
                            }
                        )
                        DropdownMenuItem(
                            text = { Text(installDlcLabel) },
                            onClick = {
                                showOverflowMenu = false
                                onInstallDlc(game.titleId)
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Extension,
                                    contentDescription = null
                                )
                            }
                        )
                        DropdownMenuItem(
                            text = { Text(deleteGameLabel) },
                            onClick = {
                                showOverflowMenu = false
                                showDeleteDialog = true
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Delete,
                                    contentDescription = null
                                )
                            }
                        )
                    }
                } else {
                    Spacer(modifier = Modifier.size(48.dp))
                }
            }
        }

        when {
            uiState.isLoading -> DetailSkeleton(horizontalInset)
            game == null && catalog == null -> EmptyDetailState(
                isCatalogAvailable = uiState.isCatalogAvailable,
                modifier = Modifier.padding(horizontal = horizontalInset, vertical = 20.dp)
            )
            else -> {
                Column(
                    modifier = Modifier.padding(vertical = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    val description = catalog?.summary?.takeIf { it.isNotBlank() }
                    val launchGameClick = game?.let {
                        rememberDebouncedClick {
                            launchScope.launch {
                                when (VitaLaunchBridge.launchInstalledTitle(context, it.titleId)) {
                                    LaunchResult.Success -> Unit
                                    LaunchResult.MissingFirmware -> Toast.makeText(context, launchRequiresFirmwareMessage, Toast.LENGTH_SHORT).show()
                                    LaunchResult.MissingFirmwareUpdate -> Toast.makeText(context, launchRequiresFirmwareUpdateMessage, Toast.LENGTH_SHORT).show()
                                    LaunchResult.Failure -> Toast.makeText(context, launchFailedMessage, Toast.LENGTH_SHORT).show()
                                }
                            }
                        }
                    }

                    if (useTwoPaneHeader) {
                        Row(
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset),
                            horizontalArrangement = Arrangement.spacedBy(18.dp),
                            verticalAlignment = Alignment.Top
                        ) {
                            Box(
                                modifier = Modifier
                                    .padding(top = 10.dp, bottom = 10.dp, start = 8.dp)
                                    .width(heroMaxWidth)
                                    .aspectRatio(GameCoverAspectRatio)
                                    .background(if (is3dCover) Color.Transparent else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                            ) {
                                GameCoverArt(
                                    titleId = game?.titleId,
                                    igdbId = if (game == null) catalog?.igdbId else null,
                                    fallbackIconPath = game?.iconPath,
                                    fallbackTitle = displayTitle,
                                    onPresentationChanged = { is3dCover = it },
                                    modifier = Modifier.fillMaxSize()
                                )
                            }
                            Column(
                                modifier = Modifier.fillMaxWidth(),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Text(
                                    text = displayTitle,
                                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.onBackground
                                )
                                MetaRow(
                                    year = catalog?.year,
                                    rating = catalog?.rating?.toDouble(),
                                    serial = game?.titleId,
                                    compatibility = compatibility
                                )
                                if (catalog?.genres?.isNotEmpty() == true) {
                                    FlowRow(
                                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                                        verticalArrangement = Arrangement.spacedBy(8.dp)
                                    ) {
                                        catalog.genres.forEach { GenreChip(it) }
                                    }
                                }
                                if (!description.isNullOrBlank()) {
                                    ExpandableInfoSection(
                                        label = stringResource(R.string.detail_overview_title),
                                        value = description
                                    )
                                    DetailSourceCard(text = stringResource(R.string.detail_igdb_source_note))
                                }
                                if (launchGameClick != null) {
                                    Button(
                                        shape = neonButtonShape(),
                                        onClick = launchGameClick,
                                        enabled = !preparingLaunch,
                                        modifier = Modifier.widthIn(min = 240.dp, max = 320.dp)
                                    ) {
                                        Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                                        Text(
                                            text = stringResource(if (preparingLaunch) R.string.common_loading else R.string.detail_launch),
                                            modifier = Modifier.padding(start = 6.dp)
                                        )
                                    }
                                }
                            }
                        }
                    } else {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = horizontalInset),
                            contentAlignment = Alignment.Center
                        ) {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .widthIn(max = heroMaxWidth)
                                    .aspectRatio(GameCoverAspectRatio)
                                    .background(if (is3dCover) Color.Transparent else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                            ) {
                                GameCoverArt(
                                    titleId = game?.titleId,
                                    igdbId = if (game == null) catalog?.igdbId else null,
                                    fallbackIconPath = game?.iconPath,
                                    fallbackTitle = displayTitle,
                                    onPresentationChanged = { is3dCover = it },
                                    modifier = Modifier.fillMaxSize()
                                )
                            }
                        }

                        Text(
                            text = displayTitle,
                            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onBackground,
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        )

                        Box(
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            MetaRow(
                                year = catalog?.year,
                                rating = catalog?.rating?.toDouble(),
                                serial = game?.titleId,
                                compatibility = compatibility
                            )
                        }

                        if (catalog?.genres?.isNotEmpty() == true) {
                            Box(
                                modifier = Modifier
                                    .align(Alignment.CenterHorizontally)
                                    .widthIn(max = contentMaxWidth)
                                    .padding(horizontal = horizontalInset)
                            ) {
                                FlowRow(
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                    verticalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    catalog.genres.forEach { GenreChip(it) }
                                }
                            }
                        }

                        if (!description.isNullOrBlank()) {
                            Box(
                                modifier = Modifier
                                    .align(Alignment.CenterHorizontally)
                                    .widthIn(max = contentMaxWidth)
                                    .padding(horizontal = horizontalInset)
                            ) {
                                ExpandableInfoSection(
                                    label = stringResource(R.string.detail_overview_title),
                                    value = description
                                )
                            }
                            Box(
                                modifier = Modifier
                                    .align(Alignment.CenterHorizontally)
                                    .widthIn(max = contentMaxWidth)
                                    .padding(horizontal = horizontalInset)
                            ) {
                                DetailSourceCard(text = stringResource(R.string.detail_igdb_source_note))
                            }
                        }

                        if (launchGameClick != null) {
                            Box(
                                modifier = Modifier
                                    .align(Alignment.CenterHorizontally)
                                    .widthIn(max = contentMaxWidth)
                                    .padding(horizontal = horizontalInset)
                            ) {
                                Button(
                                    shape = neonButtonShape(),
                                    onClick = launchGameClick,
                                    enabled = !preparingLaunch,
                                    modifier = Modifier.fillMaxWidth()
                                ) {
                                    Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                                    Text(
                                        text = stringResource(if (preparingLaunch) R.string.common_loading else R.string.detail_launch),
                                        modifier = Modifier.padding(start = 6.dp)
                                    )
                                }
                            }
                        }
                    }

                    if (game != null) {
                        SectionCard(
                            title = stringResource(R.string.detail_installed_section),
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            MetadataRow(stringResource(R.string.detail_title_id), game.titleId)
                            MetadataRow(stringResource(R.string.detail_content_id), game.contentId ?: stringResource(R.string.common_unknown))
                            MetadataRow(stringResource(R.string.detail_version), game.version ?: stringResource(R.string.common_unknown))
                            MetadataRow(stringResource(R.string.detail_category), game.category ?: stringResource(R.string.common_unknown))
                        }
                    }

                    SectionCard(
                        title = stringResource(R.string.ecv_compatibility_title),
                        modifier = Modifier
                            .align(Alignment.CenterHorizontally)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        EcvCompatibilityResults(compatibility)
                    }

                    if (catalog != null) {
                        SectionCard(
                            title = stringResource(R.string.detail_catalog_meta_title),
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            MetadataRow(stringResource(R.string.detail_release_year), catalog.year?.toString() ?: stringResource(R.string.common_unknown))
                            MetadataRow(
                                stringResource(R.string.detail_rating),
                                catalog.rating?.let { String.format(Locale.US, "%.1f", it / 10f) } ?: stringResource(R.string.common_unknown)
                            )
                            MetadataRow(
                                stringResource(R.string.detail_genres),
                                catalog.genres.takeIf { it.isNotEmpty() }?.joinToString(", ") ?: stringResource(R.string.common_unknown)
                            )
                        }
                    }

                    if (!catalog?.screenshots.isNullOrEmpty()) {
                        Box(
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            MediaSectionTitle(Icons.Rounded.VideoLibrary, stringResource(R.string.detail_screenshots_title))
                        }
                        LazyRow(
                            contentPadding = PaddingValues(horizontal = horizontalInset),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            itemsIndexed(catalog.screenshots, key = { index, item -> "$index-$item" }) { index, screenshot ->
                                ScreenshotCard(imageUrl = screenshot, title = displayTitle, onClick = { selectedScreenshotIndex = index })
                            }
                        }
                    }

                    if (!catalog?.videos.isNullOrEmpty()) {
                        Box(
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            MediaSectionTitle(Icons.Rounded.SmartDisplay, stringResource(R.string.detail_videos_title))
                        }
                        LazyRow(
                            contentPadding = PaddingValues(horizontal = horizontalInset),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            itemsIndexed(catalog.videos, key = { index, item -> "$index-$item" }) { index, video ->
                                VideoCard(youtubeId = video, title = displayTitle, onClick = { selectedVideoIndex = index })
                            }
                        }
                    }
                }
            }
        }
    }

    if (catalog != null && selectedScreenshotIndex >= 0) {
        ScreenshotViewerOverlay(
            title = displayTitle,
            screenshots = catalog.screenshots,
            startIndex = selectedScreenshotIndex,
            onDismiss = { selectedScreenshotIndex = -1 }
        )
    }

    if (showDeleteDialog && game != null) {
        AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text(deleteGameConfirmTitle) },
            text = { Text(deleteGameConfirmBody) },
            confirmButton = {
                TextButton(
                    onClick = {
                        showDeleteDialog = false
                        viewModel.deleteInstalledGame(game.titleId) { deleted ->
                            if (deleted) {
                                onBack()
                            } else {
                                Toast.makeText(context, deleteGameFailedMessage, Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                ) {
                    Text(deleteGameLabel)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteDialog = false }) {
                    Text(stringResource(R.string.install_dialog_close))
                }
            }
        )
    }

    if (catalog != null && selectedVideoIndex >= 0) {
        VideoPlayerOverlay(
            title = displayTitle,
            videoIds = catalog.videos,
            startIndex = selectedVideoIndex,
            onDismiss = { selectedVideoIndex = -1 }
        )
    }
}

@Composable
private fun DetailSkeleton(horizontalInset: Dp) {
    Column(
        modifier = Modifier.padding(vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .aspectRatio(GameCoverAspectRatio)
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .fillMaxWidth(0.72f)
                .height(34.dp)
                .clip(neonShape(14.dp))
        )
        Row(
            modifier = Modifier.padding(horizontal = horizontalInset),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            repeat(3) {
                SkeletonBlock(
                    modifier = Modifier
                        .height(36.dp)
                        .width(if (it == 2) 132.dp else 72.dp)
                        .clip(neonPillShape())
                )
            }
        }
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(58.dp)
                .clip(neonShape(18.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(144.dp)
                .clip(neonShape(16.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .width(156.dp)
                .height(24.dp)
                .clip(neonShape(10.dp))
        )
        LazyRow(
            contentPadding = PaddingValues(horizontal = horizontalInset),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(3) {
                SkeletonBlock(
                    modifier = Modifier
                        .width(220.dp)
                        .height(130.dp)
                        .clip(neonShape(18.dp))
                )
            }
        }
    }
}

@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.shimmer()
    )
}

@Composable
private fun EmptyDetailState(
    isCatalogAvailable: Boolean,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.detail_empty_title),
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = stringResource(
                    if (isCatalogAvailable) R.string.detail_empty_body
                    else R.string.detail_catalog_missing_body
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun MetaRow(
    year: Int?,
    rating: Double?,
    serial: String?,
    compatibility: List<EcvCompatibilityUiRun>
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        year?.let { MetaChip(Icons.Rounded.CalendarToday, it.toString()) }
        rating?.let { MetaChip(Icons.Rounded.Star, String.format(Locale.US, "%.1f", it / 10.0)) }
        serial?.takeIf { it.isNotBlank() }?.let { MetaChip(Icons.Rounded.ConfirmationNumber, it) }
        EcvCompatibilitySummaryChip(compatibility)
    }
}

@Composable
private fun MetaChip(
    icon: ImageVector,
    text: String
) {
    Surface(
        shape = neonPillShape(),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(16.dp)
            )
            Text(
                text = text,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun GenreChip(text: String) {
    Surface(
        shape = neonPillShape(),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 8.dp),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun ExpandableInfoSection(
    label: String,
    value: String,
    collapsedMaxChars: Int = 200
) {
    var expanded by remember(value) { mutableStateOf(false) }
    val trimmedValue = value.trim()
    val canExpand = trimmedValue.length > collapsedMaxChars
    val displayValue = when {
        expanded || !canExpand -> trimmedValue
        else -> trimmedValue.take(collapsedMaxChars).trimEnd() + "..."
    }
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (canExpand) {
                    Modifier
                        .gamepadFocusableCard(
                            shape = shape,
                            interactionSource = interactionSource,
                            addFocusTarget = false
                        )
                        .clickable(
                            interactionSource = interactionSource,
                            indication = null,
                            onClick = { expanded = !expanded }
                        )
                } else {
                    Modifier
                }
            )
            .animateContentSize(
                animationSpec = spring(
                    dampingRatio = Spring.DampingRatioNoBouncy,
                    stiffness = Spring.StiffnessLow
                )
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = displayValue,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
            if (canExpand) {
                Text(
                    text = stringResource(
                        if (expanded) R.string.detail_show_less
                        else R.string.detail_show_more
                    ),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun DetailSourceCard(
    text: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = CardContentPadding, vertical = CompactCardContentPadding)
        )
    }
}

@Composable
private fun MediaSectionTitle(
    icon: ImageVector,
    title: String
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(18.dp)
        )
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}

@Composable
private fun ScreenshotCard(
    imageUrl: String,
    title: String,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    Surface(
        modifier = Modifier
            .width(220.dp)
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        interactionSource = interactionSource,
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(130.dp)
                .clip(neonShape(18.dp))
        ) {
            RemoteBitmapImage(
                imageUrl = imageUrl,
                contentDescription = title,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
        }
    }
}

@Composable
private fun VideoCard(
    youtubeId: String,
    title: String,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    Surface(
        modifier = Modifier
            .width(232.dp)
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        interactionSource = interactionSource,
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(16f / 9f)
                .clip(neonShape(18.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
        ) {
            RemoteBitmapImage(
                imageUrl = youtubeThumbnailUrl(youtubeId),
                contentDescription = title,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .background(
                        Brush.verticalGradient(
                            colors = listOf(
                                Color.Transparent,
                                Color.Transparent,
                                Color.Black.copy(alpha = 0.22f)
                            )
                        )
                    )
            )
            Box(
                modifier = Modifier
                    .align(Alignment.Center)
                    .clip(CircleShape)
                    .background(Color.Black.copy(alpha = 0.55f))
                    .padding(14.dp)
            ) {
                Icon(
                    imageVector = Icons.Rounded.PlayArrow,
                    contentDescription = null,
                    tint = Color.White,
                    modifier = Modifier.size(24.dp)
                )
            }
            Surface(
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(10.dp),
                shape = neonPillShape(),
                color = Color.Black.copy(alpha = 0.48f)
            ) {
                Text(
                    text = stringResource(R.string.detail_videos_title),
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                    style = MaterialTheme.typography.labelMedium,
                    color = Color.White
                )
            }
        }
    }
}

@Composable
private fun MetadataRow(label: String, value: String) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurface
        )
        Spacer(modifier = Modifier.height(10.dp))
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ScreenshotViewerOverlay(
    title: String,
    screenshots: List<String>,
    startIndex: Int,
    onDismiss: () -> Unit
) {
    if (screenshots.isEmpty()) return
    val start = startIndex.coerceIn(0, screenshots.lastIndex)
    val pagerState = rememberPagerState(initialPage = start, pageCount = { screenshots.size })
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val scope = rememberCoroutineScope()
    val screenshotSavedMessage = stringResource(R.string.detail_screenshot_saved)
    val screenshotSaveFailedMessage = stringResource(R.string.detail_screenshot_save_failed)

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)
    ) {
        val dialogView = LocalView.current
        val dialogWindow = (dialogView.parent as? DialogWindowProvider)?.window

        DisposableEffect(dialogWindow) {
            if (dialogWindow != null) {
                WindowCompat.setDecorFitsSystemWindows(dialogWindow, false)
                dialogWindow.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                val controller = WindowInsetsControllerCompat(dialogWindow, dialogWindow.decorView)
                controller.hide(WindowInsetsCompat.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
            onDispose { }
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    RemoteBitmapImage(
                        imageUrl = screenshots[page],
                        contentDescription = title,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit
                    )
                }
            }

            if (isLandscape) {
                LandscapeViewerTopBar(
                    title = title,
                    counter = "${pagerState.currentPage + 1}/${screenshots.size}",
                    onSave = {
                        scope.launch {
                            val saved = saveScreenshotToGallery(context, screenshots[pagerState.currentPage], title)
                            Toast.makeText(
                                context,
                                if (saved) screenshotSavedMessage else screenshotSaveFailedMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                    },
                    onDismiss = onDismiss
                )
            } else {
                ViewerTopBar(
                    title = title,
                    counter = "${pagerState.currentPage + 1}/${screenshots.size}",
                    onSave = {
                        scope.launch {
                            val saved = saveScreenshotToGallery(context, screenshots[pagerState.currentPage], title)
                            Toast.makeText(
                                context,
                                if (saved) screenshotSavedMessage else screenshotSaveFailedMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                    },
                    onDismiss = onDismiss,
                    modifier = Modifier.windowInsetsPadding(WindowInsets.statusBarsIgnoringVisibility)
                )
            }
        }
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun VideoPlayerOverlay(
    title: String,
    videoIds: List<String>,
    startIndex: Int,
    onDismiss: () -> Unit
) {
    if (videoIds.isEmpty()) return
    val normalizedIds = remember(videoIds) { videoIds.map(::normalizeYoutubeId) }
    val start = startIndex.coerceIn(0, normalizedIds.lastIndex)
    val pagerState = rememberPagerState(initialPage = start, pageCount = { normalizedIds.size })
    val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    val configuration = LocalConfiguration.current
    val context = LocalContext.current
    val activity = remember(context) { context.findActivity() }
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp

    DisposableEffect(activity) {
        val window = activity?.window
        val controller = window?.let { WindowInsetsControllerCompat(it, it.decorView) }
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        controller?.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        onDispose {
            window?.let(ImmersiveMode::apply)
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        val dialogView = LocalView.current
        val dialogWindow = (dialogView.parent as? DialogWindowProvider)?.window

        DisposableEffect(dialogWindow) {
            if (dialogWindow != null) {
                WindowCompat.setDecorFitsSystemWindows(dialogWindow, false)
                dialogWindow.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                val controller = WindowInsetsControllerCompat(dialogWindow, dialogWindow.decorView)
                controller.hide(WindowInsetsCompat.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
            onDispose { }
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                BoxWithConstraints(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    val videoRatio = 16f / 9f
                    val screenRatio = maxWidth / maxHeight

                    val (videoWidth, videoHeight) = if (screenRatio > videoRatio) {
                        // Screen is wider than 16:9 (landscape) -> height is limiting factor
                        (maxHeight * videoRatio) to maxHeight
                    } else {
                        // Screen is narrower than 16:9 (portrait) -> width is limiting factor
                        maxWidth to (maxWidth / videoRatio)
                    }

                    Box(
                        modifier = Modifier
                            .size(width = videoWidth, height = videoHeight)
                            .background(Color.Black)
                    ) {
                        YouTubePlayerBlock(
                            youtubeId = normalizedIds[page],
                            lifecycleOwner = lifecycleOwner,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
            }

            if (isLandscape) {
                LandscapeVideoTopBar(
                    counter = "${pagerState.currentPage + 1}/${normalizedIds.size}",
                    onDismiss = onDismiss
                )
            } else {
                ViewerTopBar(
                    title = title,
                    counter = "${pagerState.currentPage + 1}/${normalizedIds.size}",
                    onSave = null,
                    onDismiss = onDismiss,
                    modifier = Modifier
                        .windowInsetsPadding(WindowInsets.statusBarsIgnoringVisibility)
                        .padding(horizontal = 16.dp)
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ViewerTopBar(
    title: String,
    counter: String,
    onSave: (() -> Unit)? = null,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
    respectStatusBar: Boolean = true,
    fillWidthBackground: Boolean = true,
    closeAtEnd: Boolean = false,
    extraTopPadding: Dp = 0.dp
) {
    val layoutDirection = LocalLayoutDirection.current
    val statusInsets = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues()
    val topSafePadding = if (respectStatusBar) {
        WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    } else {
        0.dp
    }
    val startSafePadding = statusInsets.calculateLeftPadding(layoutDirection)
    val endSafePadding = statusInsets.calculateRightPadding(layoutDirection)
    val horizontalPadding = if (compact) 12.dp else 8.dp
    val topPadding = topSafePadding + extraTopPadding + if (compact) 8.dp else 12.dp
    val bottomPadding = if (compact) 10.dp else 16.dp
    val rowModifier = if (fillWidthBackground) {
        Modifier
            .fillMaxWidth()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        Color.Black.copy(alpha = 0.48f),
                        Color.Black.copy(alpha = 0.18f),
                        Color.Transparent
                    )
                )
            )
    } else {
        Modifier
            .padding(top = 4.dp)
            .clip(neonShape(22.dp))
            .background(Color.Black.copy(alpha = 0.44f))
    }

    Row(
        modifier = modifier.then(rowModifier)
            .padding(
                start = startSafePadding + horizontalPadding,
                end = endSafePadding + horizontalPadding,
                top = topPadding,
                bottom = bottomPadding
            ),
        verticalAlignment = Alignment.CenterVertically
    ) {
        val closeButton: @Composable () -> Unit = {
            IconButton(onClick = onDismiss) {
                Icon(
                    imageVector = Icons.Rounded.Close,
                    contentDescription = stringResource(R.string.common_back),
                    tint = Color.White
                )
            }
        }
        val saveButton: @Composable (() -> Unit)? = onSave?.let { saveAction ->
            {
                IconButton(onClick = saveAction) {
                    Icon(
                        imageVector = Icons.Rounded.Save,
                        contentDescription = stringResource(R.string.detail_save_screenshot),
                        tint = Color.White
                    )
                }
            }
        }

        val textBlock: @Composable (Modifier) -> Unit = { modifier ->
            Column(modifier = modifier) {
                if (!compact || fillWidthBackground) {
                    Text(
                        text = title,
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1
                    )
                }
                Text(
                    text = counter,
                    color = Color.White.copy(alpha = 0.74f),
                    style = if (compact) {
                        MaterialTheme.typography.labelMedium
                    } else {
                        MaterialTheme.typography.bodySmall
                    }
                )
            }
        }

        if (closeAtEnd) {
            textBlock(
                Modifier
                    .weight(1f)
                    .padding(start = 12.dp, end = 8.dp)
            )
            saveButton?.invoke()
            closeButton()
        } else {
            closeButton()
            saveButton?.invoke()
            textBlock(Modifier.padding(start = 4.dp, end = if (compact) 2.dp else 8.dp))
        }
    }
}

@Composable
private fun RemoteBitmapImage(
    imageUrl: String?,
    contentDescription: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop
) {
    val bitmap = produceState(
        initialValue = imageUrl?.let(RemoteDetailBitmapCache::get),
        key1 = imageUrl
    ) {
        value = imageUrl?.let { downloadBitmap(it) }
    }.value

    if (bitmap != null) {
        androidx.compose.foundation.Image(
            bitmap = bitmap.asImageBitmap(),
            contentDescription = contentDescription,
            modifier = modifier,
            contentScale = contentScale
        )
    } else {
        Box(
            modifier = modifier.background(Color.Black),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = contentDescription.take(1).uppercase(),
                color = Color.White.copy(alpha = 0.7f)
            )
        }
    }
}

private suspend fun saveScreenshotToGallery(
    context: Context,
    imageUrl: String,
    title: String
): Boolean = withContext(Dispatchers.IO) {
    val bitmap = downloadBitmap(imageUrl) ?: return@withContext false
    val resolver = context.contentResolver
    val safeTitle = title.take(24).replace(Regex("[^A-Za-z0-9_-]"), "_")
    val fileName = "emucorev_${safeTitle}_${System.currentTimeMillis()}.jpg"
    val values = ContentValues().apply {
        put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
        put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/EmuCoreV")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
    }
    val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) ?: return@withContext false
    runCatching {
        resolver.openOutputStream(uri)?.use { stream ->
            bitmap.compress(Bitmap.CompressFormat.JPEG, 95, stream)
        } ?: error("No output stream")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            values.clear()
            values.put(MediaStore.Images.Media.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
        }
        true
    }.getOrElse {
        resolver.delete(uri, null, null)
        false
    }
}

private suspend fun downloadBitmap(imageUrl: String): Bitmap? = withContext(Dispatchers.IO) {
    RemoteDetailBitmapCache.get(imageUrl)?.let { return@withContext it }
    runCatching {
        val connection = URL(imageUrl).openConnection() as HttpURLConnection
        connection.connectTimeout = 10_000
        connection.readTimeout = 10_000
        connection.instanceFollowRedirects = true
        connection.inputStream.use(BitmapFactory::decodeStream)
    }.getOrNull()?.also { bitmap ->
        RemoteDetailBitmapCache.put(imageUrl, bitmap)
    }
}

private object RemoteDetailBitmapCache {
    private val cache = object : LruCache<String, Bitmap>(calculateCacheSizeKb()) {
        override fun sizeOf(key: String, value: Bitmap): Int {
            return (value.byteCount / 1024).coerceAtLeast(1)
        }
    }

    fun get(url: String): Bitmap? = cache.get(url)

    fun put(url: String, bitmap: Bitmap) {
        if (cache.get(url) == null) {
            cache.put(url, bitmap)
        }
    }

    private fun calculateCacheSizeKb(): Int {
        val maxMemoryKb = (Runtime.getRuntime().maxMemory() / 1024L).toInt()
        return (maxMemoryKb / 10).coerceAtLeast(6 * 1024)
    }
}

@Composable
private fun YouTubePlayerBlock(
    youtubeId: String,
    lifecycleOwner: androidx.lifecycle.LifecycleOwner,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val options = remember {
        IFramePlayerOptions.Builder(context)
            .controls(1)
            .fullscreen(0)
            .build()
    }
    val view = remember {
        YouTubePlayerView(context).apply {
            enableAutomaticInitialization = false
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }
    }
    val playerState = remember { mutableStateOf<YouTubePlayer?>(null) }
    var initialized by remember { mutableStateOf(false) }
    var loadedVideoId by remember { mutableStateOf<String?>(null) }
    var currentPosition by rememberSaveable { mutableFloatStateOf(0f) }

    DisposableEffect(lifecycleOwner, view) {
        lifecycleOwner.lifecycle.addObserver(view)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(view)
            view.release()
        }
    }

    DisposableEffect(view) {
        if (!initialized) {
            val listener = object : AbstractYouTubePlayerListener() {
                override fun onReady(youTubePlayer: YouTubePlayer) {
                    playerState.value = youTubePlayer
                    // Load video starting from saved position
                    youTubePlayer.loadVideo(youtubeId, currentPosition)
                }

                override fun onCurrentSecond(youTubePlayer: YouTubePlayer, second: Float) {
                    currentPosition = second
                }
            }
            view.initialize(listener, options)
            initialized = true
        }
        onDispose { }
    }

    LaunchedEffect(youtubeId) {
        if (loadedVideoId != null && loadedVideoId != youtubeId) {
            currentPosition = 0f
        }

        val player = playerState.value ?: return@LaunchedEffect
        if (loadedVideoId != youtubeId) {
            player.loadVideo(youtubeId, currentPosition)
            loadedVideoId = youtubeId
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { view }
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LandscapeVideoTopBar(
    counter: String,
    onDismiss: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(WindowInsets.statusBarsIgnoringVisibility.asPaddingValues())
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Surface(
            shape = neonPillShape(),
            color = Color.Black.copy(alpha = 0.42f)
        ) {
            Text(
                text = counter,
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                color = Color.White.copy(alpha = 0.88f),
                style = MaterialTheme.typography.labelLarge
            )
        }

        Surface(
        modifier = Modifier.controllerFocus(shape = CircleShape),
            shape = CircleShape,
            color = Color.Black.copy(alpha = 0.42f),
            onClick = onDismiss
        ) {
            Box(
                modifier = Modifier.size(42.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Close,
                    contentDescription = stringResource(R.string.common_close),
                    tint = Color.White
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LandscapeViewerTopBar(
    title: String,
    counter: String,
    onSave: () -> Unit,
    onDismiss: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(WindowInsets.statusBarsIgnoringVisibility.asPaddingValues())
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Surface(
            shape = neonPillShape(),
            color = Color.Black.copy(alpha = 0.42f)
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    color = Color.White,
                    style = MaterialTheme.typography.labelLarge,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.widthIn(max = 200.dp)
                )
                Box(
                    modifier = Modifier
                        .size(1.dp, 12.dp)
                        .background(Color.White.copy(alpha = 0.3f))
                )
                Text(
                    text = counter,
                    color = Color.White.copy(alpha = 0.8f),
                    style = MaterialTheme.typography.labelMedium
                )
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Surface(
        modifier = Modifier.controllerFocus(shape = CircleShape),
                shape = CircleShape,
                color = Color.Black.copy(alpha = 0.42f),
                onClick = onSave
            ) {
                Box(
                    modifier = Modifier.size(42.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Save,
                        contentDescription = stringResource(R.string.detail_save_screenshot),
                        tint = Color.White
                    )
                }
            }

            Surface(
        modifier = Modifier.controllerFocus(shape = CircleShape),
                shape = CircleShape,
                color = Color.Black.copy(alpha = 0.42f),
                onClick = onDismiss
            ) {
                Box(
                    modifier = Modifier.size(42.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Close,
                        contentDescription = stringResource(R.string.common_close),
                        tint = Color.White
                    )
                }
            }
        }
    }
}

private fun youtubeThumbnailUrl(videoId: String): String {
    return "https://img.youtube.com/vi/${normalizeYoutubeId(videoId)}/hqdefault.jpg"
}

private fun normalizeYoutubeId(raw: String): String {
    val value = raw.trim()
    return when {
        value.contains("v=") -> value.substringAfter("v=").substringBefore("&").substringBefore("?")
        value.contains("youtu.be/") -> value.substringAfter("youtu.be/").substringBefore("?").substringBefore("&")
        else -> value.substringBefore("?").substringBefore("&")
    }
}

private fun Context.findActivity(): Activity? {
    var current = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return null
}
