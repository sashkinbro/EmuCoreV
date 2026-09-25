package com.sbro.emucorev.ui.profile

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Login
import androidx.compose.material.icons.automirrored.rounded.ViewList
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.BarChart
import androidx.compose.material.icons.rounded.Block
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CloudSync
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Devices
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Email
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.Leaderboard
import androidx.compose.material.icons.rounded.LockOpen
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Notifications
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.PersonAdd
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.WorkspacePremium
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.CloudEmulatorProfile
import com.sbro.emucorev.data.PlayerActivityDay
import com.sbro.emucorev.data.PlayerDevice
import com.sbro.emucorev.data.PlayerGamePlayStat
import com.sbro.emucorev.data.PlayerLeaderboardEntry
import com.sbro.emucorev.data.PlayerProfile
import com.sbro.emucorev.data.PlayerRankInsights
import com.sbro.emucorev.data.PlayerTrophyStat
import com.sbro.emucorev.data.PlayerTrophySummary
import com.sbro.emucorev.data.PublicPlayerDevice
import com.sbro.emucorev.data.ProfileFeedEvent
import com.sbro.emucorev.data.ProfileFriendship
import com.sbro.emucorev.data.FriendshipStatus
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.UrlImage
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonShapeCorners
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

private enum class ProfileTab {
    Overview,
    Games,
    Achievements,
    Leaderboard,
    Stats
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ProfileScreen(
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onOpenGameDetails: (Long) -> Unit,
    onOpenMyLists: () -> Unit,
    onOpenTrophies: (String?) -> Unit,
    viewModel: ProfileViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val selectedTab = rememberSaveable { mutableIntStateOf(0) }
    val mainListState = rememberLazyListState()
    var showProCustomization by rememberSaveable { mutableStateOf(false) }
    var showDevices by rememberSaveable { mutableStateOf(false) }
    var showCloudProfiles by rememberSaveable { mutableStateOf(false) }
    var showNotifications by rememberSaveable { mutableStateOf(false) }
    var showFriends by rememberSaveable { mutableStateOf(false) }
    var showBlockedPlayers by rememberSaveable { mutableStateOf(false) }
    var trophyFilter by rememberSaveable { mutableStateOf("all") }
    val isViewingLeaderboardProfile = uiState.viewedProfile != null || uiState.isViewedProfileLoading

    if (showProCustomization && uiState.profile != null) {
        ProProfileCustomizationDialog(
            profile = uiState.profile!!,
            games = uiState.games,
            onDismiss = { showProCustomization = false },
            onSave = { accent, favoriteGameKeys ->
                showProCustomization = false
                viewModel.updateProProfile(accent, favoriteGameKeys)
            }
        )
    }

    if (showDevices) {
        ProfileDevicesDialog(
            devices = uiState.devices,
            isLoading = uiState.isFeatureActionLoading,
            onSetPublic = viewModel::setDevicePublic,
            onDelete = viewModel::deleteDevice,
            onDismiss = { showDevices = false }
        )
    }

    if (showCloudProfiles) {
        CloudProfilesDialog(
            profiles = uiState.cloudProfiles,
            isLoading = uiState.isFeatureActionLoading,
            onSave = viewModel::saveCloudProfile,
            onRestore = viewModel::restoreCloudProfile,
            onDelete = viewModel::deleteCloudProfile,
            onDismiss = { showCloudProfiles = false }
        )
    }

    if (showNotifications) {
        ProfileNotificationsDialog(
            friendships = uiState.friendships,
            feed = uiState.feed,
            profiles = uiState.socialProfiles,
            isLoading = uiState.isFeatureActionLoading,
            onAccept = viewModel::acceptFriendRequest,
            onOpenProfile = { uid ->
                showNotifications = false
                viewModel.viewSocialProfile(uid)
            },
            onDismiss = { showNotifications = false }
        )
    }

    if (showFriends) {
        ProfileFriendsDialog(
            friendships = uiState.friendships,
            profiles = uiState.socialProfiles,
            isLoading = uiState.isFeatureActionLoading,
            onRemove = viewModel::removeFriendship,
            onOpenProfile = { uid ->
                showFriends = false
                viewModel.viewSocialProfile(uid)
            },
            onDismiss = { showFriends = false }
        )
    }

    if (showBlockedPlayers) {
        ProfileBlockedPlayersDialog(
            blockedUids = uiState.blockedUids,
            profiles = uiState.socialProfiles,
            isLoading = uiState.isFeatureActionLoading,
            onUnblock = viewModel::unblockPlayer,
            onDismiss = { showBlockedPlayers = false }
        )
    }

    BackHandler(enabled = isViewingLeaderboardProfile) {
        viewModel.closeViewedProfile()
    }

    LaunchedEffect(uiState.messageKey, uiState.errorMessage) {
        uiState.messageKey?.let { key ->
            Toast.makeText(context, profileMessageRes(key), Toast.LENGTH_SHORT).show()
            viewModel.clearTransientMessages()
        }
        uiState.errorMessage?.let { message ->
            Toast.makeText(context, message, Toast.LENGTH_LONG).show()
            viewModel.clearTransientMessages()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        if (uiState.account == null) {
            AuthContent(
                isLoading = uiState.isAuthLoading,
                topInset = topInset,
                onBackClick = onBackClick,
                onMenuClick = onMenuClick,
                onSignIn = viewModel::signIn,
                onCreateAccount = viewModel::createAccount,
                onResetPassword = viewModel::sendPasswordReset,
                onGoogleSignIn = {
                    context.findActivity()?.let(viewModel::signInWithGoogle)
                        ?: Toast.makeText(context, R.string.profile_google_failed, Toast.LENGTH_SHORT).show()
                },
                modifier = Modifier.weight(1f)
            )
        } else {
            val tabs = ProfileTab.entries
            LaunchedEffect(uiState.account?.uid, selectedTab.intValue) {
                viewModel.onProfileTabSelected(tabs[selectedTab.intValue].name)
            }
            if (isViewingLeaderboardProfile) {
                ViewedPlayerProfile(
                    profile = uiState.viewedProfile,
                    isLoading = uiState.isViewedProfileLoading,
                    isLoadingMoreGames = uiState.isViewedGamesLoadingMore,
                    hasMoreGames = uiState.hasMoreViewedGames,
                    topInset = topInset,
                    onBack = viewModel::closeViewedProfile,
                    onLoadMoreGames = viewModel::loadMoreViewedGames,
                    onGameClick = { game -> viewModel.openGameDetails(game, onOpenGameDetails) },
                    friendship = uiState.friendships.firstOrNull { it.otherUid == uiState.viewedProfile?.uid },
                    isActionLoading = uiState.isFeatureActionLoading,
                    onAddFriend = { uiState.viewedProfile?.uid?.let(viewModel::sendFriendRequest) },
                    onAcceptFriend = viewModel::acceptFriendRequest,
                    onRemoveFriend = viewModel::removeFriendship,
                    onBlock = { uiState.viewedProfile?.uid?.let(viewModel::blockPlayer) },
                    modifier = Modifier.weight(1f)
                )
            } else {
                Box(modifier = Modifier.weight(1f)) {
                    LazyColumn(
                        state = mainListState,
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = PaddingValues(
                            start = ScreenHorizontalPadding,
                            end = ScreenHorizontalPadding,
                            top = topInset,
                            bottom = bottomInset + 110.dp
                        ),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        item {
                            ScreenTopBar(
                                title = stringResource(R.string.profile_title),
                                onBackClick = if (onMenuClick != null) null else onBackClick,
                                onMenuClick = onMenuClick,
                                actions = {
                                    ProfileNotificationButton(
                                        pendingCount = uiState.friendships.count {
                                            it.status == FriendshipStatus.PendingIncoming
                                        },
                                        onClick = { showNotifications = true }
                                    )
                                }
                            )
                        }
                        when (tabs[selectedTab.intValue]) {
                            ProfileTab.Overview -> {
                                item {
                                    RevealOnEnter(revealKey = "overview-${uiState.account?.uid}") {
                                        ProfileOverview(
                                            profile = uiState.profile,
                                            isLoading = uiState.isProfileLoading,
                                            photoURL = uiState.account?.photoURL,
                                            email = uiState.account?.email,
                                            isActionLoading = uiState.isAuthLoading,
                                            isProUnlocked = uiState.isProUnlocked,
                                            rankInsights = uiState.rankInsights,
                                            trophySummary = uiState.trophies,
                                            onCustomizePro = { showProCustomization = true },
                                            onShareCard = {
                                                uiState.profile?.let { profile ->
                                                    PlayerCardSharer.share(context, profile, uiState.rankInsights)
                                                }
                                            },
                                            onUpdateName = viewModel::updateDisplayName,
                                            onSignOut = { viewModel.signOut() }
                                        )
                                    }
                                }
                                item {
                                    MyListsActionCard(onClick = onOpenMyLists)
                                }
                                item {
                                    ProfileFeatureHubCard(
                                        deviceCount = uiState.devices.size,
                                        cloudProfileCount = uiState.cloudProfiles.size,
                                        friendCount = uiState.friendships.count { it.status == FriendshipStatus.Accepted },
                                        blockedCount = uiState.blockedUids.size,
                                        onDevices = { showDevices = true },
                                        onCloudProfiles = { showCloudProfiles = true },
                                        onFriends = { showFriends = true },
                                        onBlocked = { showBlockedPlayers = true }
                                    )
                                }
                                if (uiState.isProfileLoading && uiState.profile == null) {
                                    item { RecentGamesSkeletonCard() }
                                } else {
                                    uiState.profile?.games
                                        .orEmpty()
                                        .filter { (it.lastPlayedAtMs ?: 0L) > 0L }
                                        .sortedByDescending { it.lastPlayedAtMs ?: 0L }
                                        .take(8)
                                        .takeIf { it.isNotEmpty() }
                                        ?.let { recentGames ->
                                            item {
                                                RevealOnEnter(revealKey = "recent-${recentGames.first().gameKey}") {
                                                    RecentGamesCard(
                                                        games = recentGames,
                                                        onGameClick = { game -> viewModel.openGameDetails(game, onOpenGameDetails) }
                                                    )
                                                }
                                            }
                                        }
                                }
                            }

                            ProfileTab.Games -> {
                                val showGamesSkeleton = uiState.isProfileLoading && uiState.games.isEmpty()
                                if (showGamesSkeleton) {
                                    items(4, key = { "game-skeleton-$it" }) { GamePlayStatSkeletonRow() }
                                } else if (uiState.games.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_games_empty)) }
                                } else {
                                    items(uiState.games, key = { it.gameKey }) { game ->
                                        GamePlayStatRow(
                                            game = game,
                                            onClick = { viewModel.openGameDetails(game, onOpenGameDetails) }
                                        )
                                    }
                                }
                            }

                            ProfileTab.Achievements -> {
                                item {
                                    TrophySummaryCard(uiState.trophies)
                                }
                                item {
                                    TrophyFilterRow(trophyFilter) { trophyFilter = it }
                                }
                                if (!uiState.hasLoadedTrophies && uiState.trophies.sets.isEmpty()) {
                                    items(4, key = { "trophy-skeleton-$it" }) { GamePlayStatSkeletonRow() }
                                } else {
                                    val visibleSets = when (trophyFilter) {
                                        "completed" -> uiState.trophies.sets.filter { it.completed }
                                        "in_progress" -> uiState.trophies.sets.filter { it.unlockedCount > 0 && !it.completed }
                                        else -> uiState.trophies.sets
                                    }
                                    if (visibleSets.isEmpty()) {
                                        item { EmptyProfileState(text = stringResource(R.string.profile_trophies_empty)) }
                                    } else {
                                        items(visibleSets, key = { it.communicationId }) { stat ->
                                            TrophySetRow(
                                                stat = stat,
                                                onClick = { onOpenTrophies(stat.titleId) }
                                            )
                                        }
                                    }
                                }
                            }

                            ProfileTab.Leaderboard -> {
                                item {
                                    RevealOnEnter(revealKey = "leaderboard-search") {
                                        PlayerSearchField(
                                            query = uiState.leaderboardSearchQuery,
                                            isLoading = uiState.isPlayerSearchLoading,
                                            onQueryChange = viewModel::updatePlayerSearch,
                                            onRefresh = viewModel::refreshLeaderboard
                                        )
                                    }
                                }
                                val isSearching = uiState.leaderboardSearchQuery.trim().length >= 2
                                val visibleEntries = if (isSearching) uiState.searchResults else uiState.leaderboard
                                val showLeaderboardSkeleton = visibleEntries.isEmpty() && (
                                    if (isSearching) uiState.isPlayerSearchLoading
                                    else uiState.isLeaderboardLoading || !uiState.hasLoadedLeaderboard
                                )
                                if (showLeaderboardSkeleton) {
                                    items(if (isSearching) 3 else 6, key = { "leaderboard-skeleton-$it" }) {
                                        LeaderboardRowSkeleton()
                                    }
                                } else if (isSearching && !uiState.isPlayerSearchLoading && visibleEntries.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_leaderboard_no_results)) }
                                } else if (!isSearching && visibleEntries.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_leaderboard_empty)) }
                                } else {
                                    items(visibleEntries, key = { it.uid }) { entry ->
                                        LeaderboardRow(
                                            entry = entry,
                                            currentUid = uiState.account?.uid,
                                            onClick = {
                                                if (entry.uid == uiState.account?.uid) {
                                                    selectedTab.intValue = ProfileTab.Overview.ordinal
                                                    viewModel.onProfileTabSelected(ProfileTab.Overview.name)
                                                } else {
                                                    viewModel.viewLeaderboardProfile(entry)
                                                }
                                            }
                                        )
                                    }
                                }
                                if (!isSearching && uiState.hasMoreLeaderboard && uiState.leaderboard.isNotEmpty()) {
                                    item {
                                        LaunchedEffect(uiState.leaderboard.size) { viewModel.loadMoreLeaderboard() }
                                        LeaderboardRowSkeleton()
                                    }
                                }
                            }

                            ProfileTab.Stats -> {
                                item {
                                    RevealOnEnter(revealKey = "profile-stats-${uiState.account?.uid}") {
                                        AdvancedStatsContent(
                                            isProUnlocked = uiState.isProUnlocked,
                                            isLoading = uiState.isActivityLoading,
                                            hasAttemptedLoad = uiState.hasAttemptedActivityLoad,
                                            isProfileLoading = uiState.isProfileLoading,
                                            activity = uiState.activity,
                                            rankInsights = uiState.rankInsights,
                                            profile = uiState.profile,
                                            onRefresh = viewModel::loadActivity
                                        )
                                    }
                                }
                            }
                        }
                    }
                    ProfileBottomNav(
                        tabs = tabs,
                        selectedIndex = selectedTab.intValue,
                        onSelect = {
                            selectedTab.intValue = it
                            viewModel.onProfileTabSelected(tabs[it].name)
                        },
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .padding(horizontal = ScreenHorizontalPadding)
                            .padding(bottom = bottomInset + 12.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun ProfileBottomNav(
    tabs: List<ProfileTab>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            tabs.forEachIndexed { index, tab ->
                val selected = selectedIndex == index
                Surface(
                    onClick = { onSelect(index) },
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp),
                    shape = neonShape(22.dp),
                    color = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.16f) else Color.Transparent
                ) {
                    Column(
                        modifier = Modifier.fillMaxSize(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Icon(
                            imageVector = tab.icon(),
                            contentDescription = stringResource(tab.titleRes()),
                            tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(26.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AuthContent(
    isLoading: Boolean,
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)?,
    onSignIn: (String, String) -> Unit,
    onCreateAccount: (String, String, String) -> Unit,
    onResetPassword: (String) -> Unit,
    onGoogleSignIn: () -> Unit,
    modifier: Modifier = Modifier
) {
    var email by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var displayName by rememberSaveable { mutableStateOf("") }
    var createMode by rememberSaveable { mutableStateOf(false) }

    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = 28.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.profile_title),
                onBackClick = if (onMenuClick != null) null else onBackClick,
                onMenuClick = onMenuClick
            )
        }
        item {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = neonShape(22.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 2.dp,
                border = profileCardBorder()
            ) {
                Column(
                    modifier = Modifier
                        .padding(18.dp)
                        .animateContentSize(
                            animationSpec = tween(
                                durationMillis = 260,
                                easing = FastOutSlowInEasing
                            )
                        ),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Text(
                        text = stringResource(R.string.profile_auth_title),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    AnimatedVisibility(
                        visible = createMode,
                        enter = fadeIn(animationSpec = tween(160)) + expandVertically(
                            animationSpec = tween(260, easing = FastOutSlowInEasing)
                        ),
                        exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(
                            animationSpec = tween(220, easing = FastOutSlowInEasing)
                        )
                    ) {
                        OutlinedTextField(
                            value = displayName,
                            onValueChange = { displayName = it },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            shape = neonShape(20.dp),
                            label = { Text(stringResource(R.string.profile_display_name)) },
                            leadingIcon = { Icon(Icons.Rounded.Person, contentDescription = null) }
                        )
                    }
                    OutlinedTextField(
                        value = email,
                        onValueChange = { email = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = neonShape(20.dp),
                        label = { Text(stringResource(R.string.profile_email)) },
                        leadingIcon = { Icon(Icons.Rounded.Email, contentDescription = null) }
                    )
                    OutlinedTextField(
                        value = password,
                        onValueChange = { password = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = neonShape(20.dp),
                        label = { Text(stringResource(R.string.profile_password)) },
                        visualTransformation = PasswordVisualTransformation(),
                        leadingIcon = { Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null) }
                    )
                    Button(
                        shape = neonButtonShape(),
                        enabled = !isLoading,
                        onClick = {
                            if (createMode) {
                                onCreateAccount(email, password, displayName)
                            } else {
                                onSignIn(email, password)
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        if (isLoading) {
                            CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                            Spacer(Modifier.width(10.dp))
                        }
                        Text(stringResource(if (createMode) R.string.profile_create_account else R.string.profile_sign_in))
                    }
                    OutlinedButton(
                        shape = neonButtonShape(),
                        enabled = !isLoading,
                        onClick = onGoogleSignIn,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.AccountCircle, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.profile_google_sign_in))
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        TextButton(onClick = { createMode = !createMode }) {
                            Text(stringResource(if (createMode) R.string.profile_have_account else R.string.profile_need_account))
                        }
                        TextButton(enabled = email.isNotBlank() && !isLoading, onClick = { onResetPassword(email) }) {
                            Text(stringResource(R.string.profile_reset_password))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ViewedPlayerProfile(
    profile: PlayerProfile?,
    isLoading: Boolean,
    isLoadingMoreGames: Boolean,
    hasMoreGames: Boolean,
    topInset: androidx.compose.ui.unit.Dp,
    onBack: () -> Unit,
    onLoadMoreGames: () -> Unit,
    onGameClick: (PlayerGamePlayStat) -> Unit,
    friendship: ProfileFriendship?,
    isActionLoading: Boolean,
    onAddFriend: () -> Unit,
    onAcceptFriend: (String) -> Unit,
    onRemoveFriend: (String) -> Unit,
    onBlock: () -> Unit,
    modifier: Modifier = Modifier
) {
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = 18.dp
        ),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.profile_title),
                onBackClick = onBack
            )
        }

        if (isLoading) {
            item { ReadOnlyProfileSkeletonCard() }
            items(3) { GamePlayStatSkeletonRow() }
        } else if (profile == null) {
            item { EmptyProfileState(text = stringResource(R.string.profile_player_not_found)) }
        } else {
            item {
                ReadOnlyProfileCard(profile = profile)
            }
            item {
                PlayerSocialActions(
                    friendship = friendship,
                    isLoading = isActionLoading,
                    onAddFriend = onAddFriend,
                    onAcceptFriend = onAcceptFriend,
                    onRemoveFriend = onRemoveFriend,
                    onBlock = onBlock
                )
            }
            if (profile.games.isNotEmpty()) {
                item {
                    Text(
                        text = stringResource(R.string.profile_tab_games),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onBackground,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
                items(profile.games, key = { it.gameKey }) { game ->
                    GamePlayStatRow(game = game, onClick = { onGameClick(game) })
                }
                if (hasMoreGames) {
                    item {
                        LaunchedEffect(profile.games.size) { onLoadMoreGames() }
                        GamePlayStatSkeletonRow()
                    }
                } else if (isLoadingMoreGames) {
                    item { GamePlayStatSkeletonRow() }
                }
            }
        }
    }
}

@Composable
private fun ReadOnlyProfileCard(profile: PlayerProfile) {
    val accent = profileAccentColor(profile.profileAccent)
    var showDevice by rememberSaveable(profile.uid) { mutableStateOf(false) }
    if (showDevice && profile.publicDevice != null) {
        PublicDeviceDialog(profile.publicDevice, onDismiss = { showDevice = false })
    }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = if (profile.isProMember) BorderStroke(1.dp, accent.copy(alpha = 0.72f)) else profileCardBorder()
    ) {
        Column {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (profile.isProMember) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            ProBadge(accent)
                        }
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            modifier = Modifier
                                .size(72.dp)
                                .clip(CircleShape)
                                .background(
                                    if (profile.isProMember) accent.copy(alpha = 0.22f)
                                    else MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            UrlImage(
                                imageUrl = profile.photoURL,
                                contentDescription = profile.displayName,
                                fallbackLabel = profile.displayName,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = profile.displayName,
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            profile.playerTag.takeIf { it.isNotBlank() }?.let { tag ->
                                Text(
                                    text = tag,
                                    style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                                    color = if (profile.isProMember) accent else MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                        if (profile.publicDevice != null) {
                            IconButton(onClick = { showDevice = true }) {
                                Icon(
                                    imageVector = Icons.Rounded.Devices,
                                    contentDescription = stringResource(R.string.profile_device_show),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                    }
                }

                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatChip(
                        icon = Icons.Rounded.Schedule,
                        label = stringResource(R.string.profile_total_time),
                        value = formatDuration(profile.totalPlayTimeMs),
                        modifier = Modifier.weight(1f)
                    )
                    StatChip(
                        icon = Icons.Rounded.SportsEsports,
                        label = stringResource(R.string.profile_games_played),
                        value = profile.gamesPlayed.toString(),
                        modifier = Modifier.weight(1f)
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatChip(
                        icon = Icons.Rounded.EmojiEvents,
                        label = stringResource(R.string.profile_tab_achievements),
                        value = profile.achievementCount.toString(),
                        modifier = Modifier.weight(1f)
                    )
                    StatChip(
                        icon = Icons.Rounded.Leaderboard,
                        label = stringResource(R.string.settings_ra_points_label),
                        value = profile.achievementPoints.toString(),
                        modifier = Modifier.weight(1f)
                    )
                }
            }
            FavoriteGamesShowcase(
                profile = profile,
                modifier = Modifier.padding(bottom = 16.dp),
                contentPadding = PaddingValues(horizontal = 16.dp)
            )
        }
    }
}

@Composable
private fun PlayerSocialActions(
    friendship: ProfileFriendship?,
    isLoading: Boolean,
    onAddFriend: () -> Unit,
    onAcceptFriend: (String) -> Unit,
    onRemoveFriend: (String) -> Unit,
    onBlock: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Button(
                shape = neonButtonShape(),
                enabled = !isLoading && friendship?.status != FriendshipStatus.PendingOutgoing,
                onClick = {
                    when (friendship?.status) {
                        FriendshipStatus.PendingIncoming -> onAcceptFriend(friendship.id)
                        FriendshipStatus.Accepted -> onRemoveFriend(friendship.id)
                        FriendshipStatus.PendingOutgoing -> Unit
                        null -> onAddFriend()
                    }
                },
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = if (friendship?.status == FriendshipStatus.Accepted) Icons.Rounded.CheckCircle else Icons.Rounded.PersonAdd,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    text = stringResource(when (friendship?.status) {
                        FriendshipStatus.PendingIncoming -> R.string.profile_friend_accept
                        FriendshipStatus.PendingOutgoing -> R.string.profile_friend_pending
                        FriendshipStatus.Accepted -> R.string.profile_friend_remove
                        null -> R.string.profile_friend_add
                    }),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            IconButton(enabled = !isLoading, onClick = onBlock) {
                Icon(Icons.Rounded.Block, contentDescription = stringResource(R.string.profile_block_player))
            }
        }
    }
}

@Composable
private fun MyListsActionCard(onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(48.dp),
                shape = neonShape(16.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.ViewList,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = stringResource(R.string.my_lists_title),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
                )
                Text(
                    text = stringResource(R.string.my_lists_subtitle),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Text(
                text = stringResource(R.string.my_lists_open),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun ProfileFeatureHubCard(
    deviceCount: Int,
    cloudProfileCount: Int,
    friendCount: Int,
    blockedCount: Int,
    onDevices: () -> Unit,
    onCloudProfiles: () -> Unit,
    onFriends: () -> Unit,
    onBlocked: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder()
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(
                text = stringResource(R.string.profile_features_title),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
            )
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedButton(
                    shape = neonButtonShape(), onClick = onDevices, modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.Devices, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(7.dp))
                    Text(stringResource(R.string.profile_devices_count, deviceCount), maxLines = 1)
                }
                OutlinedButton(
                    shape = neonButtonShape(), onClick = onCloudProfiles, modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.CloudSync, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(7.dp))
                    Text(stringResource(R.string.profile_cloud_count, cloudProfileCount), maxLines = 1)
                }
            }
            OutlinedButton(
                shape = neonButtonShape(), onClick = onFriends, modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Rounded.Person, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(8.dp))
                Text(
                    stringResource(R.string.profile_friends_title),
                    modifier = Modifier.weight(1f),
                    textAlign = TextAlign.Start
                )
                Text(friendCount.toString())
            }
            OutlinedButton(
                shape = neonButtonShape(), onClick = onBlocked, modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Rounded.Block, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(8.dp))
                Text(
                    stringResource(R.string.profile_blocked_title),
                    modifier = Modifier.weight(1f),
                    textAlign = TextAlign.Start
                )
                Text(blockedCount.toString())
            }
        }
    }
}

@Composable
private fun ProfileNotificationButton(pendingCount: Int, onClick: () -> Unit) {
    Box {
        IconButton(onClick = onClick) {
            Icon(
                imageVector = Icons.Rounded.Notifications,
                contentDescription = stringResource(R.string.profile_social_center_title),
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (pendingCount > 0) {
            Surface(
                modifier = Modifier.align(Alignment.TopEnd),
                shape = CircleShape,
                color = MaterialTheme.colorScheme.error,
                contentColor = MaterialTheme.colorScheme.onError
            ) {
                Text(
                    text = pendingCount.coerceAtMost(99).toString(),
                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                    modifier = Modifier.padding(horizontal = 5.dp, vertical = 1.dp)
                )
            }
        }
    }
}

@Composable
private fun TrophySummaryCard(summary: PlayerTrophySummary) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f),
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Rounded.EmojiEvents,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(34.dp)
            )
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.profile_tab_achievements),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = stringResource(
                        R.string.profile_trophies_summary_format,
                        summary.unlockedTrophies,
                        summary.totalTrophies,
                        summary.earnedPoints
                    ),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (summary.totalTrophies > 0) {
                    LinearProgressIndicator(
                        progress = { summary.progressFraction },
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 2.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun TrophyFilterRow(selected: String, onSelect: (String) -> Unit) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        listOf(
            "all" to R.string.profile_trophy_filter_all,
            "in_progress" to R.string.profile_trophy_filter_in_progress,
            "completed" to R.string.profile_trophy_filter_completed
        ).forEach { (key, label) ->
            FilterChip(
                shape = neonChipShape(),
                selected = selected == key,
                onClick = { onSelect(key) },
                label = { Text(stringResource(label), maxLines = 1) },
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun TrophySetRow(stat: PlayerTrophyStat, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder(alpha = if (stat.completed) 0.8f else 0.45f)
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(48.dp),
                shape = neonShape(14.dp),
                color = MaterialTheme.colorScheme.surfaceVariant
            ) {
                LocalImage(
                    path = stat.iconPath,
                    contentDescription = stat.gameTitle,
                    fallbackLabel = stat.gameTitle,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = stat.gameTitle,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    Text(
                        text = stringResource(R.string.profile_trophies_points_format, stat.earnedPoints, stat.totalPoints),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
                Text(
                    text = stat.setName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                LinearProgressIndicator(
                    progress = { stat.progressFraction },
                    modifier = Modifier.fillMaxWidth()
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = stringResource(
                            R.string.profile_trophy_progress_format,
                            stat.unlockedCount,
                            stat.trophyCount
                        ),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    TrophyGradeBreakdown(stat)
                }
            }
        }
    }
}

@Composable
private fun TrophyGradeBreakdown(stat: PlayerTrophyStat) {
    val grades = listOf(
        "P" to stat.platinumCount,
        "G" to stat.goldCount,
        "S" to stat.silverCount,
        "B" to stat.bronzeCount
    )
    Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
        grades.forEach { (label, count) ->
            Surface(
                shape = neonShapeCorners(6.dp),
                color = MaterialTheme.colorScheme.surfaceVariant
            ) {
                Text(
                    text = "$label$count",
                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 5.dp, vertical = 1.dp)
                )
            }
        }
    }
}

@Composable
private fun ProfileNotificationsDialog(
    friendships: List<ProfileFriendship>,
    feed: List<ProfileFeedEvent>,
    profiles: Map<String, PlayerProfile>,
    isLoading: Boolean,
    onAccept: (String) -> Unit,
    onOpenProfile: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val incoming = friendships.filter { it.status == FriendshipStatus.PendingIncoming }
    ProfileFeatureDialog(title = stringResource(R.string.profile_social_center_title), onDismiss = onDismiss) {
        Text(
            text = stringResource(R.string.profile_friend_requests),
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold)
        )
        if (incoming.isEmpty()) {
            Text(
                text = stringResource(R.string.profile_friend_requests_empty),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            incoming.forEach { friendship ->
                SocialIdentityRow(
                    uid = friendship.otherUid,
                    profile = profiles[friendship.otherUid],
                    onOpen = { onOpenProfile(friendship.otherUid) },
                    action = {
                        Button(
                            shape = neonButtonShape(),
                            enabled = !isLoading,
                            onClick = { onAccept(friendship.id) }
                        ) {
                            Text(stringResource(R.string.profile_friend_accept))
                        }
                    }
                )
            }
        }
        Text(
            text = stringResource(R.string.profile_recent_events),
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold)
        )
        if (feed.isEmpty()) {
            Text(
                text = stringResource(R.string.profile_friend_requests_empty),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            feed.take(5).forEach { event ->
                Text(
                    text = stringResource(R.string.profile_event_achievement, event.gameTitle.orEmpty()),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun ProfileFriendsDialog(
    friendships: List<ProfileFriendship>,
    profiles: Map<String, PlayerProfile>,
    isLoading: Boolean,
    onRemove: (String) -> Unit,
    onOpenProfile: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val accepted = friendships.filter { it.status == FriendshipStatus.Accepted }
    var pendingRemove by remember { mutableStateOf<ProfileFriendship?>(null) }
    ProfileFeatureDialog(title = stringResource(R.string.profile_friends_title), onDismiss = onDismiss) {
        if (accepted.isEmpty()) {
            Text(
                text = stringResource(R.string.profile_friends_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            accepted.forEach { friendship ->
                SocialIdentityRow(
                    uid = friendship.otherUid,
                    profile = profiles[friendship.otherUid],
                    onOpen = { onOpenProfile(friendship.otherUid) },
                    action = {
                        Box {
                            var menuOpen by remember { mutableStateOf(false) }
                            IconButton(enabled = !isLoading, onClick = { menuOpen = true }) {
                                Icon(Icons.Rounded.MoreVert, contentDescription = stringResource(R.string.profile_friend_remove_action))
                            }
                            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                                DropdownMenuItem(
                                    text = { Text(stringResource(R.string.profile_friend_remove_action)) },
                                    onClick = {
                                        menuOpen = false
                                        pendingRemove = friendship
                                    },
                                    leadingIcon = { Icon(Icons.Rounded.Delete, contentDescription = null) }
                                )
                            }
                        }
                    }
                )
            }
        }
    }
    pendingRemove?.let { friendship ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { pendingRemove = null },
            title = { Text(stringResource(R.string.profile_friend_remove_title)) },
            text = { Text(stringResource(R.string.profile_friend_remove_confirm)) },
            confirmButton = {
                TextButton(onClick = {
                    onRemove(friendship.id)
                    pendingRemove = null
                }) { Text(stringResource(R.string.profile_friend_remove_action)) }
            },
            dismissButton = {
                TextButton(onClick = { pendingRemove = null }) { Text(stringResource(R.string.common_cancel)) }
            }
        )
    }
}

@Composable
private fun ProfileBlockedPlayersDialog(
    blockedUids: List<String>,
    profiles: Map<String, PlayerProfile>,
    isLoading: Boolean,
    onUnblock: (String) -> Unit,
    onDismiss: () -> Unit
) {
    ProfileFeatureDialog(title = stringResource(R.string.profile_blocked_title), onDismiss = onDismiss) {
        if (blockedUids.isEmpty()) {
            Text(
                text = stringResource(R.string.profile_blocked_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            blockedUids.forEach { uid ->
                SocialIdentityRow(
                    uid = uid,
                    profile = profiles[uid],
                    onOpen = null,
                    action = {
                        OutlinedButton(
                            shape = neonButtonShape(),
                            enabled = !isLoading,
                            onClick = { onUnblock(uid) }
                        ) {
                            Icon(Icons.Rounded.LockOpen, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(stringResource(R.string.profile_unblock_player))
                        }
                    }
                )
            }
        }
    }
}

@Composable
private fun SocialIdentityRow(
    uid: String,
    profile: PlayerProfile?,
    onOpen: (() -> Unit)?,
    action: @Composable () -> Unit
) {
    Surface(
        onClick = { onOpen?.invoke() },
        enabled = onOpen != null,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(42.dp),
                shape = CircleShape,
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
            ) {
                UrlImage(
                    imageUrl = profile?.photoURL,
                    contentDescription = profile?.displayName ?: uid,
                    fallbackLabel = profile?.displayName ?: uid,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = profile?.displayName ?: uid.take(8),
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                profile?.playerTag?.takeIf { it.isNotBlank() }?.let { tag ->
                    Text(
                        text = tag,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
            action()
        }
    }
}

@Composable
private fun ProfileDevicesDialog(
    devices: List<PlayerDevice>,
    isLoading: Boolean,
    onSetPublic: (String, Boolean) -> Unit,
    onDelete: (String) -> Unit,
    onDismiss: () -> Unit
) {
    ProfileFeatureDialog(title = stringResource(R.string.profile_devices_title), onDismiss = onDismiss) {
        Text(
            text = stringResource(R.string.profile_devices_description),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (devices.isEmpty()) {
            Text(
                text = stringResource(R.string.profile_devices_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        devices.forEach { device ->
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = neonShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
            ) {
                Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            text = device.displayName.ifBlank { device.model },
                            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                            modifier = Modifier.weight(1f)
                        )
                        if (device.isCurrent) {
                            Surface(
                                shape = neonChipShape(),
                                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
                            ) {
                                Text(
                                    text = stringResource(R.string.profile_device_current),
                                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)
                                )
                            }
                        }
                    }
                    Text(
                        text = stringResource(R.string.profile_device_soc, device.soc.ifBlank { "—" }),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(R.string.profile_device_gpu, device.gpuFamily.ifBlank { "—" }),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(R.string.profile_device_ram, device.ramMb),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(R.string.profile_device_android, device.androidVersion),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(R.string.profile_device_app_core, device.appVersion, device.coreVersion),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(
                            shape = neonButtonShape(),
                            enabled = !isLoading,
                            onClick = { onSetPublic(device.deviceId, !device.isPublic) },
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(
                                stringResource(
                                    if (device.isPublic) R.string.profile_device_make_private
                                    else R.string.profile_device_make_public
                                )
                            )
                        }
                        if (!device.isCurrent) {
                            IconButton(enabled = !isLoading, onClick = { onDelete(device.deviceId) }) {
                                Icon(Icons.Rounded.Delete, contentDescription = stringResource(R.string.profile_device_delete))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
internal fun CloudProfilesDialog(
    profiles: List<CloudEmulatorProfile> = emptyList(),
    isLoading: Boolean = false,
    onSave: (String, String?) -> Unit = { _, _ -> },
    onRestore: (String) -> Unit = {},
    onDelete: (String) -> Unit = {},
    onDismiss: () -> Unit,
    firebaseAvailable: Boolean = true,
    initialDrive: Boolean = false
) {
    var name by rememberSaveable { mutableStateOf("") }
    var pendingRestore by rememberSaveable { mutableStateOf<String?>(null) }
    var cloudTab by rememberSaveable { mutableIntStateOf(if (initialDrive) 1 else 0) }
    ProfileFeatureDialog(title = stringResource(R.string.profile_cloud_title), onDismiss = onDismiss) {
        if (firebaseAvailable) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    shape = neonChipShape(),
                    selected = cloudTab == 0,
                    onClick = { cloudTab = 0 },
                    label = { Text(stringResource(R.string.drive_settings_tab)) }
                )
                FilterChip(
                    shape = neonChipShape(),
                    selected = cloudTab == 1,
                    onClick = { cloudTab = 1 },
                    label = { Text(stringResource(R.string.drive_title)) }
                )
            }
        }
        if (cloudTab == 1 || !firebaseAvailable) {
            DriveBackupPanel()
            TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End)) {
                Text(stringResource(R.string.common_close))
            }
            return@ProfileFeatureDialog
        }
        Text(
            text = stringResource(R.string.profile_cloud_description),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        OutlinedTextField(
            value = name,
            onValueChange = { name = it.take(64) },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            shape = neonShape(18.dp),
            label = { Text(stringResource(R.string.profile_cloud_name)) }
        )
        Button(
            shape = neonButtonShape(),
            enabled = !isLoading && name.trim().isNotEmpty() && profiles.size < 5,
            onClick = {
                onSave(name, null)
                name = ""
            },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.profile_cloud_save_current))
        }
        profiles.forEach { profile ->
            Surface(
                shape = neonShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
            ) {
                Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(profile.name, style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold))
                    Text(
                        text = stringResource(R.string.profile_cloud_version, profile.appVersion, profile.coreVersion),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (pendingRestore == profile.id) {
                        Text(
                            text = stringResource(R.string.profile_cloud_restore_warning),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(
                            shape = neonButtonShape(),
                            enabled = !isLoading,
                            onClick = {
                                if (pendingRestore == profile.id) {
                                    onRestore(profile.id)
                                    pendingRestore = null
                                } else {
                                    pendingRestore = profile.id
                                }
                            },
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(
                                stringResource(
                                    if (pendingRestore == profile.id) R.string.profile_cloud_confirm_restore
                                    else R.string.profile_cloud_restore
                                )
                            )
                        }
                        IconButton(enabled = !isLoading, onClick = { onDelete(profile.id) }) {
                            Icon(Icons.Rounded.Delete, contentDescription = stringResource(R.string.profile_cloud_delete))
                        }
                    }
                }
            }
        }
        TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End)) {
            Text(stringResource(R.string.common_close))
        }
    }
}

@Composable
private fun PublicDeviceDialog(device: PublicPlayerDevice, onDismiss: () -> Unit) {
    ProfileFeatureDialog(title = stringResource(R.string.profile_devices_title), onDismiss = onDismiss) {
        Text(
            text = device.displayName,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
        )
        Text(
            text = stringResource(R.string.profile_device_soc, device.soc.ifBlank { "—" }),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = stringResource(R.string.profile_device_gpu, device.gpuFamily.ifBlank { "—" }),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = stringResource(R.string.profile_device_ram, device.ramMb),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = stringResource(R.string.profile_device_android, device.androidVersion),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = stringResource(R.string.profile_device_app_core, device.appVersion, device.coreVersion),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun ProfileFeatureDialog(
    title: String,
    onDismiss: () -> Unit,
    content: @Composable ColumnScope.() -> Unit
) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(
            modifier = Modifier
                .widthIn(max = 620.dp)
                .fillMaxWidth()
                .padding(20.dp)
                .heightIn(max = 720.dp),
            shape = neonShape(28.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 6.dp,
            border = profileCardBorder()
        ) {
            Column(
                modifier = Modifier
                    .verticalScroll(rememberScrollState())
                    .padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
                content = {
                    Text(title, style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold))
                    content()
                }
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProfileOverview(
    profile: PlayerProfile?,
    isLoading: Boolean,
    photoURL: String?,
    email: String?,
    isActionLoading: Boolean,
    isProUnlocked: Boolean,
    rankInsights: PlayerRankInsights?,
    trophySummary: PlayerTrophySummary,
    onCustomizePro: () -> Unit,
    onShareCard: () -> Unit,
    onUpdateName: (String) -> Unit,
    onSignOut: () -> Unit
) {
    var showEditName by rememberSaveable { mutableStateOf(false) }
    val avatarUrl = photoURL ?: profile?.photoURL
    val accent = profileAccentColor(profile?.profileAccent ?: "gold")
    if (showEditName) {
        EditProfileNameDialog(
            initialName = profile?.displayName.orEmpty(),
            isLoading = isActionLoading,
            onNameChange = onUpdateName,
            onDismiss = { showEditName = false },
            onSave = { showEditName = false }
        )
    }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = if (profile?.isProMember == true) BorderStroke(1.dp, accent.copy(alpha = 0.72f)) else profileCardBorder()
    ) {
        if (isLoading && profile == null) {
            ProfileOverviewSkeletonContent(showAccountActions = true, showRank = true, showProActions = isProUnlocked)
        } else {
            Column(
                modifier = Modifier.padding(18.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(86.dp)
                            .clip(CircleShape)
                            .background(
                                if (profile?.isProMember == true) accent.copy(alpha = 0.22f)
                                else MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        UrlImage(
                            imageUrl = avatarUrl,
                            contentDescription = profile?.displayName.orEmpty(),
                            fallbackLabel = profile?.displayName ?: email.orEmpty(),
                            modifier = Modifier.fillMaxSize(),
                            pinInMemory = true
                        )
                    }
                    Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                text = profile?.displayName ?: stringResource(R.string.profile_player),
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, fill = false)
                            )
                            if (profile?.isProMember == true) {
                                ProBadge(accent)
                            }
                        }
                        profile?.playerTag?.takeIf { it.isNotBlank() }?.let { tag ->
                            Text(
                                text = stringResource(R.string.profile_player_id_format, tag),
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }
                        email?.takeIf { it.isNotBlank() }?.let { mail ->
                            Text(
                                text = mail,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                    IconButton(enabled = !isActionLoading, onClick = { showEditName = true }) {
                        Icon(Icons.Rounded.Edit, contentDescription = stringResource(R.string.profile_edit_name))
                    }
                }

                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatChip(
                        icon = Icons.Rounded.Schedule,
                        label = stringResource(R.string.profile_total_time),
                        value = formatDuration(profile?.totalPlayTimeMs ?: 0L),
                        modifier = Modifier.weight(1f)
                    )
                    StatChip(
                        icon = Icons.Rounded.SportsEsports,
                        label = stringResource(R.string.profile_games_played),
                        value = (profile?.gamesPlayed ?: 0).toString(),
                        modifier = Modifier.weight(1f)
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatChip(
                        icon = Icons.Rounded.EmojiEvents,
                        label = stringResource(R.string.profile_tab_achievements),
                        value = trophySummary.unlockedTrophies.toString(),
                        modifier = Modifier.weight(1f)
                    )
                    StatChip(
                        icon = Icons.Rounded.Leaderboard,
                        label = stringResource(R.string.settings_ra_points_label),
                        value = trophySummary.earnedPoints.toString(),
                        modifier = Modifier.weight(1f)
                    )
                }
                rankInsights?.let { rank ->
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = neonShape(18.dp),
                        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.14f))
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.Leaderboard,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = stringResource(
                                    R.string.profile_rank_insights,
                                    rank.rank,
                                    rank.totalPlayers,
                                    rank.percentile
                                ),
                                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                                modifier = Modifier.weight(1f)
                            )
                            Text(
                                text = stringResource(R.string.profile_total_players, rank.totalPlayers),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
                FavoriteGamesShowcase(profile = profile)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        shape = neonButtonShape(),
                        enabled = isProUnlocked && profile != null,
                        onClick = onCustomizePro,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.Palette, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(7.dp))
                        Text(stringResource(R.string.profile_customize_pro), maxLines = 1)
                    }
                    OutlinedButton(
                        shape = neonButtonShape(),
                        enabled = isProUnlocked && profile != null,
                        onClick = onShareCard,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.Share, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(7.dp))
                        Text(stringResource(R.string.profile_player_card_share), maxLines = 1)
                    }
                }
                OutlinedButton(
                    shape = neonButtonShape(),
                    enabled = !isActionLoading,
                    onClick = onSignOut,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.AutoMirrored.Rounded.Logout, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.profile_sign_out))
                }
            }
        }
    }
}

@Composable
private fun EditProfileNameDialog(
    initialName: String,
    isLoading: Boolean,
    onNameChange: (String) -> Unit,
    onDismiss: () -> Unit,
    onSave: () -> Unit
) {
    var value by rememberSaveable { mutableStateOf(initialName) }
    val focusRequester = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .widthIn(max = 520.dp)
                .padding(20.dp),
            shape = neonShape(26.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 8.dp,
            border = profileCardBorder()
        ) {
            Column(modifier = Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Text(
                    text = stringResource(R.string.profile_edit_name),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                )
                Text(
                    text = stringResource(R.string.profile_edit_name_description),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                OutlinedTextField(
                    value = value,
                    onValueChange = { value = it.take(32) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .focusRequester(focusRequester),
                    singleLine = true,
                    shape = neonShape(18.dp),
                    label = { Text(stringResource(R.string.profile_display_name)) },
                    keyboardActions = androidx.compose.foundation.text.KeyboardActions(
                        onDone = {
                            keyboard?.hide()
                            onNameChange(value)
                            onSave()
                        }
                    ),
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = ImeAction.Done),
                    supportingText = {
                        Text(stringResource(R.string.profile_name_character_count, value.length, 32))
                    }
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        shape = neonButtonShape(),
                        onClick = onDismiss,
                        modifier = Modifier.weight(1f)
                    ) {
                        Text(stringResource(R.string.common_cancel))
                    }
                    Button(
                        shape = neonButtonShape(),
                        enabled = !isLoading && value.trim().isNotEmpty(),
                        onClick = {
                            keyboard?.hide()
                            onNameChange(value)
                            onSave()
                        },
                        modifier = Modifier.weight(1f)
                    ) {
                        Text(stringResource(R.string.save))
                    }
                }
            }
        }
        LaunchedEffect(Unit) { focusRequester.requestFocus() }
    }
}

@Composable
private fun ProBadge(accent: Color, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = neonShape(10.dp),
        color = accent.copy(alpha = 0.18f),
        border = BorderStroke(1.dp, accent.copy(alpha = 0.6f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.WorkspacePremium,
                contentDescription = null,
                tint = accent,
                modifier = Modifier.size(13.dp)
            )
            Text(
                text = stringResource(R.string.profile_pro_badge),
                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                color = accent
            )
        }
    }
}

@Composable
private fun FavoriteGamesShowcase(
    profile: PlayerProfile?,
    modifier: Modifier = Modifier,
    contentPadding: PaddingValues = PaddingValues(0.dp)
) {
    val favorites = profile?.favoriteGameKeys.orEmpty()
        .mapNotNull { key -> profile?.games?.firstOrNull { it.gameKey == key } }
    if (favorites.isEmpty()) return
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            text = stringResource(R.string.profile_showcase_title),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
        )
        LazyRow(
            contentPadding = contentPadding,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            items(favorites, key = { it.gameKey }) { game ->
                Column(
                    modifier = Modifier.width(104.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(138.dp),
                        shape = neonShape(16.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant
                    ) {
                        ProfileCoverArt(
                            path = game.coverArtPath,
                            title = game.title,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.labelSmall,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = formatDuration(game.totalPlayTimeMs),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

@Composable
private fun PlayerSearchField(
    query: String,
    isLoading: Boolean,
    onQueryChange: (String) -> Unit,
    onRefresh: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = query,
                    onValueChange = onQueryChange,
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                    shape = neonShape(18.dp),
                    label = { Text(stringResource(R.string.profile_leaderboard_search_hint)) },
                    leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                    trailingIcon = {
                        if (isLoading) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        }
                    }
                )
                IconButton(onClick = onRefresh) {
                    Icon(
                        imageVector = Icons.Rounded.Refresh,
                        contentDescription = stringResource(R.string.profile_leaderboard_refresh)
                    )
                }
            }
            Text(
                text = stringResource(R.string.profile_leaderboard_search_help),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProProfileCustomizationDialog(
    profile: PlayerProfile,
    games: List<PlayerGamePlayStat>,
    onDismiss: () -> Unit,
    onSave: (String, List<String>) -> Unit
) {
    var accent by rememberSaveable(profile.profileAccent) { mutableStateOf(profile.profileAccent) }
    var favoriteKeys by rememberSaveable(profile.favoriteGameKeys) {
        mutableStateOf(profile.favoriteGameKeys.take(3))
    }
    val accents = listOf("gold", "crimson", "blue", "violet", "emerald")
    val sortedGames = remember(games) { games.sortedByDescending { it.totalPlayTimeMs }.take(30) }
    val windowSize = LocalWindowInfo.current.containerSize
    val density = LocalDensity.current
    val windowWidth = with(density) { windowSize.width.toDp() }
    val windowHeight = with(density) { windowSize.height.toDp() }
    val isLandscape = windowWidth > windowHeight
    val maxDialogHeight = if (isLandscape) {
        (windowHeight - 40.dp).coerceAtLeast(300.dp)
    } else {
        (windowHeight - 64.dp).coerceAtLeast(520.dp)
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(
                    start = if (isLandscape) 10.dp else 14.dp,
                    top = if (isLandscape) 10.dp else 14.dp,
                    end = if (isLandscape) 10.dp else 14.dp,
                    bottom = if (isLandscape) 4.dp else 8.dp
                ),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth(if (isLandscape) 0.98f else 0.94f)
                    .widthIn(max = if (isLandscape) 1600.dp else 720.dp)
                    .heightIn(max = maxDialogHeight),
                shape = neonShape(30.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 8.dp,
                border = profileCardBorder()
            ) {
                Column(
                    modifier = Modifier
                        .verticalScroll(rememberScrollState())
                        .padding(18.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Text(
                        text = stringResource(R.string.profile_customize_pro_title),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                    )
                    ProCustomizationSection(
                        title = stringResource(R.string.profile_customize_pro_accent),
                        description = stringResource(R.string.profile_customize_pro_accent_help)
                    ) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            accents.forEach { item ->
                                ProfileAccentChoice(
                                    accent = item,
                                    selected = accent == item,
                                    onClick = { accent = item }
                                )
                            }
                        }
                    }
                    ProCustomizationSection(
                        title = stringResource(R.string.profile_customize_pro_games),
                        description = stringResource(R.string.profile_customize_pro_games_help),
                        trailing = stringResource(R.string.profile_showcase_count_format, favoriteKeys.size, 3)
                    ) {
                        sortedGames.forEach { game ->
                            ProShowcaseGameChoice(
                                game = game,
                                selected = game.gameKey in favoriteKeys,
                                enabled = game.gameKey in favoriteKeys || favoriteKeys.size < 3,
                                onClick = {
                                    favoriteKeys = if (game.gameKey in favoriteKeys) {
                                        favoriteKeys - game.gameKey
                                    } else {
                                        (favoriteKeys + game.gameKey).take(3)
                                    }
                                }
                            )
                        }
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        OutlinedButton(
                            shape = neonButtonShape(),
                            onClick = onDismiss,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.common_cancel))
                        }
                        Button(
                            shape = neonButtonShape(),
                            onClick = { onSave(accent, favoriteKeys) },
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.save))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProCustomizationSection(
    title: String,
    description: String,
    trailing: String? = null,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(title, style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold))
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                trailing?.let {
                    Text(
                        text = it,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
            content()
        }
    }
}

@Composable
private fun ProfileAccentChoice(accent: String, selected: Boolean, onClick: () -> Unit) {
    val color = profileAccentColor(accent)
    Surface(
        onClick = onClick,
        shape = neonShape(16.dp),
        color = if (selected) color.copy(alpha = 0.2f) else MaterialTheme.colorScheme.surface,
        border = BorderStroke(if (selected) 2.dp else 1.dp, if (selected) color else MaterialTheme.colorScheme.outlineVariant)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(18.dp)
                    .clip(CircleShape)
                    .background(color)
            )
            Text(
                text = stringResource(accentNameRes(accent)),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold)
            )
            if (selected) {
                Icon(
                    imageVector = Icons.Rounded.CheckCircle,
                    contentDescription = null,
                    tint = color,
                    modifier = Modifier.size(16.dp)
                )
            }
        }
    }
}

@Composable
private fun ProShowcaseGameChoice(
    game: PlayerGamePlayStat,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.12f) else MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Row(
            modifier = Modifier.padding(10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Surface(
                modifier = Modifier
                    .size(width = 42.dp, height = 56.dp),
                shape = neonShape(10.dp),
                color = MaterialTheme.colorScheme.surfaceVariant
            ) {
                ProfileCoverArt(path = game.coverArtPath, title = game.title, modifier = Modifier.fillMaxSize())
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = formatDuration(game.totalPlayTimeMs),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (selected) {
                Icon(
                    imageVector = Icons.Rounded.CheckCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun AdvancedStatsContent(
    isProUnlocked: Boolean,
    isLoading: Boolean,
    hasAttemptedLoad: Boolean,
    isProfileLoading: Boolean,
    activity: List<PlayerActivityDay>,
    rankInsights: PlayerRankInsights?,
    profile: PlayerProfile?,
    onRefresh: () -> Unit
) {
    if (!isProUnlocked) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder()
        ) {
            Column(
                modifier = Modifier.padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Icon(
                    imageVector = Icons.Rounded.WorkspacePremium,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(38.dp)
                )
                Text(
                    text = stringResource(R.string.profile_stats_locked_title),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    textAlign = TextAlign.Center
                )
                Text(
                    text = stringResource(R.string.profile_stats_locked_body),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }
        }
        return
    }

    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (isProfileLoading && profile == null) {
            StatChipSkeleton()
            StatChipSkeleton()
        } else {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = neonShape(24.dp),
                color = MaterialTheme.colorScheme.surface,
                border = profileCardBorder()
            ) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = stringResource(R.string.profile_stats_lifetime_title),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        StatChip(
                            icon = Icons.Rounded.Schedule,
                            label = stringResource(R.string.profile_stats_this_week),
                            value = formatDuration(playTimeWithinDays(activity, 7)),
                            modifier = Modifier.weight(1f)
                        )
                        StatChip(
                            icon = Icons.Rounded.BarChart,
                            label = stringResource(R.string.profile_stats_this_month),
                            value = formatDuration(playTimeWithinDays(activity, 30)),
                            modifier = Modifier.weight(1f)
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        StatChip(
                            icon = Icons.Rounded.SportsEsports,
                            label = stringResource(R.string.profile_stats_total_sessions),
                            value = activity.sumOf { it.sessions }.toString(),
                            modifier = Modifier.weight(1f)
                        )
                        val streaks = calculateStreaks(activity)
                        StatChip(
                            icon = Icons.Rounded.EmojiEvents,
                            label = stringResource(R.string.profile_stats_current_streak),
                            value = stringResource(R.string.profile_stats_days_format, streaks.first),
                            modifier = Modifier.weight(1f)
                        )
                    }
                    val bestStreak = calculateStreaks(activity).second
                    Text(
                        text = stringResource(R.string.profile_stats_best_streak, bestStreak),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(R.string.profile_stats_lifetime_body),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    rankInsights?.let { rank ->
                        Text(
                            text = stringResource(
                                R.string.profile_rank_insights,
                                rank.rank,
                                rank.totalPlayers,
                                rank.percentile
                            ),
                            style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
        }
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder()
        ) {
            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = stringResource(R.string.profile_stats_activity),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        modifier = Modifier.weight(1f)
                    )
                    IconButton(onClick = onRefresh) {
                        Icon(Icons.Rounded.Refresh, contentDescription = stringResource(R.string.profile_stats_refresh))
                    }
                }
                when {
                    isLoading && activity.isEmpty() -> StatsActivitySkeleton()
                    hasAttemptedLoad && !isLoading && activity.isEmpty() -> EmptyActivityCard()
                    activity.isEmpty() -> StatsActivitySkeleton()
                    else -> {
                        Text(
                            text = stringResource(R.string.profile_stats_weekly_chart),
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        ActivityChart(activity)
                    }
                }
            }
        }
    }
}

@Composable
private fun EmptyActivityCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.BarChart,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = stringResource(R.string.profile_stats_history_empty_title),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold)
            )
            Text(
                text = stringResource(R.string.profile_stats_empty),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
private fun ActivityChart(activity: List<PlayerActivityDay>) {
    val days = activity.takeLast(14)
    if (days.isEmpty()) return
    val maxValue = days.maxOf { it.playTimeMs }.coerceAtLeast(1L)
    LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        items(days, key = { it.day }) { day ->
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp),
                modifier = Modifier.width(44.dp)
            ) {
                Text(
                    text = formatDuration(day.playTimeMs),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1
                )
                Box(
                    modifier = Modifier
                        .height(90.dp)
                        .width(26.dp),
                    contentAlignment = Alignment.BottomCenter
                ) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(
                                (90f * (day.playTimeMs.toFloat() / maxValue.toFloat()))
                                    .coerceAtLeast(6f)
                                    .dp
                            ),
                        shape = neonShapeCorners(8.dp),
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.75f)
                    ) {}
                }
                Text(
                    text = day.day.takeLast(5),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
private fun ProfileOverviewSkeletonContent(
    showAccountActions: Boolean,
    showRank: Boolean,
    showProActions: Boolean
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            SkeletonBlock(modifier = Modifier.size(72.dp))
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.6f).height(20.dp))
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.4f).height(14.dp))
            }
        }
        StatChipSkeleton()
        StatChipSkeleton()
        if (showRank) SkeletonBlock(modifier = Modifier.fillMaxWidth().height(44.dp))
        if (showProActions) SkeletonButtonRow()
        if (showAccountActions) SkeletonBlock(modifier = Modifier.fillMaxWidth().height(42.dp))
    }
}

@Composable
private fun SkeletonButtonRow() {
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        SkeletonBlock(modifier = Modifier.weight(1f).height(40.dp))
        SkeletonBlock(modifier = Modifier.weight(1f).height(40.dp))
    }
}

@Composable
private fun ReadOnlyProfileSkeletonCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Box(modifier = Modifier.padding(16.dp)) {
            ProfileOverviewSkeletonContent(showAccountActions = false, showRank = false, showProActions = false)
        }
    }
}

@Composable
private fun GamePlayStatRow(game: PlayerGamePlayStat, onClick: (() -> Unit)?) {
    Surface(
        onClick = { onClick?.invoke() },
        enabled = onClick != null,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier
                    .size(width = 54.dp, height = 72.dp),
                shape = neonShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceVariant
            ) {
                ProfileCoverArt(path = game.coverArtPath, title = game.title, modifier = Modifier.fillMaxSize())
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = stringResource(R.string.profile_game_sessions_format, game.sessions),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                game.lastPlayedAtMs?.let { lastPlayed ->
                    Text(
                        text = stringResource(R.string.profile_game_last_played_format, formatDate(lastPlayed)),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Text(
                text = formatDuration(game.totalPlayTimeMs),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun GamePlayStatSkeletonRow() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            SkeletonBlock(modifier = Modifier.size(width = 54.dp, height = 72.dp))
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.7f).height(16.dp))
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.45f).height(12.dp))
            }
            SkeletonBlock(modifier = Modifier.size(width = 52.dp, height = 18.dp))
        }
    }
}

