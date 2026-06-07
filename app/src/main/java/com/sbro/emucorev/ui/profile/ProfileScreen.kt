package com.sbro.emucorev.ui.profile

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.automirrored.rounded.ViewList
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.CloudUpload
import androidx.compose.material.icons.rounded.Email
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.PhotoCamera
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material.icons.rounded.ViewModule
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.ProfileCatalogGame
import com.sbro.emucorev.data.ProfileGameStatus
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.NavigationMenuButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.UrlImage
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset
import com.sbro.emucorev.ui.theme.useMultiColumnLayout
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ProfileScreen(
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onGameClick: (Long) -> Unit,
    viewModel: ProfileViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val useDenseCards = configuration.useMultiColumnLayout()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    val gridState = rememberLazyGridState()
    var showEmailDialog by rememberSaveable { mutableStateOf(false) }
    var showSignOutConfirm by rememberSaveable { mutableStateOf(false) }
    val avatarPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        runCatching {
            context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        viewModel.setLocalAvatar(uri.toString())
    }
    val profilePreferences = remember(context) {
        context.getSharedPreferences("profile_preferences", Context.MODE_PRIVATE)
    }
    var accountCardHidden by rememberSaveable {
        mutableStateOf(profilePreferences.getBoolean("profile_backup_card_hidden", false))
    }
    val setAccountCardHidden: (Boolean) -> Unit = { hidden ->
        accountCardHidden = hidden
        profilePreferences.edit().putBoolean("profile_backup_card_hidden", hidden).apply()
    }
    val signOut: () -> Unit = { showSignOutConfirm = true }

    LaunchedEffect(Unit) {
        viewModel.refresh()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        when {
            uiState.isLoading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }

            uiState.layoutMode == ProfileLayoutMode.LIST -> {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = topInset,
                        bottom = ScreenContentBottomPadding
                    ),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    item {
                        ProfileHeader(
                            totalCount = uiState.totalCount,
                            layoutMode = uiState.layoutMode,
                            accountCardHidden = accountCardHidden,
                            onBackClick = guardedBackClick,
                            onMenuClick = onMenuClick,
                            onRefresh = viewModel::refresh,
                            onLayoutMode = viewModel::setLayoutMode,
                            onShowAccount = { setAccountCardHidden(false) }
                        )
                    }
                    item {
                        AnimatedVisibility(
                            visible = !accountCardHidden,
                            enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                            exit = fadeOut(animationSpec = tween(140)) + shrinkVertically(animationSpec = tween(220))
                        ) {
                            ProfileAccountCard(
                                uiState = uiState,
                                onEmailSignIn = { showEmailDialog = true },
                                onBackup = viewModel::backupProfile,
                                onRestore = viewModel::restoreProfile,
                                onSignOut = signOut,
                                onChangeAvatar = { avatarPicker.launch(arrayOf("image/*")) },
                                onHide = { setAccountCardHidden(true) },
                                onDismissMessage = viewModel::clearCloudMessage
                            )
                        }
                    }
                    if (uiState.totalCount == 0) {
                        item { ProfileEmptyState() }
                    }
                    if (uiState.favoriteGames.isNotEmpty()) {
                        item { ProfileSectionTitle(title = stringResource(R.string.profile_status_favorites), count = uiState.favoriteGames.size) }
                        items(uiState.favoriteGames, key = { "favorite-${it.profile.igdbId}" }) { game ->
                            ProfileListGameCard(
                                game = game,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                    uiState.visibleStatuses.forEach { status ->
                        val games = uiState.gamesByStatus[status].orEmpty()
                        item { ProfileSectionTitle(title = stringResource(status.titleResId()), count = games.size) }
                        items(games, key = { "${status.name}-${it.profile.igdbId}" }) { game ->
                            ProfileListGameCard(
                                game = game,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                }
            }

            else -> {
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = if (useDenseCards) 128.dp else 154.dp),
                    state = gridState,
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = topInset,
                        bottom = ScreenContentBottomPadding
                    ),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        ProfileHeader(
                            totalCount = uiState.totalCount,
                            layoutMode = uiState.layoutMode,
                            accountCardHidden = accountCardHidden,
                            onBackClick = guardedBackClick,
                            onMenuClick = onMenuClick,
                            onRefresh = viewModel::refresh,
                            onLayoutMode = viewModel::setLayoutMode,
                            onShowAccount = { setAccountCardHidden(false) }
                        )
                    }
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        AnimatedVisibility(
                            visible = !accountCardHidden,
                            enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                            exit = fadeOut(animationSpec = tween(140)) + shrinkVertically(animationSpec = tween(220))
                        ) {
                            ProfileAccountCard(
                                uiState = uiState,
                                onEmailSignIn = { showEmailDialog = true },
                                onBackup = viewModel::backupProfile,
                                onRestore = viewModel::restoreProfile,
                                onSignOut = signOut,
                                onChangeAvatar = { avatarPicker.launch(arrayOf("image/*")) },
                                onHide = { setAccountCardHidden(true) },
                                onDismissMessage = viewModel::clearCloudMessage
                            )
                        }
                    }
                    if (uiState.totalCount == 0) {
                        item(span = { GridItemSpan(maxLineSpan) }) { ProfileEmptyState() }
                    }
                    if (uiState.favoriteGames.isNotEmpty()) {
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            ProfileSectionTitle(title = stringResource(R.string.profile_status_favorites), count = uiState.favoriteGames.size)
                        }
                        items(uiState.favoriteGames, key = { "favorite-${it.profile.igdbId}" }) { game ->
                            ProfileGridGameCard(
                                game = game,
                                compact = useDenseCards,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                    uiState.visibleStatuses.forEach { status ->
                        val games = uiState.gamesByStatus[status].orEmpty()
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            ProfileSectionTitle(title = stringResource(status.titleResId()), count = games.size)
                        }
                        items(games, key = { "${status.name}-${it.profile.igdbId}" }) { game ->
                            ProfileGridGameCard(
                                game = game,
                                compact = useDenseCards,
                                onClick = { onGameClick(game.catalog.igdbId) },
                                onStatus = { viewModel.setGameStatus(game.catalog.igdbId, it) },
                                onClearStatus = { viewModel.clearGameStatus(game.catalog.igdbId) },
                                onFavorite = { viewModel.setFavorite(game.catalog.igdbId, it) },
                                onRemove = { viewModel.removeGame(game.catalog.igdbId) }
                            )
                        }
                    }
                }
            }
        }
    }
    if (showEmailDialog) {
        ProfileEmailDialog(
            onDismiss = { showEmailDialog = false },
            onSubmit = { email, password, displayName, createAccount ->
                showEmailDialog = false
                viewModel.signInWithEmail(email, password, displayName, createAccount)
            }
        )
    }
    if (showSignOutConfirm) {
        ProfileSignOutDialog(
            onDismiss = { showSignOutConfirm = false },
            onConfirm = {
                showSignOutConfirm = false
                viewModel.signOut()
            }
        )
    }
}

