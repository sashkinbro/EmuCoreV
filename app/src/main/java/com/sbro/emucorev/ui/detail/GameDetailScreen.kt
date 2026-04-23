package com.sbro.emucorev.ui.detail

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.ContextWrapper
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.provider.MediaStore
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CalendarToday
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ConfirmationNumber
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.VideoLibrary
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.window.Dialog
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
import com.sbro.emucorev.data.VitaCompatibilityState
import com.sbro.emucorev.data.VitaCompatibilitySummary
import com.sbro.emucorev.data.VitaCatalogDetails
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.UrlImage
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset
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
    viewModel: GameDetailViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    val backClick = rememberDebouncedClick(onClick = onBack)
    val launchFailedMessage = stringResource(R.string.game_launch_failed)
    val launchRequiresFirmwareMessage = stringResource(R.string.game_launch_requires_firmware)
    val launchRequiresFirmwareUpdateMessage = stringResource(R.string.game_launch_requires_firmware_update)
    val deleteGameLabel = stringResource(R.string.detail_delete_game)
    val deleteGameConfirmTitle = stringResource(R.string.detail_delete_game_confirm_title)
    val deleteGameConfirmBody = stringResource(R.string.detail_delete_game_confirm_body)
    val deleteGameFailedMessage = stringResource(R.string.detail_delete_game_failed)
    val horizontalInset = ScreenHorizontalPadding
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val contentMaxWidth = if (isLandscape) 980.dp else 760.dp
    val heroMaxWidth = if (isLandscape) 170.dp else 240.dp
    var selectedScreenshotIndex by rememberSaveable { mutableIntStateOf(-1) }
    var selectedVideoIndex by rememberSaveable { mutableIntStateOf(-1) }
    var showOverflowMenu by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }

    LaunchedEffect(titleId, igdbId) {
        viewModel.load(titleId, igdbId)
    }

    val game = uiState.game
    val catalog = uiState.catalogEntry
    val compatibility = uiState.compatibility
    val displayTitle = game?.title ?: catalog?.name ?: stringResource(R.string.common_not_available)
    val heroImage = catalogDetailImageUrl(catalog) ?: game?.iconPath
    val knownTitleIds = remember(game?.titleId, catalog?.serials) {
        buildList {
            game?.titleId?.takeIf(String::isNotBlank)?.let(::add)
            addAll(catalog?.serials.orEmpty().filter(String::isNotBlank))
        }.distinct()
    }

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
                            when (VitaLaunchBridge.launchInstalledTitle(context, it.titleId)) {
                                LaunchResult.Success -> Unit
                                LaunchResult.MissingFirmware -> Toast.makeText(context, launchRequiresFirmwareMessage, Toast.LENGTH_SHORT).show()
                                LaunchResult.MissingFirmwareUpdate -> Toast.makeText(context, launchRequiresFirmwareUpdateMessage, Toast.LENGTH_SHORT).show()
                                LaunchResult.Failure -> Toast.makeText(context, launchFailedMessage, Toast.LENGTH_SHORT).show()
                            }
                        }
                    }

                    if (isLandscape) {
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
                                    .aspectRatio(2f / 3f)
                                    .clip(RoundedCornerShape(28.dp))
                                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                            ) {
                                if (heroImage == game?.iconPath) {
                                    LocalImage(
                                        path = game?.iconPath,
                                        contentDescription = displayTitle,
                                        fallbackLabel = displayTitle,
                                        modifier = Modifier.fillMaxSize()
                                    )
                                } else {
                                    UrlImage(
                                        imageUrl = heroImage,
                                        contentDescription = displayTitle,
                                        fallbackLabel = displayTitle,
                                        modifier = Modifier.fillMaxSize()
                                    )
                                }
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
                                        onClick = launchGameClick,
                                        modifier = Modifier.widthIn(min = 240.dp, max = 320.dp)
                                    ) {
                                        Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                                        Text(
                                            text = stringResource(R.string.detail_launch),
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
                                    .aspectRatio(2f / 3f)
                                    .clip(RoundedCornerShape(28.dp))
                                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                            ) {
                                if (heroImage == game?.iconPath) {
                                    LocalImage(
                                        path = game?.iconPath,
                                        contentDescription = displayTitle,
                                        fallbackLabel = displayTitle,
                                        modifier = Modifier.fillMaxSize()
                                    )
                                } else {
                                    UrlImage(
                                        imageUrl = heroImage,
                                        contentDescription = displayTitle,
                                        fallbackLabel = displayTitle,
                                        modifier = Modifier.fillMaxSize()
                                    )
                                }
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
                                    onClick = launchGameClick,
                                    modifier = Modifier.fillMaxWidth()
                                ) {
                                    Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                                    Text(
                                        text = stringResource(R.string.detail_launch),
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

                    if (game != null || catalog != null) {
                        SectionCard(
                            title = stringResource(R.string.detail_compatibility_title),
                            modifier = Modifier
                                .align(Alignment.CenterHorizontally)
                                .widthIn(max = contentMaxWidth)
                                .padding(horizontal = horizontalInset)
                        ) {
                            when {
                                compatibility != null -> {
                                    MetadataRow(
                                        stringResource(R.string.detail_compatibility_status),
                                        stringResource(compatibility.state.labelResId())
                                    )
                                    MetadataRow(
                                        stringResource(R.string.detail_compatibility_issue),
                                        "#${compatibility.issueId}"
                                    )
                                    MetadataRow(
                                        stringResource(R.string.detail_compatibility_title_id_label),
                                        compatibility.matchedTitleId
                                    )
                                    compatibility.updatedAtEpochSeconds?.let { updatedAt ->
                                        MetadataRow(
                                            stringResource(R.string.detail_compatibility_updated),
                                            formatCompatibilityUpdatedAt(updatedAt)
                                        )
                                    }
                                    if (knownTitleIds.size > 1) {
                                        MetadataRow(
                                            stringResource(R.string.detail_compatibility_known_serials),
                                            knownTitleIds.joinToString(", ")
                                        )
                                    }
                                    DetailSourceCard(
                                        text = stringResource(
                                            R.string.detail_compatibility_source_note,
                                            compatibility.issueId
                                        )
                                    )
                                }

                                !uiState.isCompatibilityAvailable -> {
                                    Text(
                                        text = stringResource(R.string.detail_compatibility_unavailable),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }

                                knownTitleIds.isEmpty() -> {
                                    Text(
                                        text = stringResource(R.string.detail_compatibility_unmapped),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }

                                else -> {
                                    Text(
                                        text = stringResource(R.string.detail_compatibility_missing_entry),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
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

private fun catalogDetailImageUrl(catalog: VitaCatalogDetails?): String? {
    val sourceUrl = catalog?.coverUrl ?: catalog?.heroUrl ?: return null
    return if (sourceUrl.contains("/image/upload/", ignoreCase = true)) {
        sourceUrl.replace(Regex("/t_[^/]+/"), "/t_1080p/")
    } else {
        sourceUrl
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
                .aspectRatio(2f / 3f)
                .clip(RoundedCornerShape(28.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .fillMaxWidth(0.72f)
                .height(34.dp)
                .clip(RoundedCornerShape(14.dp))
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
                        .clip(RoundedCornerShape(999.dp))
                )
            }
        }
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(56.dp)
                .clip(RoundedCornerShape(16.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(144.dp)
                .clip(RoundedCornerShape(16.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .width(156.dp)
                .height(24.dp)
                .clip(RoundedCornerShape(10.dp))
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
                        .clip(RoundedCornerShape(18.dp))
                )
            }
        }
    }
}

@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
    )
}

@Composable
private fun EmptyDetailState(
    isCatalogAvailable: Boolean,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
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
    compatibility: VitaCompatibilitySummary?
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        year?.let { MetaChip(Icons.Rounded.CalendarToday, it.toString()) }
        rating?.let { MetaChip(Icons.Rounded.Star, String.format(Locale.US, "%.1f", it / 10.0)) }
        serial?.takeIf { it.isNotBlank() }?.let { MetaChip(Icons.Rounded.ConfirmationNumber, it) }
        compatibility?.let { CompatibilityMetaChip(it) }
    }
}

@Composable
private fun MetaChip(
    icon: ImageVector,
    text: String
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
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
        shape = RoundedCornerShape(999.dp),
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
private fun CompatibilityMetaChip(compatibility: VitaCompatibilitySummary) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = compatibilityContainerColor(compatibility.state)
    ) {
        Text(
            text = stringResource(compatibility.state.labelResId()),
            modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = 8.dp),
            style = MaterialTheme.typography.labelLarge,
            color = compatibilityContentColor(compatibility.state),
            fontWeight = FontWeight.SemiBold
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

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(
                animationSpec = spring(
                    dampingRatio = Spring.DampingRatioNoBouncy,
                    stiffness = Spring.StiffnessLow
                )
            ),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier.padding(CardContentPadding),
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
                TextButton(
                    onClick = { expanded = !expanded },
                    contentPadding = PaddingValues(0.dp)
                ) {
                    Text(
                        text = stringResource(
                            if (expanded) R.string.detail_show_less else R.string.detail_show_more
                        )
                    )
                }
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
        shape = RoundedCornerShape(16.dp),
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
    Surface(
        modifier = Modifier.width(220.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(130.dp)
                .clip(RoundedCornerShape(18.dp))
        ) {
            UrlImage(
                imageUrl = imageUrl,
                contentDescription = title,
                fallbackLabel = title,
                modifier = Modifier.fillMaxSize()
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
    Surface(
        modifier = Modifier.width(232.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(16f / 9f)
                .clip(RoundedCornerShape(18.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
        ) {
            UrlImage(
                imageUrl = youtubeThumbnailUrl(youtubeId),
                contentDescription = title,
                fallbackLabel = title,
                modifier = Modifier.fillMaxSize()
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
                shape = RoundedCornerShape(999.dp),
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
            controller?.show(WindowInsetsCompat.Type.systemBars())
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

@Composable
private fun ViewerTopBar(
    title: String,
    counter: String,
    onSave: (() -> Unit)?,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .windowInsetsPadding(WindowInsets.displayCutout)
            .padding(horizontal = 20.dp, vertical = 20.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = Color.White,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = counter,
                style = MaterialTheme.typography.bodyMedium,
                color = Color.White.copy(alpha = 0.8f)
            )
        }
        if (onSave != null) {
            IconButton(onClick = onSave) {
                Icon(
                    imageVector = Icons.Rounded.Save,
                    contentDescription = stringResource(R.string.detail_save_screenshot),
                    tint = Color.White
                )
            }
        }
        IconButton(onClick = onDismiss) {
            Icon(
                imageVector = Icons.Rounded.Close,
                contentDescription = stringResource(R.string.common_close),
                tint = Color.White
            )
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
    val bitmap = produceState<Bitmap?>(initialValue = null, key1 = imageUrl) {
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
    runCatching {
        val connection = URL(imageUrl).openConnection() as HttpURLConnection
        connection.connectTimeout = 10_000
        connection.readTimeout = 10_000
        connection.instanceFollowRedirects = true
        connection.inputStream.use(BitmapFactory::decodeStream)
    }.getOrNull()
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
            shape = RoundedCornerShape(999.dp),
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
            shape = RoundedCornerShape(999.dp),
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

private fun formatCompatibilityUpdatedAt(epochSeconds: Long): String {
    return java.time.Instant.ofEpochSecond(epochSeconds)
        .atZone(java.time.ZoneId.systemDefault())
        .toLocalDate()
        .toString()
}

@Composable
private fun compatibilityContainerColor(state: VitaCompatibilityState): Color {
    return when (state) {
        VitaCompatibilityState.PLAYABLE -> Color(0x1F2E7D32)
        VitaCompatibilityState.INGAME_MORE -> Color(0x1F00897B)
        VitaCompatibilityState.INGAME_LESS -> Color(0x1F1E88E5)
        VitaCompatibilityState.MENU -> Color(0x1F00ACC1)
        VitaCompatibilityState.INTRO -> Color(0x1FF9A825)
        VitaCompatibilityState.BOOTABLE -> Color(0x1FFF8F00)
        VitaCompatibilityState.NOTHING -> Color(0x1FC62828)
        VitaCompatibilityState.UNKNOWN -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.7f)
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

private fun Context.findActivity(): Activity? {
    var current = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return null
}
