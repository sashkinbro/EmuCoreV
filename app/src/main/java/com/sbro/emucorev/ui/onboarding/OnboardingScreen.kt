package com.sbro.emucorev.ui.onboarding

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.ArrowForward
import androidx.compose.material.icons.automirrored.rounded.LibraryBooks
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.FirmwareKind
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
    firmwareInstalled: Boolean,
    firmwareUpdateInstalled: Boolean,
    onInstallFirmware: () -> Unit,
    onInstallFirmwareUpdate: () -> Unit,
    onInstallDownloadedFirmware: (String) -> Unit = {},
    firmwareDownloadViewModel: FirmwareDownloadViewModel = viewModel(),
    viewModel: OnboardingViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val downloadState by firmwareDownloadViewModel.state.collectAsState()
    val pagerState = rememberPagerState(pageCount = { uiState.totalPages })
    val scope = rememberCoroutineScope()
    var isCompleting by remember { mutableStateOf(false) }
    val folderPickerFailedMessage = stringResource(R.string.folder_picker_failed)

    LaunchedEffect(uiState.currentPage) {
        if (pagerState.currentPage != uiState.currentPage) {
            pagerState.animateScrollToPage(uiState.currentPage)
        }
    }
    LaunchedEffect(pagerState.currentPage) {
        if (pagerState.currentPage != uiState.currentPage) {
            viewModel.setCurrentPage(pagerState.currentPage)
        }
    }
    LaunchedEffect(downloadState.status, downloadState.resultFileUri) {
        val resultUri = downloadState.resultFileUri
        if (downloadState.status == FirmwareDownloadStatus.Completed && resultUri != null) {
            onInstallDownloadedFirmware(resultUri)
            firmwareDownloadViewModel.consumeResult()
        }
    }

    val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        runCatching {
            viewModel.onPackagesFolderSelected(uri)
        }.onFailure {
            Toast.makeText(context, folderPickerFailedMessage, Toast.LENGTH_SHORT).show()
        }
    }

    val chooseFolderClick = rememberDebouncedClick { folderPicker.launch(null) }
    val installFirmwareClick = rememberDebouncedClick(onClick = onInstallFirmware)
    val installFirmwareUpdateClick = rememberDebouncedClick(onClick = onInstallFirmwareUpdate)
    val backClick = rememberDebouncedClick(onClick = viewModel::goBack)
    val nextClick = rememberDebouncedClick(onClick = viewModel::goNext)
    val canComplete = uiState.canContinue && firmwareInstalled && firmwareUpdateInstalled
    val completeClick = rememberDebouncedClick {
        if (isCompleting || !canComplete) return@rememberDebouncedClick
        isCompleting = true
        scope.launch {
            delay(280)
            viewModel.completeOnboarding()
            onComplete()
        }
    }

    val contentAlpha by animateFloatAsState(
        targetValue = if (isCompleting) 0.34f else 1f,
        animationSpec = tween(280),
        label = "onboarding-content-alpha"
    )
    val contentOffset by animateFloatAsState(
        targetValue = if (isCompleting) -32f else 0f,
        animationSpec = tween(320),
        label = "onboarding-content-offset"
    )

    val backgroundMotion = rememberInfiniteTransition(label = "onboarding-background-motion")
    val isDarkTheme = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val orbOneOffsetX by backgroundMotion.animateFloat(
        initialValue = -18f,
        targetValue = 42f,
        animationSpec = infiniteRepeatable(animation = tween(5200), repeatMode = RepeatMode.Reverse),
        label = "orb-one-offset-x"
    )
    val orbOneOffsetY by backgroundMotion.animateFloat(
        initialValue = -12f,
        targetValue = 34f,
        animationSpec = infiniteRepeatable(animation = tween(6100), repeatMode = RepeatMode.Reverse),
        label = "orb-one-offset-y"
    )
    val orbTwoOffsetX by backgroundMotion.animateFloat(
        initialValue = 20f,
        targetValue = -56f,
        animationSpec = infiniteRepeatable(animation = tween(6800), repeatMode = RepeatMode.Reverse),
        label = "orb-two-offset-x"
    )
    val orbTwoOffsetY by backgroundMotion.animateFloat(
        initialValue = 0f,
        targetValue = 58f,
        animationSpec = infiniteRepeatable(animation = tween(5600), repeatMode = RepeatMode.Reverse),
        label = "orb-two-offset-y"
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        MaterialTheme.colorScheme.primaryContainer.copy(alpha = if (isDarkTheme) 0.58f else 0.72f),
                        MaterialTheme.colorScheme.background,
                        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = if (isDarkTheme) 0.70f else 0.88f)
                    )
                )
            )
    ) {
        Box(
            modifier = Modifier
                .padding(start = 28.dp)
                .size(180.dp)
                .graphicsLayer {
                    translationX = orbOneOffsetX
                    translationY = orbOneOffsetY
                }
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.primary.copy(alpha = if (isDarkTheme) 0.13f else 0.10f))
        )
        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 96.dp, end = 20.dp)
                .size(140.dp)
                .graphicsLayer {
                    translationX = orbTwoOffsetX
                    translationY = orbTwoOffsetY
                }
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.tertiary.copy(alpha = if (isDarkTheme) 0.12f else 0.10f))
        )

        Box(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .graphicsLayer {
                    alpha = contentAlpha
                    translationY = contentOffset
                }
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .statusBarsPadding()
                        .navigationBarsPadding()
                        .padding(start = ScreenHorizontalPadding, end = ScreenHorizontalPadding, top = 48.dp, bottom = 160.dp),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    when (page) {
                        0 -> OnboardingHero(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.onboarding_page_1_title),
                            subtitle = stringResource(R.string.onboarding_page_1_body)
                        )
                        1 -> OnboardingHero(
                            icon = Icons.Rounded.SmartDisplay,
                            title = stringResource(R.string.onboarding_page_2_title),
                            subtitle = stringResource(R.string.onboarding_page_2_body)
                        )
                        2 -> OnboardingHero(
                            icon = Icons.AutoMirrored.Rounded.LibraryBooks,
                            title = stringResource(R.string.onboarding_page_3_title),
                            subtitle = stringResource(R.string.onboarding_page_3_body)
                        )
                        else -> OnboardingSetupContent(
                            packagesFolder = uiState.packagesFolder,
                            storagePath = uiState.storagePath,
                            firmwareInstalled = firmwareInstalled,
                            firmwareUpdateInstalled = firmwareUpdateInstalled,
                            installFirmware = installFirmwareClick,
                            installFirmwareUpdate = installFirmwareUpdateClick,
                            downloadState = downloadState,
                            startFirmwareDownload = firmwareDownloadViewModel::start,
                            cancelFirmwareDownload = firmwareDownloadViewModel::cancel,
                            launchFolderPicker = chooseFolderClick
                        )
                    }
                }
            }

            Column(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .navigationBarsPadding()
                    .padding(horizontal = ScreenHorizontalPadding, vertical = 20.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                OnboardingIndicator(
                    currentPage = pagerState.currentPage,
                    totalPages = uiState.totalPages
                )
                Spacer(modifier = Modifier.height(24.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                        if (uiState.currentPage > 0) {
                            OutlinedButton(
                                onClick = backClick,
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = null)
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(stringResource(R.string.onboarding_back))
                            }
                        }
                    }
                    Spacer(modifier = Modifier.width(12.dp))
                    Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
                        if (uiState.currentPage < uiState.totalPages - 1) {
                            Button(
                                onClick = nextClick,
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Text(stringResource(R.string.onboarding_next))
                                Spacer(modifier = Modifier.width(6.dp))
                                Icon(Icons.AutoMirrored.Rounded.ArrowForward, contentDescription = null)
                            }
                        } else {
                            Button(
                                onClick = completeClick,
                                enabled = canComplete,
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Text(stringResource(R.string.onboarding_get_started))
                                Spacer(modifier = Modifier.width(8.dp))
                                Icon(Icons.AutoMirrored.Rounded.ArrowForward, contentDescription = null)
                            }
                        }
                    }
                }
            }
        }

        AnimatedVisibility(
            visible = isCompleting,
            modifier = Modifier.align(Alignment.Center),
            enter = fadeIn(tween(180)) + scaleIn(initialScale = 0.96f, animationSpec = tween(220)),
            exit = fadeOut(tween(120)) + scaleOut(targetScale = 1.02f, animationSpec = tween(120))
        ) {
            Surface(
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
                tonalElevation = 12.dp
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 22.dp, vertical = 18.dp),
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(22.dp),
                        strokeWidth = 2.5.dp
                    )
                    Text(
                        text = stringResource(R.string.onboarding_get_started),
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }
    }
}