@Composable
private fun ProfileHeader(
    totalCount: Int,
    layoutMode: ProfileLayoutMode,
    accountCardHidden: Boolean,
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onRefresh: () -> Unit,
    onLayoutMode: (ProfileLayoutMode) -> Unit,
    onShowAccount: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (onMenuClick != null) {
            NavigationMenuButton(onClick = onMenuClick)
        } else {
            NavigationBackButton(onClick = onBackClick)
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = stringResource(R.string.profile_title),
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = stringResource(R.string.profile_game_count, totalCount),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        AnimatedVisibility(
            visible = accountCardHidden,
            enter = fadeIn(animationSpec = tween(160)),
            exit = fadeOut(animationSpec = tween(120))
        ) {
            IconButton(onClick = onShowAccount) {
                Icon(Icons.Rounded.AccountCircle, contentDescription = stringResource(R.string.profile_show_backup))
            }
        }
        IconButton(onClick = onRefresh) {
            Icon(Icons.Rounded.Refresh, contentDescription = stringResource(R.string.profile_refresh))
        }
        IconButton(
            onClick = {
                onLayoutMode(
                    if (layoutMode == ProfileLayoutMode.GRID) ProfileLayoutMode.LIST else ProfileLayoutMode.GRID
                )
            }
        ) {
            Icon(
                imageVector = if (layoutMode == ProfileLayoutMode.GRID) Icons.AutoMirrored.Rounded.ViewList else Icons.Rounded.ViewModule,
                contentDescription = stringResource(R.string.profile_toggle_layout)
            )
        }
    }
}