@Composable
private fun LeaderboardRowSkeleton() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            SkeletonBlock(modifier = Modifier.size(42.dp))
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.55f).height(16.dp))
                SkeletonBlock(modifier = Modifier.fillMaxWidth(0.35f).height(12.dp))
            }
            SkeletonBlock(modifier = Modifier.size(width = 60.dp, height = 18.dp))
        }
    }
}

@Composable
private fun StatsActivitySkeleton() {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SkeletonBlock(modifier = Modifier.fillMaxWidth(0.4f).height(14.dp))
        SkeletonBlock(modifier = Modifier.fillMaxWidth().height(110.dp))
    }
}

@Composable
private fun StatChipSkeleton(modifier: Modifier = Modifier) {
    SkeletonBlock(modifier = modifier.fillMaxWidth().height(64.dp))
}

@Composable
private fun RecentGamesCard(
    games: List<PlayerGamePlayStat>,
    onGameClick: (PlayerGamePlayStat) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            text = stringResource(R.string.profile_recently_played),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
        )
        LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            items(games, key = { it.gameKey }) { game ->
                Surface(
                    onClick = { onGameClick(game) },
                    modifier = Modifier.width(112.dp),
                    shape = neonShape(16.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = profileCardBorder()
                ) {
                    Column(modifier = Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(138.dp),
                            shape = neonShape(12.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant
                        ) {
                            ProfileCoverArt(path = game.coverArtPath, title = game.title, modifier = Modifier.fillMaxSize())
                        }
                        Text(
                            text = game.title,
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.heightIn(min = 28.dp)
                        )
                        Text(
                            text = formatDuration(game.totalPlayTimeMs),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun RecentGamesSkeletonCard() {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SkeletonBlock(modifier = Modifier.fillMaxWidth(0.4f).height(18.dp))
        SkeletonBlock(modifier = Modifier.fillMaxWidth().height(180.dp))
    }
}

@Composable
private fun LeaderboardRow(
    entry: PlayerLeaderboardEntry,
    currentUid: String?,
    onClick: () -> Unit
) {
    val accent = profileAccentColor(entry.profileAccent)
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface,
        border = if (entry.uid == currentUid) BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.6f)) else profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            RankBadge(entry.rank)
            Surface(
                modifier = Modifier.size(42.dp),
                shape = CircleShape,
                color = accent.copy(alpha = 0.16f)
            ) {
                UrlImage(
                    imageUrl = entry.photoURL,
                    contentDescription = entry.displayName,
                    fallbackLabel = entry.displayName,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(
                        text = entry.displayName,
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    if (entry.isProMember) ProBadge(accent)
                }
                entry.playerTag.takeIf { it.isNotBlank() }?.let { tag ->
                    Text(
                        text = tag,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
                Text(
                    text = stringResource(R.string.profile_leaderboard_games_format, entry.gamesPlayed),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Text(
                text = formatDuration(entry.totalPlayTimeMs),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun RankBadge(rank: Int?) {
    Surface(
        modifier = Modifier.size(42.dp),
        shape = CircleShape,
        color = colorForRank(rank).copy(alpha = 0.18f),
        border = BorderStroke(1.dp, colorForRank(rank).copy(alpha = 0.55f))
    ) {
        Box(contentAlignment = Alignment.Center) {
            if (rank != null && rank <= 3) {
                Icon(
                    imageVector = Icons.Rounded.EmojiEvents,
                    contentDescription = null,
                    tint = colorForRank(rank),
                    modifier = Modifier.size(22.dp)
                )
            } else {
                Text(
                    text = rank?.toString() ?: "—",
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = colorForRank(rank)
                )
            }
        }
    }
}

@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "profile-skeleton")
    val alpha by transition.animateFloat(
        initialValue = 0.35f,
        targetValue = 0.85f,
        animationSpec = infiniteRepeatable(
            animation = tween(900, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "profile-skeleton-alpha"
    )
    Box(
        modifier = modifier
            .clip(neonShape(14.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = alpha))
    )
}

@Composable
private fun RevealOnEnter(revealKey: Any?, content: @Composable () -> Unit) {
    var visible by remember(revealKey) { mutableStateOf(false) }
    LaunchedEffect(revealKey) { visible = true }
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(animationSpec = tween(220)) + expandVertically(animationSpec = tween(260)),
        exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(160))
    ) {
        content()
    }
}

@Composable
private fun EmptyProfileState(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 26.dp)
        )
    }
}

@Composable
private fun StatChip(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    value: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(18.dp)
            )
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun ProfileCoverArt(path: String?, title: String, modifier: Modifier = Modifier) {
    val normalized = path?.takeIf { it.isNotBlank() }
    if (normalized != null && !normalized.startsWith("http://") && !normalized.startsWith("https://")) {
        LocalImage(
            path = normalized,
            contentDescription = title,
            fallbackLabel = title,
            modifier = modifier
        )
    } else {
        UrlImage(
            imageUrl = normalized,
            contentDescription = title,
            fallbackLabel = title,
            modifier = modifier
        )
    }
}

@Composable
internal fun profileCardBorder(alpha: Float = 0.58f): BorderStroke {
    return BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = alpha))
}

private fun ProfileTab.titleRes(): Int = when (this) {
    ProfileTab.Overview -> R.string.profile_tab_overview
    ProfileTab.Games -> R.string.profile_tab_games
    ProfileTab.Achievements -> R.string.profile_tab_achievements
    ProfileTab.Leaderboard -> R.string.profile_tab_leaderboard
    ProfileTab.Stats -> R.string.profile_tab_stats
}

private fun ProfileTab.icon(): androidx.compose.ui.graphics.vector.ImageVector = when (this) {
    ProfileTab.Overview -> Icons.Rounded.Person
    ProfileTab.Games -> Icons.Rounded.SportsEsports
    ProfileTab.Achievements -> Icons.Rounded.EmojiEvents
    ProfileTab.Leaderboard -> Icons.Rounded.Leaderboard
    ProfileTab.Stats -> Icons.Rounded.BarChart
}

private fun profileMessageRes(key: String): Int = when (key) {
    "profile_signed_in" -> R.string.profile_signed_in
    "profile_account_created" -> R.string.profile_account_created
    "profile_password_reset_sent" -> R.string.profile_password_reset_sent
    "profile_name_updated" -> R.string.profile_name_updated
    "profile_signed_out" -> R.string.profile_signed_out
    "profile_pro_updated" -> R.string.profile_pro_updated
    "profile_device_public" -> R.string.profile_device_public_done
    "profile_device_private" -> R.string.profile_device_private_done
    "profile_device_deleted" -> R.string.profile_device_deleted
    "profile_cloud_saved" -> R.string.profile_cloud_saved
    "profile_cloud_restored" -> R.string.profile_cloud_restored
    "profile_cloud_deleted" -> R.string.profile_cloud_deleted
    "profile_friend_request_sent" -> R.string.profile_friend_request_sent
    "profile_friend_added" -> R.string.profile_friend_added
    "profile_friend_removed" -> R.string.profile_friend_removed
    "profile_player_blocked" -> R.string.profile_player_blocked
    "profile_player_unblocked" -> R.string.profile_player_unblocked
    else -> R.string.profile_done
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

private fun colorForRank(rank: Int?): Color = when (rank) {
    1 -> Color(0xFFFFD65C)
    2 -> Color(0xFFC9D4E4)
    3 -> Color(0xFFE0A06A)
    else -> Color(0xFF8A93A8)
}

private fun profileAccentColor(accent: String): Color = when (accent) {
    "crimson" -> Color(0xFFDC414F)
    "blue" -> Color(0xFF4D9EFF)
    "violet" -> Color(0xFF9D6DFF)
    "emerald" -> Color(0xFF36C48E)
    else -> Color(0xFFD8B45F)
}

private fun accentNameRes(accent: String): Int = when (accent) {
    "crimson" -> R.string.profile_accent_crimson
    "blue" -> R.string.profile_accent_blue
    "violet" -> R.string.profile_accent_violet
    "emerald" -> R.string.profile_accent_emerald
    else -> R.string.profile_accent_gold
}

private fun calculateStreaks(activity: List<PlayerActivityDay>): Pair<Int, Int> {
    if (activity.isEmpty()) return 0 to 0
    val format = SimpleDateFormat("yyyy-MM-dd", Locale.US)
    val days = activity.filter { it.playTimeMs > 0L }
        .mapNotNull { day -> runCatching { format.parse(day.day) }.getOrNull() }
        .sorted()
    if (days.isEmpty()) return 0 to 0
    var best = 1
    var current = 1
    for (index in 1 until days.size) {
        val previous = days[index - 1]
        val next = days[index]
        val diff = (next.time - previous.time) / 86_400_000L
        current = if (diff == 1L) current + 1 else 1
        if (current > best) best = current
    }
    val calendar = Calendar.getInstance().apply {
        set(Calendar.HOUR_OF_DAY, 0)
        set(Calendar.MINUTE, 0)
        set(Calendar.SECOND, 0)
        set(Calendar.MILLISECOND, 0)
    }
    val today = calendar.time
    val lastDay = days.last()
    val lastDiff = ((today.time - lastDay.time).coerceAtLeast(0L) / 86_400_000L)
    val currentStreak = if (lastDiff <= 1L) current else 0
    return currentStreak to best
}

private fun playTimeWithinDays(activity: List<PlayerActivityDay>, days: Int): Long {
    val threshold = System.currentTimeMillis() - days * 86_400_000L
    val format = SimpleDateFormat("yyyy-MM-dd", Locale.US)
    return activity.filter { day ->
        val time = runCatching { format.parse(day.day)?.time }.getOrNull() ?: return@filter false
        time >= threshold
    }.sumOf { it.playTimeMs }
}

private fun formatDuration(durationMs: Long): String {
    val totalMinutes = durationMs.coerceAtLeast(0L) / 60_000L
    val hours = totalMinutes / 60L
    val minutes = totalMinutes % 60L
    return if (hours > 0L) "${hours}h ${minutes}m" else "${minutes}m"
}

private fun formatDate(timestampMs: Long): String {
    return DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(timestampMs))
}