@Composable
private fun OnboardingHero(
    icon: ImageVector,
    title: String,
    subtitle: String
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(
            modifier = Modifier
                .size(112.dp)
                .clip(RoundedCornerShape(32.dp))
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(56.dp)
            )
        }
        Spacer(modifier = Modifier.height(36.dp))
        Text(
            text = title,
            style = MaterialTheme.typography.displayLarge.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.5).sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = subtitle,
            style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 6.dp)
        )
    }
}

@Composable
private fun OnboardingSetupContent(
    packagesFolder: String?,
    storagePath: String,
    firmwareInstalled: Boolean,
    firmwareUpdateInstalled: Boolean,
    installFirmware: () -> Unit,
    installFirmwareUpdate: () -> Unit,
    downloadState: FirmwareDownloadState,
    startFirmwareDownload: (FirmwareKind) -> Unit,
    cancelFirmwareDownload: () -> Unit,
    launchFolderPicker: () -> Unit
) {
    val isDownloading = downloadState.status == FirmwareDownloadStatus.Running
    val downloadButton = stringResource(R.string.onboarding_firmware_download)
    val cancelDownloadButton = stringResource(R.string.onboarding_firmware_cancel_download)

    val baseDownloadStatus = firmwareDownloadStatusText(FirmwareKind.Base, downloadState)
    val updateDownloadStatus = firmwareDownloadStatusText(FirmwareKind.Update, downloadState)
    val baseDownloadProgress = downloadState.progress.takeIf {
        downloadState.kind == FirmwareKind.Base && downloadState.status == FirmwareDownloadStatus.Running
    }
    val updateDownloadProgress = downloadState.progress.takeIf {
        downloadState.kind == FirmwareKind.Update && downloadState.status == FirmwareDownloadStatus.Running
    }

    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = stringResource(R.string.onboarding_page_4_title),
            style = MaterialTheme.typography.displayLarge.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.5).sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(12.dp))
        Text(
            text = stringResource(R.string.onboarding_page_4_body),
            style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 6.dp)
        )
        Spacer(modifier = Modifier.height(26.dp))

        SetupCard(
            icon = Icons.Rounded.Memory,
            title = stringResource(R.string.onboarding_firmware_title),
            description = stringResource(R.string.onboarding_firmware_desc),
            status = if (firmwareInstalled) {
                stringResource(R.string.onboarding_status_ready)
            } else {
                stringResource(R.string.onboarding_status_install_firmware)
            },
            statusColor = if (firmwareInstalled) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.primary,
            onClick = installFirmware,
            secondaryActionLabel = if (firmwareInstalled) null else if (baseDownloadProgress != null) cancelDownloadButton else downloadButton,
            secondaryActionEnabled = !isDownloading || downloadState.kind == FirmwareKind.Base,
            onSecondaryAction = {
                if (baseDownloadProgress != null) cancelFirmwareDownload() else startFirmwareDownload(FirmwareKind.Base)
            },
            downloadProgress = baseDownloadProgress,
            downloadStatus = baseDownloadStatus
        )

        Spacer(modifier = Modifier.height(10.dp))

        SetupCard(
            icon = Icons.Rounded.SystemUpdateAlt,
            title = stringResource(R.string.onboarding_firmware_update_title),
            description = stringResource(R.string.onboarding_firmware_update_desc),
            status = if (firmwareUpdateInstalled) {
                stringResource(R.string.onboarding_status_ready)
            } else {
                stringResource(R.string.onboarding_status_install_firmware_update)
            },
            statusColor = if (firmwareUpdateInstalled) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.primary,
            onClick = installFirmwareUpdate,
            secondaryActionLabel = if (firmwareUpdateInstalled) null else if (updateDownloadProgress != null) cancelDownloadButton else downloadButton,
            secondaryActionEnabled = !isDownloading || downloadState.kind == FirmwareKind.Update,
            onSecondaryAction = {
                if (updateDownloadProgress != null) cancelFirmwareDownload() else startFirmwareDownload(FirmwareKind.Update)
            },
            downloadProgress = updateDownloadProgress,
            downloadStatus = updateDownloadStatus
        )

        Spacer(modifier = Modifier.height(10.dp))

        SetupCard(
            icon = Icons.Rounded.FolderOpen,
            title = stringResource(R.string.onboarding_packages_title),
            description = packagesFolder ?: stringResource(R.string.onboarding_packages_desc),
            status = if (packagesFolder == null) {
                stringResource(R.string.onboarding_status_pick_folder)
            } else {
                stringResource(R.string.onboarding_status_ready)
            },
            statusColor = if (packagesFolder == null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.tertiary,
            onClick = launchFolderPicker
        )

        Spacer(modifier = Modifier.height(10.dp))

        SetupCard(
            icon = Icons.Rounded.Memory,
            title = stringResource(R.string.onboarding_storage_title),
            description = storagePath,
            status = stringResource(R.string.onboarding_status_ready),
            statusColor = MaterialTheme.colorScheme.tertiary,
            onClick = {}
        )
    }
}