@Composable
private fun ProfileSectionTitle(title: String, count: Int) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = stringResource(R.string.profile_section_count, count),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun ProfileAccountCard(
    uiState: ProfileUiState,
    onEmailSignIn: () -> Unit,
    onBackup: () -> Unit,
    onRestore: () -> Unit,
    onSignOut: () -> Unit,
    onChangeAvatar: () -> Unit,
    onHide: () -> Unit,
    onDismissMessage: () -> Unit
) {
    var showBackupActions by remember { mutableStateOf(false) }
    val signedInEmail = uiState.account.email.orEmpty()
    val distinctDisplayName = uiState.account.displayName
        ?.takeIf { it.isNotBlank() && it != signedInEmail }
    val accountTitle = if (uiState.account.isSignedIn) {
        distinctDisplayName ?: stringResource(R.string.profile_account_signed_in)
    } else {
        stringResource(R.string.profile_account_title)
    }
    val accountBody = if (uiState.account.isSignedIn) {
        signedInEmail.ifBlank { stringResource(R.string.profile_account_signed_in) }
    } else {
        stringResource(R.string.profile_account_body)
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 22.dp, vertical = 22.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(18.dp)
        ) {
            Box(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.align(Alignment.Center),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ProfileAvatar(
                        imageUrl = uiState.account.photoUrl,
                        fallbackLabel = accountTitle,
                        contentDescription = accountTitle,
                        onClick = if (uiState.account.isSignedIn && !uiState.isCloudBusy) onChangeAvatar else null,
                        modifier = Modifier.size(136.dp)
                    )
                    if (uiState.account.isSignedIn) {
                        Surface(
                            onClick = onChangeAvatar,
                            enabled = !uiState.isCloudBusy,
                            shape = RoundedCornerShape(999.dp),
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.64f),
                            contentColor = MaterialTheme.colorScheme.primary,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.14f))
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Rounded.PhotoCamera,
                                    contentDescription = null,
                                    modifier = Modifier.size(16.dp)
                                )
                                Text(
                                    text = stringResource(R.string.profile_change_avatar),
                                    style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                                    maxLines = 1
                                )
                            }
                        }
                    }
                }
                if (uiState.account.isSignedIn) {
                    ProfileRoundAction(
                        icon = Icons.AutoMirrored.Rounded.Logout,
                        contentDescription = stringResource(R.string.profile_sign_out),
                        onClick = onSignOut,
                        enabled = !uiState.isCloudBusy,
                        modifier = Modifier.align(Alignment.TopEnd),
                        destructive = true
                    )
                    ProfileRoundAction(
                        icon = Icons.Rounded.VisibilityOff,
                        contentDescription = stringResource(R.string.profile_hide_backup),
                        onClick = onHide,
                        enabled = !uiState.isCloudBusy,
                        modifier = Modifier.align(Alignment.TopStart)
                    )
                    ProfileRoundAction(
                        icon = Icons.Rounded.MoreVert,
                        contentDescription = stringResource(R.string.profile_account_title),
                        onClick = { showBackupActions = true },
                        enabled = !uiState.isCloudBusy,
                        modifier = Modifier.align(Alignment.BottomEnd)
                    )
                } else {
                    ProfileRoundAction(
                        icon = Icons.Rounded.VisibilityOff,
                        contentDescription = stringResource(R.string.profile_hide_backup),
                        onClick = onHide,
                        enabled = !uiState.isCloudBusy,
                        modifier = Modifier.align(Alignment.TopEnd)
                    )
                }
            }
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = accountTitle,
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    text = accountBody,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = if (uiState.account.isSignedIn) 1 else 3,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
            }
            ProfileAccountActions(
                isSignedIn = uiState.account.isSignedIn,
                isBusy = uiState.isCloudBusy,
                onEmailSignIn = onEmailSignIn,
                onBackup = onBackup,
                onRestore = onRestore
            )
            uiState.cloudMessage?.let { message ->
                Surface(
                    modifier = Modifier.widthIn(max = 360.dp),
                    shape = RoundedCornerShape(999.dp),
                    color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.30f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)),
                    onClick = onDismissMessage
                ) {
                    Text(
                        text = message,
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        textAlign = TextAlign.Center
                    )
                }
            }
        }
    }
    if (showBackupActions) {
        ProfileBackupActionsDialog(
            onDismiss = { showBackupActions = false },
            onBackup = {
                showBackupActions = false
                onBackup()
            },
            onRestore = {
                showBackupActions = false
                onRestore()
            }
        )
    }
}

