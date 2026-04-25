package com.sbro.emucorev.ui.onboarding

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
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.FirmwareKind
import com.sbro.emucorev.ui.common.rememberDebouncedClick
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
    val uiState by viewModel.uiState.collectAsState()
    val downloadState by firmwareDownloadViewModel.state.collectAsState()
    val pagerState = rememberPagerState(pageCount = { uiState.totalPages })
    val scope = rememberCoroutineScope()
    var isCompleting by remember { mutableStateOf(false) }

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
                val isSetupPage = page == uiState.totalPages - 1
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .statusBarsPadding()
                        .navigationBarsPadding()
                        .padding(
                            start = ScreenHorizontalPadding,
                            end = ScreenHorizontalPadding,
                            top = if (isSetupPage) 12.dp else 48.dp,
                            bottom = if (isSetupPage) 144.dp else 160.dp
                        ),
                    verticalArrangement = if (isSetupPage) Arrangement.Top else Arrangement.Center,
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
                            storagePath = uiState.storagePath,
                            firmwareInstalled = firmwareInstalled,
                            firmwareUpdateInstalled = firmwareUpdateInstalled,
                            installFirmware = installFirmwareClick,
                            installFirmwareUpdate = installFirmwareUpdateClick,
                            downloadState = downloadState,
                            startFirmwareDownload = firmwareDownloadViewModel::start,
                            cancelFirmwareDownload = firmwareDownloadViewModel::cancel
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
                letterSpacing = 0.sp
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
    storagePath: String,
    firmwareInstalled: Boolean,
    firmwareUpdateInstalled: Boolean,
    installFirmware: () -> Unit,
    installFirmwareUpdate: () -> Unit,
    downloadState: FirmwareDownloadState,
    startFirmwareDownload: (FirmwareKind) -> Unit,
    cancelFirmwareDownload: () -> Unit
) {
    val isDownloading = downloadState.status == FirmwareDownloadStatus.Running
    val downloadButton = stringResource(R.string.onboarding_firmware_download)
    val cancelDownloadButton = stringResource(R.string.onboarding_firmware_cancel_download)
    var firmwareInfoVisible by remember { mutableStateOf(false) }

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
            style = MaterialTheme.typography.headlineLarge.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = 0.sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = stringResource(R.string.onboarding_page_4_body),
            style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 23.sp),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 6.dp)
        )
        Spacer(modifier = Modifier.height(18.dp))

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
            onInfoClick = { firmwareInfoVisible = true },
            downloadProgress = baseDownloadProgress,
            downloadStatus = baseDownloadStatus
        )

        Spacer(modifier = Modifier.height(9.dp))

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
            onInfoClick = { firmwareInfoVisible = true },
            downloadProgress = updateDownloadProgress,
            downloadStatus = updateDownloadStatus
        )

        Spacer(modifier = Modifier.height(9.dp))

        SetupCard(
            icon = Icons.Rounded.Memory,
            title = stringResource(R.string.onboarding_storage_title),
            description = storagePath,
            status = stringResource(R.string.onboarding_status_ready),
            statusColor = MaterialTheme.colorScheme.tertiary,
            onClick = {}
        )

        FirmwareDownloadInfoDialog(
            visible = firmwareInfoVisible,
            onDismiss = { firmwareInfoVisible = false }
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
    onInfoClick: (() -> Unit)? = null,
    downloadProgress: Float? = null,
    downloadStatus: String? = null
) {
    val isDarkTheme = MaterialTheme.colorScheme.background.luminance() < 0.5f
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        shape = RoundedCornerShape(26.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
        tonalElevation = 2.dp,
        onClick = onClick,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = if (isDarkTheme) 0.45f else 0.7f)
        )
    ) {
        Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp)) {
            Row(
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(48.dp)
                        .clip(RoundedCornerShape(16.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(24.dp)
                    )
                }

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2
                    )
                    Spacer(modifier = Modifier.height(1.dp))
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodyMedium.copy(lineHeight = 21.sp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }

            Spacer(modifier = Modifier.height(14.dp))
            if (secondaryActionLabel != null && onSecondaryAction != null) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    StatusPill(
                        status = status,
                        statusColor = statusColor,
                        modifier = Modifier.fillMaxWidth()
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (onInfoClick != null) {
                            FirmwareInfoButton(onClick = onInfoClick)
                        } else {
                            Spacer(modifier = Modifier.width(48.dp))
                        }
                        Button(
                            onClick = onSecondaryAction,
                            enabled = secondaryActionEnabled,
                            modifier = Modifier.align(Alignment.CenterVertically),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.primary,
                                contentColor = MaterialTheme.colorScheme.onPrimary
                            )
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
            } else {
                StatusPill(
                    status = status,
                    statusColor = statusColor
                )
            }

            if (downloadProgress != null) {
                Spacer(modifier = Modifier.height(8.dp))
                downloadStatus?.let { statusText ->
                    Text(
                        text = statusText,
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                }
                LinearProgressIndicator(
                    progress = { downloadProgress },
                    modifier = Modifier.fillMaxWidth()
                )
            } else if (downloadStatus != null) {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = downloadStatus,
                    style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

@Composable
private fun FirmwareInfoButton(onClick: () -> Unit) {
    val pulse = rememberInfiniteTransition(label = "firmware-info-pulse")
    val scale by pulse.animateFloat(
        initialValue = 1f,
        targetValue = 1.08f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 1200),
            repeatMode = RepeatMode.Reverse
        ),
        label = "firmware-info-scale"
    )
    val alpha by pulse.animateFloat(
        initialValue = 0.10f,
        targetValue = 0.22f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 1200),
            repeatMode = RepeatMode.Reverse
        ),
        label = "firmware-info-alpha"
    )
    Surface(
        modifier = Modifier.graphicsLayer {
            scaleX = scale
            scaleY = scale
        },
        shape = CircleShape,
        color = MaterialTheme.colorScheme.primary.copy(alpha = alpha),
        contentColor = MaterialTheme.colorScheme.primary,
        onClick = onClick
    ) {
        Icon(
            imageVector = Icons.Rounded.Info,
            contentDescription = null,
            modifier = Modifier
                .padding(12.dp)
                .size(20.dp)
        )
    }
}

@Composable
private fun FirmwareDownloadInfoDialog(
    visible: Boolean,
    onDismiss: () -> Unit
) {
    if (!visible) return
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = {
            Icon(Icons.Rounded.Info, contentDescription = null)
        },
        title = {
            Text(stringResource(R.string.onboarding_firmware_info_title))
        },
        text = {
            Text(
                text = stringResource(R.string.onboarding_firmware_info_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.install_dialog_close))
            }
        }
    )
}

@Composable
private fun StatusPill(
    status: String,
    statusColor: Color,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = CircleShape,
        color = statusColor.copy(alpha = 0.12f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
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
                color = statusColor,
                maxLines = 3,
                overflow = TextOverflow.Clip
            )
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