@Composable
private fun firmwareDownloadStatusText(
    kind: FirmwareKind,
    state: FirmwareDownloadState
): String? {
    if (state.kind != kind) return null
    return when (state.status) {
        FirmwareDownloadStatus.Running -> {
            val percent = (state.progress * 100f).toInt().coerceIn(0, 100)
            stringResource(R.string.onboarding_firmware_downloading, percent)
        }

        FirmwareDownloadStatus.Failed -> stringResource(R.string.onboarding_firmware_download_failed)
        else -> null
    }
}

@Composable
private fun SetupCard(
    icon: ImageVector,
    title: String,
    description: String,
    status: String,
    statusColor: Color,
    onClick: () -> Unit,
    secondaryActionLabel: String? = null,
    secondaryActionEnabled: Boolean = true,
    onSecondaryAction: (() -> Unit)? = null,
    downloadProgress: Float? = null,
    downloadStatus: String? = null
) {
    val isDarkTheme = MaterialTheme.colorScheme.background.luminance() < 0.5f
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        shape = RoundedCornerShape(32.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
        tonalElevation = 2.dp,
        onClick = onClick,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = if (isDarkTheme) 0.45f else 0.7f)
        )
    ) {
        Column(modifier = Modifier.padding(horizontal = CardContentPadding, vertical = CardContentPadding)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(52.dp)
                        .clip(RoundedCornerShape(18.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(26.dp)
                    )
                }

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            Surface(
                shape = CircleShape,
                color = statusColor.copy(alpha = 0.12f)
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(6.dp)
                            .clip(CircleShape)
                            .background(statusColor)
                    )
                    Text(
                        text = status,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = statusColor
                    )
                }
            }

            if (secondaryActionLabel != null || downloadProgress != null || downloadStatus != null) {
                Spacer(modifier = Modifier.height(12.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (downloadStatus != null) {
                        Text(
                            text = downloadStatus,
                            style = MaterialTheme.typography.bodySmall,
                            color = if (downloadProgress == null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.weight(1f)
                        )
                    } else {
                        Spacer(modifier = Modifier.weight(1f))
                    }

                    if (secondaryActionLabel != null && onSecondaryAction != null) {
                        Spacer(modifier = Modifier.width(12.dp))
                        OutlinedButton(
                            onClick = onSecondaryAction,
                            enabled = secondaryActionEnabled
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.CloudDownload,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(secondaryActionLabel)
                        }
                    }
                }
            }

            if (downloadProgress != null) {
                Spacer(modifier = Modifier.height(10.dp))
                LinearProgressIndicator(
                    progress = { downloadProgress },
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

@Composable
private fun OnboardingIndicator(
    currentPage: Int,
    totalPages: Int
) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        repeat(totalPages) { index ->
            val selected = index == currentPage
            val width by animateFloatAsState(
                targetValue = if (selected) 28f else 8f,
                animationSpec = tween(250),
                label = "onboarding-indicator"
            )
            Box(
                modifier = Modifier
                    .height(8.dp)
                    .width(width.dp)
                    .clip(CircleShape)
                    .background(
                        if (selected) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
                    )
            )
        }
    }
}