@Composable
private fun ProfileAvatar(
    imageUrl: String?,
    fallbackLabel: String,
    contentDescription: String,
    onClick: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    val shape = RoundedCornerShape(42.dp)
    val color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    val border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
    val content: @Composable () -> Unit = {
        if (imageUrl != null) {
            UrlImage(
                imageUrl = imageUrl,
                contentDescription = contentDescription,
                fallbackLabel = fallbackLabel,
                modifier = Modifier.fillMaxSize(),
                pinInMemory = true
            )
        } else {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    imageVector = Icons.Rounded.AccountCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(70.dp)
                )
            }
        }
    }
    if (onClick != null) {
        Surface(
            onClick = onClick,
            modifier = modifier,
            shape = shape,
            color = color,
            border = border
        ) {
            content()
        }
    } else {
        Surface(
            modifier = modifier,
            shape = shape,
            color = color,
            border = border
        ) {
            content()
        }
    }
}

@Composable
private fun ProfileAccountActions(
    isSignedIn: Boolean,
    isBusy: Boolean,
    onEmailSignIn: () -> Unit,
    onBackup: () -> Unit,
    onRestore: () -> Unit
) {
    if (isSignedIn) {
        return
    } else {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            ProfileSignInAction(
                enabled = !isBusy,
                onClick = onEmailSignIn
            )
        }
    }
}

@Composable
private fun ProfileBackupAction(
    icon: ImageVector,
    label: String,
    onClick: () -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    prominent: Boolean = false
) {
    val containerColor = if (prominent) {
        MaterialTheme.colorScheme.primary.copy(alpha = 0.95f)
    } else {
        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.46f)
    }
    val contentColor = if (prominent) {
        MaterialTheme.colorScheme.onPrimary
    } else {
        MaterialTheme.colorScheme.primary
    }
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.heightIn(min = 58.dp),
        shape = RoundedCornerShape(20.dp),
        color = if (enabled) containerColor else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
        contentColor = if (enabled) contentColor else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.62f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                modifier = Modifier.size(20.dp)
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun ProfileSignInAction(
    enabled: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.widthIn(min = 176.dp),
        shape = RoundedCornerShape(22.dp),
        color = if (enabled) {
            MaterialTheme.colorScheme.primary
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
        },
        contentColor = if (enabled) {
            MaterialTheme.colorScheme.onPrimary
        } else {
            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.62f)
        }
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 22.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Icon(
                imageVector = Icons.Rounded.Email,
                contentDescription = null,
                modifier = Modifier.size(23.dp)
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text(
                text = stringResource(R.string.profile_sign_in),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                maxLines = 1
            )
        }
    }
}

@Composable
private fun ProfileRoundAction(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    prominent: Boolean = false,
    destructive: Boolean = false
) {
    val contentColor = when {
        destructive -> MaterialTheme.colorScheme.error
        prominent -> MaterialTheme.colorScheme.onPrimary
        else -> MaterialTheme.colorScheme.primary
    }
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.size(38.dp),
        shape = RoundedCornerShape(999.dp),
        color = Color.Transparent,
        contentColor = if (enabled) contentColor else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.62f)
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = icon,
                contentDescription = contentDescription,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
private fun ProfileEmptyState() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(62.dp),
                shape = RoundedCornerShape(22.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.10f))
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Person,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(34.dp)
                    )
                }
            }
            Text(
                text = stringResource(R.string.profile_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            Text(
                text = stringResource(R.string.profile_empty_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 360.dp)
            )
        }
    }
}

@Composable
private fun ProfileEmailDialog(
    onDismiss: () -> Unit,
    onSubmit: (String, String, String, Boolean) -> Unit
) {
    var email by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var displayName by rememberSaveable { mutableStateOf("") }
    var createAccount by rememberSaveable { mutableStateOf(false) }
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            val scrollState = rememberScrollState()

            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 620.dp)
                    .heightIn(max = maxHeight),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 10.dp,
                shadowElevation = 18.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .verticalScroll(scrollState)
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(18.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(48.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.Rounded.Email,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(26.dp)
                                )
                            }
                        }
                        Spacer(modifier = Modifier.width(10.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.profile_account_title),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = if (createAccount) {
                                    stringResource(R.string.profile_create_account)
                                } else {
                                    stringResource(R.string.profile_sign_in)
                                },
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.common_close))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    AnimatedContent(
                        targetState = createAccount,
                        transitionSpec = {
                            fadeIn(animationSpec = tween(180)) togetherWith fadeOut(animationSpec = tween(120))
                        },
                        label = "profile_email_mode_body"
                    ) { isCreateMode ->
                        Text(
                            text = if (isCreateMode) {
                                stringResource(R.string.profile_email_create_body)
                            } else {
                                stringResource(R.string.profile_email_sign_in_body)
                            },
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.fillMaxWidth()
                        )
                    }

                    OutlinedTextField(
                        value = email,
                        onValueChange = { email = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text(stringResource(R.string.profile_email)) },
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp)
                    )
                    AnimatedVisibility(
                        visible = createAccount,
                        enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(180)),
                        exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(160))
                    ) {
                        OutlinedTextField(
                            value = displayName,
                            onValueChange = { displayName = it },
                            modifier = Modifier.fillMaxWidth(),
                            label = { Text(stringResource(R.string.profile_user_name)) },
                            singleLine = true,
                            shape = RoundedCornerShape(18.dp)
                        )
                    }
                    OutlinedTextField(
                        value = password,
                        onValueChange = { password = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text(stringResource(R.string.profile_password)) },
                        visualTransformation = PasswordVisualTransformation(),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp)
                    )

                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Button(
                            onClick = { onSubmit(email, password, displayName, createAccount) },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = if (createAccount) {
                                    stringResource(R.string.profile_create_account)
                                } else {
                                    stringResource(R.string.profile_sign_in)
                                }
                            )
                        }
                        OutlinedButton(
                            onClick = { createAccount = !createAccount },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = if (createAccount) {
                                    stringResource(R.string.profile_sign_in)
                                } else {
                                    stringResource(R.string.profile_create_account)
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileSignOutDialog(
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 520.dp),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 10.dp,
                shadowElevation = 18.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(52.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.65f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.AutoMirrored.Rounded.Logout,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.error,
                                    modifier = Modifier.size(26.dp)
                                )
                            }
                        }
                        Spacer(modifier = Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.profile_sign_out),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.error
                            )
                            Text(
                                text = stringResource(R.string.profile_sign_out_title),
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.common_close))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    Text(
                        text = stringResource(R.string.profile_sign_out_body),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        OutlinedButton(
                            onClick = onDismiss,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.common_cancel))
                        }
                        Button(
                            onClick = onConfirm,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.profile_sign_out_confirm))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileBackupActionsDialog(
    onDismiss: () -> Unit,
    onBackup: () -> Unit,
    onRestore: () -> Unit
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 520.dp),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 10.dp,
                shadowElevation = 18.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(52.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.Rounded.CloudUpload,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(26.dp)
                                )
                            }
                        }
                        Spacer(modifier = Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.profile_account_title),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = stringResource(R.string.profile_account_actions_title),
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.common_close))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        ProfileBackupAction(
                            icon = Icons.Rounded.CloudUpload,
                            label = stringResource(R.string.profile_backup),
                            onClick = onBackup,
                            enabled = true,
                            prominent = true,
                            modifier = Modifier.weight(1f)
                        )
                        ProfileBackupAction(
                            icon = Icons.Rounded.CloudDownload,
                            label = stringResource(R.string.profile_restore),
                            onClick = onRestore,
                            enabled = true,
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileGameCardMenuDialog(
    game: ProfileCatalogGame,
    onDismiss: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit
) {
    val profileStatus = game.profile.status
    val isFavorite = game.profile.isFavorite
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            val scrollState = rememberScrollState()
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 620.dp)
                    .heightIn(max = maxHeight),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 10.dp,
                shadowElevation = 18.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .verticalScroll(scrollState)
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(52.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                        ) {
                            UrlImage(
                                imageUrl = game.catalog.coverUrl,
                                contentDescription = game.catalog.name,
                                fallbackLabel = game.catalog.name,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        Spacer(modifier = Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.catalog_profile_menu),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = game.catalog.name,
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.common_close))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    ProfileMenuOption(
                        text = stringResource(R.string.profile_status_favorites),
                        icon = if (isFavorite) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                        selected = isFavorite,
                        onClick = {
                            onFavorite(!isFavorite)
                            onDismiss()
                        }
                    )

                    Text(
                        text = stringResource(R.string.catalog_profile_status_title),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    ProfileGameStatus.entries.forEach { status ->
                        ProfileMenuOption(
                            text = stringResource(status.titleResId()),
                            selected = profileStatus == status,
                            onClick = { onStatus(status) }
                        )
                    }

                    if (profileStatus != null) {
                        OutlinedButton(
                            onClick = onClearStatus,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.catalog_profile_clear_status))
                        }
                    }

                    TextButton(
                        onClick = onRemove,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = stringResource(R.string.catalog_profile_remove),
                            color = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileMenuOption(
    text: String,
    selected: Boolean,
    onClick: () -> Unit,
    icon: ImageVector? = null
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(22.dp)
                )
                Spacer(modifier = Modifier.width(12.dp))
            }
            Text(
                text = text,
                style = MaterialTheme.typography.titleMedium.copy(
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal
                ),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            if (selected) {
                Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            }
        }
    }
}

@Composable
private fun ProfileGridGameCard(
    game: ProfileCatalogGame,
    compact: Boolean,
    onClick: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier
) {
    val openClick = rememberDebouncedClick(onClick = onClick)
    var showProfileMenu by remember { mutableStateOf(false) }
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = openClick
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f))
            ) {
                UrlImage(
                    imageUrl = game.catalog.coverUrl,
                    contentDescription = game.catalog.name,
                    fallbackLabel = game.catalog.name,
                    modifier = Modifier.fillMaxSize()
                )
                IconButton(
                    onClick = { showProfileMenu = true },
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(4.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.MoreVert,
                        contentDescription = stringResource(R.string.catalog_profile_menu),
                        tint = Color.White
                    )
                }
            }
            ProfileGameText(
                game = game,
                compact = compact,
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 76.dp else 84.dp)
                    .padding(
                        horizontal = if (compact) CompactCardContentPadding else CardContentPadding,
                        vertical = if (compact) 8.dp else 10.dp
                    )
            )
        }
    }
    if (showProfileMenu) {
        ProfileGameCardMenuDialog(
            game = game,
            onDismiss = { showProfileMenu = false },
            onStatus = { status ->
                showProfileMenu = false
                onStatus(status)
            },
            onClearStatus = {
                showProfileMenu = false
                onClearStatus()
            },
            onFavorite = onFavorite,
            onRemove = {
                showProfileMenu = false
                onRemove()
            }
        )
    }
}

@Composable
private fun ProfileListGameCard(
    game: ProfileCatalogGame,
    onClick: () -> Unit,
    onStatus: (ProfileGameStatus) -> Unit,
    onClearStatus: () -> Unit,
    onFavorite: (Boolean) -> Unit,
    onRemove: () -> Unit
) {
    val openClick = rememberDebouncedClick(onClick = onClick)
    var showProfileMenu by remember { mutableStateOf(false) }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = openClick
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(width = 58.dp, height = 86.dp),
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
            ) {
                UrlImage(
                    imageUrl = game.catalog.coverUrl,
                    contentDescription = game.catalog.name,
                    fallbackLabel = game.catalog.name,
                    modifier = Modifier.fillMaxSize()
                )
            }
            ProfileGameText(
                game = game,
                compact = false,
                modifier = Modifier.weight(1f)
            )
            IconButton(onClick = { showProfileMenu = true }) {
                Icon(Icons.Rounded.MoreVert, contentDescription = stringResource(R.string.catalog_profile_menu))
            }
        }
    }
    if (showProfileMenu) {
        ProfileGameCardMenuDialog(
            game = game,
            onDismiss = { showProfileMenu = false },
            onStatus = { status ->
                showProfileMenu = false
                onStatus(status)
            },
            onClearStatus = {
                showProfileMenu = false
                onClearStatus()
            },
            onFavorite = onFavorite,
            onRemove = {
                showProfileMenu = false
                onRemove()
            }
        )
    }
}

@Composable
private fun ProfileGameText(
    game: ProfileCatalogGame,
    compact: Boolean,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Text(
            text = game.catalog.name,
            style = if (compact) {
                MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium)
            } else {
                MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Medium)
            },
            minLines = 2,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.onSurface
        )
        Text(
            text = buildMeta(game),
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (game.catalog.genres.isNotEmpty()) {
            Text(
                text = game.catalog.genres.take(2).joinToString(" • "),
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

private fun buildMeta(game: ProfileCatalogGame): String {
    val parts = mutableListOf<String>()
    game.catalog.year?.let { parts += it.toString() }
    game.catalog.rating?.let { parts += String.format(Locale.US, "%.1f", it / 10f) }
    return parts.joinToString(" • ")
}

private fun ProfileGameStatus.titleResId(): Int {
    return when (this) {
        ProfileGameStatus.PLAYING -> R.string.profile_status_playing
        ProfileGameStatus.WANT_TO_PLAY -> R.string.profile_status_want_to_play
        ProfileGameStatus.COMPLETED -> R.string.profile_status_completed
        ProfileGameStatus.DROPPED -> R.string.profile_status_dropped
    }
}
