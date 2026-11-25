package com.sbro.emucorev.ui.profile

import android.app.Activity
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.auth.FirebaseAuthInvalidCredentialsException
import com.google.firebase.auth.FirebaseAuthInvalidUserException
import com.google.firebase.firestore.DocumentSnapshot
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.data.CloudEmulatorProfile
import com.sbro.emucorev.data.CloudEmulatorSettingsRepository
import com.sbro.emucorev.data.FriendRequestBlockedException
import com.sbro.emucorev.data.PlayerAccount
import com.sbro.emucorev.data.PlayerActivityDay
import com.sbro.emucorev.data.PlayerDevice
import com.sbro.emucorev.data.PlayerGamePlayStat
import com.sbro.emucorev.data.PlayerLeaderboardEntry
import com.sbro.emucorev.data.PlayerProfile
import com.sbro.emucorev.data.PlayerProfileRepository
import com.sbro.emucorev.data.PlayerRankInsights
import com.sbro.emucorev.data.PlayerTrophySummary
import com.sbro.emucorev.data.ProfileDeviceRepository
import com.sbro.emucorev.data.ProfileFeedEvent
import com.sbro.emucorev.data.ProfileFriendship
import com.sbro.emucorev.data.ProfileSocialRepository
import com.sbro.emucorev.data.ReauthCancelledException
import com.sbro.emucorev.data.TrophyCloudRepository
import com.sbro.emucorev.data.VitaCatalogRepository
import com.sbro.emucorev.data.VitaTrophyProfileRepository
import com.sbro.emucorev.data.collectReporting
import com.sbro.emucorev.data.runCatchingCancellable
import com.sbro.emucorev.data.truncateGraphemes
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.milliseconds

data class ProfileUiState(
    val account: PlayerAccount? = null,
    val profile: PlayerProfile? = null,
    val viewedProfile: PlayerProfile? = null,
    val games: List<PlayerGamePlayStat> = emptyList(),
    val leaderboard: List<PlayerLeaderboardEntry> = emptyList(),
    val searchResults: List<PlayerLeaderboardEntry> = emptyList(),
    val leaderboardSearchQuery: String = "",
    val rankInsights: PlayerRankInsights? = null,
    val activity: List<PlayerActivityDay> = emptyList(),
    val devices: List<PlayerDevice> = emptyList(),
    val trophies: PlayerTrophySummary = PlayerTrophySummary(),
    val cloudProfiles: List<CloudEmulatorProfile> = emptyList(),
    val friendships: List<ProfileFriendship> = emptyList(),
    val feed: List<ProfileFeedEvent> = emptyList(),
    val blockedUids: List<String> = emptyList(),
    val socialProfiles: Map<String, PlayerProfile> = emptyMap(),
    val isProUnlocked: Boolean = false,
    val isAuthLoading: Boolean = false,
    val isProfileLoading: Boolean = true,
    val isViewedProfileLoading: Boolean = false,
    val isViewedGamesLoadingMore: Boolean = false,
    val hasMoreViewedGames: Boolean = false,
    val isLeaderboardLoading: Boolean = false,
    val isLeaderboardLoadingMore: Boolean = false,
    val hasMoreLeaderboard: Boolean = true,
    val hasLoadedLeaderboard: Boolean = false,
    val leaderboardLoadFailed: Boolean = false,
    val isPlayerSearchLoading: Boolean = false,
    val isActivityLoading: Boolean = false,
    val hasLoadedActivity: Boolean = false,
    val hasAttemptedActivityLoad: Boolean = false,
    val isFeatureActionLoading: Boolean = false,
    val hasLoadedTrophies: Boolean = false,
    val messageKey: String? = null,
    val errorMessage: String? = null
)

class ProfileViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = PlayerProfileRepository(application)
    private val proPurchaseManager = ProPurchaseManager.getInstance(application)
    private val catalogRepository = VitaCatalogRepository(application)
    private val deviceRepository = ProfileDeviceRepository(application)
    private val cloudSettingsRepository = CloudEmulatorSettingsRepository(application)
    private val trophyRepository = VitaTrophyProfileRepository(application)
    private val socialRepository = ProfileSocialRepository()
    private val _uiState = MutableStateFlow(ProfileUiState())
    val uiState: StateFlow<ProfileUiState> = _uiState.asStateFlow()

    private var profileJob: Job? = null
    private var viewedProfileJob: Job? = null
    private var searchJob: Job? = null
    private var rankJob: Job? = null
    private var devicesJob: Job? = null
    private var friendshipsJob: Job? = null
    private var feedJob: Job? = null
    private var trophiesJob: Job? = null
    private var blocksJob: Job? = null
    private val leaderboardPager = LeaderboardPager { cursor, rankOffset ->
        repository.loadLeaderboardPage(cursor = cursor, rankOffset = rankOffset)
    }
    private var leaderboardJob: Job? = null
    private var allLeaderboard: List<PlayerLeaderboardEntry> = emptyList()
    private var allSearchResults: List<PlayerLeaderboardEntry> = emptyList()
    private var allFeed: List<ProfileFeedEvent> = emptyList()
    private var allFriendships: List<ProfileFriendship> = emptyList()
    private var viewedGamesCursor: DocumentSnapshot? = null

    init {
        viewModelScope.launch {
            repository.observeAuthState().collect { account ->
                profileJob?.cancel()
                viewedProfileJob?.cancel()
                searchJob?.cancel()
                rankJob?.cancel()
                devicesJob?.cancel()
                friendshipsJob?.cancel()
                feedJob?.cancel()
                trophiesJob?.cancel()
                blocksJob?.cancel()
                leaderboardJob?.cancel()
                leaderboardPager.invalidate()
                allLeaderboard = emptyList()
                allSearchResults = emptyList()
                allFeed = emptyList()
                allFriendships = emptyList()
                viewedGamesCursor = null
                _uiState.update {
                    it.copy(
                        account = account,
                        profile = null,
                        viewedProfile = null,
                        games = emptyList(),
                        leaderboard = emptyList(),
                        searchResults = emptyList(),
                        leaderboardSearchQuery = "",
                        rankInsights = null,
                        activity = emptyList(),
                        devices = emptyList(),
                        trophies = PlayerTrophySummary(),
                        cloudProfiles = emptyList(),
                        friendships = emptyList(),
                        feed = emptyList(),
                        blockedUids = emptyList(),
                        socialProfiles = emptyMap(),
                        isProfileLoading = account != null,
                        isViewedProfileLoading = false,
                        hasLoadedLeaderboard = false,
                        leaderboardLoadFailed = false,
                        hasMoreLeaderboard = true,
                        hasLoadedActivity = false,
                        hasAttemptedActivityLoad = false,
                        hasLoadedTrophies = false,
                        errorMessage = null
                    )
                }
                if (account != null) {
                    viewModelScope.launch {
                        runCatchingCancellable {
                            repository.ensureCurrentUserProfile()
                            deviceRepository.registerCurrentDevice()
                            val proState = proPurchaseManager.state.value
                            if (proState.isProUnlocked || proState.isPurchaseStatusVerified) {
                                repository.updateProMembership(proState.isProUnlocked)
                            }
                            TrophyCloudRepository(application).sync()
                        }
                        refreshTrophies()
                    }
                    observeProfile(account.uid)
                    observeProfileFeatures(account.uid)
                    refreshCloudProfiles()
                    refreshTrophies()
                }
            }
        }
        viewModelScope.launch {
            proPurchaseManager.state.collect { proState ->
                _uiState.update { it.copy(isProUnlocked = proState.isProUnlocked) }
                if (proState.isPurchaseStatusVerified && _uiState.value.account != null) {
                    viewModelScope.launch {
                        runCatchingCancellable { repository.updateProMembership(proState.isProUnlocked) }
                    }
                }
            }
        }
    }

    fun signIn(email: String, password: String) {
        runAuthAction {
            repository.signIn(email, password)
            "profile_signed_in"
        }
    }

    fun createAccount(email: String, password: String, displayName: String) {
        runAuthAction {
            repository.createAccount(email, password, displayName)
            "profile_account_created"
        }
    }

    fun signInWithGoogle(activity: Activity) {
        runAuthAction {
            repository.signInWithGoogle(activity)
            "profile_signed_in"
        }
    }

    fun sendPasswordReset(email: String) {
        runAuthAction {
            repository.sendPasswordReset(email)
            "profile_password_reset_sent"
        }
    }

    /** [onFinished] receives true on success, so the name dialog can stay open until the result is known (B073). */
    fun updateDisplayName(displayName: String, onFinished: (Boolean) -> Unit = {}) {
        runAuthAction(onFinished = onFinished) {
            repository.updateDisplayName(displayName)
            "profile_name_updated"
        }
    }

    /**
     * Re-authenticates, erases the account's cloud data and deletes the account (B066).
     * [password] is only needed for email/password accounts, [activity] for Google accounts.
     */
    fun deleteAccount(activity: Activity?, password: String?, onFinished: (Boolean) -> Unit = {}) {
        runAuthAction(onFinished = onFinished, errorText = ::deleteAccountErrorText) {
            repository.deleteAccount(activity, password)
            "profile_account_deleted"
        }
    }

    private fun deleteAccountErrorText(error: Throwable): String? = when (error) {
        is FirebaseAuthInvalidCredentialsException, is FirebaseAuthInvalidUserException ->
            getApplication<Application>().getString(R.string.profile_delete_account_wrong_password)
        else -> null
    }

    fun signOut() {
        viewedProfileJob?.cancel()
        repository.signOut()
        _uiState.update { it.copy(viewedProfile = null, isViewedProfileLoading = false, messageKey = "profile_signed_out", errorMessage = null) }
    }

    fun clearTransientMessages() {
        _uiState.update { it.copy(messageKey = null, errorMessage = null) }
    }

    fun onProfileTabSelected(tabName: String) {
        when (tabName) {
            "Achievements" -> refreshTrophies()
            "Leaderboard" -> _uiState.value.let { state ->
                if (!state.hasLoadedLeaderboard || state.leaderboardLoadFailed) refreshLeaderboard()
            }
            "Stats" -> if (!_uiState.value.hasLoadedActivity) loadActivity()
        }
    }

    fun setDevicePublic(deviceId: String, visible: Boolean) = runFeatureAction(
        successKey = if (visible) "profile_device_public" else "profile_device_private"
    ) {
        deviceRepository.setPublic(deviceId, visible)
    }

    fun deleteDevice(deviceId: String) = runFeatureAction("profile_device_deleted") {
        deviceRepository.deleteDevice(deviceId)
    }

    fun refreshCloudProfiles() {
        if (_uiState.value.account == null) return
        viewModelScope.launch {
            runCatchingCancellable { cloudSettingsRepository.loadProfiles() }
                .onSuccess { profiles -> _uiState.update { it.copy(cloudProfiles = profiles) } }
        }
    }

    fun saveCloudProfile(name: String, replaceProfileId: String? = null) = runFeatureAction("profile_cloud_saved") {
        cloudSettingsRepository.saveCurrent(name, replaceProfileId)
        _uiState.update { it.copy(cloudProfiles = cloudSettingsRepository.loadProfiles()) }
    }

    fun restoreCloudProfile(profileId: String) = runFeatureAction("profile_cloud_restored") {
        cloudSettingsRepository.restore(profileId)
    }

    fun deleteCloudProfile(profileId: String) = runFeatureAction("profile_cloud_deleted") {
        cloudSettingsRepository.delete(profileId)
        _uiState.update { it.copy(cloudProfiles = cloudSettingsRepository.loadProfiles()) }
    }

    fun sendFriendRequest(uid: String) = runFeatureAction("profile_friend_request_sent") {
        socialRepository.sendFriendRequest(uid)
    }

    fun acceptFriendRequest(friendshipId: String) = runFeatureAction("profile_friend_added") {
        socialRepository.acceptFriendRequest(friendshipId)
    }

    fun removeFriendship(friendshipId: String) = runFeatureAction("profile_friend_removed") {
        socialRepository.removeFriendship(friendshipId)
    }

    fun blockPlayer(uid: String) = runFeatureAction("profile_player_blocked") {
        socialRepository.block(uid)
        closeViewedProfile()
    }

    fun unblockPlayer(uid: String) = runFeatureAction("profile_player_unblocked") {
        socialRepository.unblock(uid)
    }

    fun refreshTrophies() {
        trophiesJob?.cancel()
        trophiesJob = viewModelScope.launch(Dispatchers.IO) {
            runCatchingCancellable { trophyRepository.loadSummary() }
                .onSuccess { summary ->
                    _uiState.update { it.copy(trophies = summary, hasLoadedTrophies = true) }
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            hasLoadedTrophies = true,
                            errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_trophies_load_failed)
                        )
                    }
                }
            runCatchingCancellable { trophyRepository.syncPublicSummary() }
        }
    }

    fun refreshLeaderboard() {
        leaderboardJob?.cancel()
        allLeaderboard = emptyList()
        _uiState.update {
            it.copy(
                leaderboard = emptyList(),
                isLeaderboardLoading = true,
                isLeaderboardLoadingMore = false,
                hasMoreLeaderboard = true,
                hasLoadedLeaderboard = false,
                leaderboardLoadFailed = false,
                errorMessage = null
            )
        }
        leaderboardJob = viewModelScope.launch { applyLeaderboardOutcome(leaderboardPager.reset()) }
    }

    fun loadMoreLeaderboard() {
        val state = _uiState.value
        if (!state.hasMoreLeaderboard || state.isLeaderboardLoading || state.isLeaderboardLoadingMore) return
        _uiState.update { it.copy(isLeaderboardLoadingMore = true) }
        leaderboardJob = viewModelScope.launch { applyLeaderboardOutcome(leaderboardPager.loadMore()) }
    }

    private fun applyLeaderboardOutcome(outcome: LeaderboardPager.Outcome) {
        when (outcome) {
            // A superseded request must not touch the state a newer request owns (B070).
            LeaderboardPager.Outcome.Ignored -> Unit
            is LeaderboardPager.Outcome.Loaded -> {
                allLeaderboard = if (outcome.replace) {
                    outcome.entries
                } else {
                    (allLeaderboard + outcome.entries).distinctBy { it.uid }
                }
                _uiState.update { state ->
                    state.copy(
                        leaderboard = allLeaderboard.excludingBlocked(state.blockedUids),
                        isLeaderboardLoading = false,
                        isLeaderboardLoadingMore = false,
                        hasMoreLeaderboard = outcome.hasMore,
                        hasLoadedLeaderboard = true,
                        leaderboardLoadFailed = false
                    )
                }
            }
            is LeaderboardPager.Outcome.Failed -> _uiState.update {
                it.copy(
                    isLeaderboardLoading = false,
                    isLeaderboardLoadingMore = false,
                    hasLoadedLeaderboard = true,
                    // A failed first page is retried the next time the tab is opened.
                    leaderboardLoadFailed = outcome.replace || it.leaderboardLoadFailed,
                    errorMessage = outcome.error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_leaderboard_load_failed)
                )
            }
        }
    }

    fun updatePlayerSearch(query: String) {
        searchJob?.cancel()
        val trimmed = query.truncateGraphemes()
        if (trimmed.length < 2) allSearchResults = emptyList()
        _uiState.update {
            it.copy(
                leaderboardSearchQuery = trimmed,
                searchResults = if (trimmed.length < 2) emptyList() else it.searchResults,
                isPlayerSearchLoading = trimmed.length >= 2
            )
        }
        if (trimmed.length < 2) return
        searchJob = viewModelScope.launch {
            delay(400.milliseconds)
            runCatchingCancellable { repository.searchPlayers(trimmed) }
                .onSuccess { results ->
                    if (_uiState.value.leaderboardSearchQuery == trimmed) {
                        allSearchResults = results
                        _uiState.update {
                            it.copy(
                                searchResults = results.excludingBlocked(it.blockedUids),
                                isPlayerSearchLoading = false
                            )
                        }
                    }
                }
                .onFailure { error ->
                    if (_uiState.value.leaderboardSearchQuery == trimmed) {
                        allSearchResults = emptyList()
                        _uiState.update {
                            it.copy(
                                searchResults = emptyList(),
                                isPlayerSearchLoading = false,
                                errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_player_search_failed)
                            )
                        }
                    }
                }
        }
    }

    fun loadActivity() {
        if (_uiState.value.isActivityLoading) return
        _uiState.update { it.copy(isActivityLoading = true, hasAttemptedActivityLoad = true) }
        viewModelScope.launch {
            runCatchingCancellable { repository.loadPlayerActivity() }
                .onSuccess { activity ->
                    _uiState.update {
                        it.copy(
                            activity = activity,
                            isActivityLoading = false,
                            hasLoadedActivity = true,
                            errorMessage = null
                        )
                    }
                }
                .onFailure {
                    _uiState.update {
                        it.copy(
                            isActivityLoading = false,
                            hasLoadedActivity = false,
                            errorMessage = getApplication<Application>().getString(R.string.profile_stats_load_failed)
                        )
                    }
                }
        }
    }

    fun updateProProfile(accent: String, favoriteGameKeys: List<String>) {
        if (!_uiState.value.isProUnlocked) return
        runAuthAction {
            repository.updateProProfile(accent, favoriteGameKeys)
            "profile_pro_updated"
        }
    }

    fun viewLeaderboardProfile(entry: PlayerLeaderboardEntry) {
        if (entry.uid == _uiState.value.account?.uid) {
            closeViewedProfile()
            return
        }
        val fallbackProfile = PlayerProfile(
            uid = entry.uid,
            email = null,
            displayName = entry.displayName,
            photoURL = entry.photoURL,
            totalPlayTimeMs = entry.totalPlayTimeMs,
            gamesPlayed = entry.gamesPlayed,
            lastPlayedTitle = entry.lastPlayedTitle,
            lastPlayedAtMs = null,
            games = emptyList(),
            playerTag = entry.playerTag,
            profileAccent = entry.profileAccent,
            isProMember = entry.isProMember
        )
        loadViewedProfile(entry.uid, fallbackProfile)
    }

    fun viewSocialProfile(uid: String) {
        val profile = _uiState.value.socialProfiles[uid] ?: return
        if (uid == _uiState.value.account?.uid) {
            closeViewedProfile()
            return
        }
        loadViewedProfile(uid, profile)
    }

    private fun loadViewedProfile(uid: String, fallbackProfile: PlayerProfile) {
        viewedProfileJob?.cancel()
        viewedGamesCursor = null
        _uiState.update { it.copy(viewedProfile = null, isViewedProfileLoading = true, errorMessage = null) }
        viewedProfileJob = viewModelScope.launch {
            runCatchingCancellable { repository.loadPublicProfile(uid) }
                .onSuccess { page ->
                    viewedGamesCursor = page.cursor
                    _uiState.update {
                        it.copy(
                            viewedProfile = page.profile ?: fallbackProfile,
                            isViewedProfileLoading = false,
                            hasMoreViewedGames = page.profile != null && page.hasMoreGames
                        )
                    }
                }
                .onFailure {
                    _uiState.update {
                        it.copy(
                            viewedProfile = fallbackProfile,
                            isViewedProfileLoading = false,
                            hasMoreViewedGames = false
                        )
                    }
                }
        }
    }

    fun loadMoreViewedGames() {
        val profile = _uiState.value.viewedProfile ?: return
        if (!_uiState.value.hasMoreViewedGames || _uiState.value.isViewedGamesLoadingMore) return
        _uiState.update { it.copy(isViewedGamesLoadingMore = true) }
        viewedProfileJob = viewModelScope.launch {
            runCatchingCancellable { repository.loadPublicGamesPage(profile.uid, viewedGamesCursor) }
                .onSuccess { page ->
                    viewedGamesCursor = page.cursor
                    _uiState.update { state ->
                        state.copy(
                            viewedProfile = state.viewedProfile?.copy(
                                games = (state.viewedProfile.games + page.games).distinctBy { it.gameKey }
                            ),
                            isViewedGamesLoadingMore = false,
                            hasMoreViewedGames = page.hasMore
                        )
                    }
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isViewedGamesLoadingMore = false,
                            errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_more_games_failed)
                        )
                    }
                }
        }
    }

    fun closeViewedProfile() {
        viewedProfileJob?.cancel()
        viewedProfileJob = null
        viewedGamesCursor = null
        _uiState.update {
            it.copy(
                viewedProfile = null,
                isViewedProfileLoading = false,
                isViewedGamesLoadingMore = false,
                hasMoreViewedGames = false
            )
        }
    }

    fun openGameDetails(game: PlayerGamePlayStat, onOpen: (Long) -> Unit) {
        viewModelScope.launch {
            val catalogId = withContext(Dispatchers.IO) {
                runCatchingCancellable {
                    catalogRepository.findBySerial(game.serial.orEmpty())?.igdbId
                        ?: catalogRepository.findBestMatch(game.title)?.igdbId
                }.getOrNull()
            }
            if (catalogId != null) {
                onOpen(catalogId)
            } else {
                _uiState.update {
                    it.copy(errorMessage = getApplication<Application>().getString(R.string.profile_catalog_game_not_found, game.title))
                }
            }
        }
    }

    private fun observeProfile(uid: String) {
        profileJob = viewModelScope.launch {
            // A listener error used to end this collection silently and leave the spinner up forever (B061).
            repository.observeProfile(uid).collectReporting(
                onError = { error ->
                    _uiState.update {
                        it.copy(
                            isProfileLoading = false,
                            errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_action_failed)
                        )
                    }
                }
            ) { profile ->
                val previousTotal = _uiState.value.profile?.totalPlayTimeMs
                _uiState.update {
                    it.copy(
                        profile = profile,
                        games = profile?.games.orEmpty(),
                        isProfileLoading = false
                    )
                }
                if (profile != null && profile.totalPlayTimeMs != previousTotal) {
                    loadRankInsights(profile.totalPlayTimeMs)
                    refreshTrophies()
                }
            }
        }
    }

    private fun observeProfileFeatures(uid: String) {
        devicesJob = viewModelScope.launch {
            deviceRepository.observeDevices().collectReporting(onError = {}) { devices ->
                _uiState.update { it.copy(devices = devices) }
            }
        }
        friendshipsJob = viewModelScope.launch {
            socialRepository.observeFriendships().collectReporting(onError = {}) { friendships ->
                allFriendships = friendships
                republishBlockFiltered()
                hydrateSocialProfiles(friendships.map { it.otherUid })
            }
        }
        feedJob = viewModelScope.launch {
            socialRepository.observeFeed(uid).collectReporting(onError = {}) { feed ->
                allFeed = feed
                republishBlockFiltered()
            }
        }
        blocksJob = viewModelScope.launch {
            socialRepository.observeBlockedUids().collectReporting(onError = {}) { blocked ->
                _uiState.update { it.copy(blockedUids = blocked) }
                republishBlockFiltered()
                hydrateSocialProfiles(blocked)
            }
        }
    }

    /** Re-derives every list that must hide blocked players from the unfiltered source lists (B069). */
    private fun republishBlockFiltered() {
        _uiState.update { state ->
            val blocked = state.blockedUids
            state.copy(
                leaderboard = allLeaderboard.excludingBlocked(blocked),
                searchResults = allSearchResults.excludingBlocked(blocked),
                feed = allFeed.excludingBlockedAuthors(blocked),
                friendships = allFriendships.excludingBlockedFriends(blocked)
            )
        }
    }

    private suspend fun hydrateSocialProfiles(uids: List<String>) {
        val missing = uids.distinct().take(50).filterNot { it in _uiState.value.socialProfiles }
        if (missing.isEmpty()) return
        val loaded = runCatchingCancellable { repository.loadPublicProfileSummaries(missing) }.getOrDefault(emptyMap())
        if (loaded.isNotEmpty()) {
            _uiState.update { it.copy(socialProfiles = it.socialProfiles + loaded) }
        }
    }

    private fun runFeatureAction(successKey: String, action: suspend () -> Unit) {
        viewModelScope.launch {
            _uiState.update { it.copy(isFeatureActionLoading = true, messageKey = null, errorMessage = null) }
            runCatchingCancellable { action() }
                .onSuccess { _uiState.update { it.copy(isFeatureActionLoading = false, messageKey = successKey) } }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isFeatureActionLoading = false,
                            errorMessage = featureErrorText(error)
                        )
                    }
                }
        }
    }

    private fun featureErrorText(error: Throwable): String {
        val app = getApplication<Application>()
        return when (error) {
            is FriendRequestBlockedException -> app.getString(R.string.profile_friend_request_blocked)
            else -> error.localizedMessage ?: app.getString(R.string.profile_action_failed)
        }
    }

    private fun loadRankInsights(totalPlayTimeMs: Long) {
        rankJob?.cancel()
        rankJob = viewModelScope.launch {
            runCatchingCancellable { repository.loadRankInsights(totalPlayTimeMs) }
                .onSuccess { insights -> _uiState.update { it.copy(rankInsights = insights) } }
        }
    }

    private fun runAuthAction(
        onFinished: (Boolean) -> Unit = {},
        errorText: (Throwable) -> String? = { null },
        action: suspend () -> String
    ) {
        viewModelScope.launch {
            _uiState.update { it.copy(isAuthLoading = true, messageKey = null, errorMessage = null) }
            runCatchingCancellable { action() }
                .onSuccess { messageKey ->
                    _uiState.update { it.copy(isAuthLoading = false, messageKey = messageKey) }
                    onFinished(true)
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isAuthLoading = false,
                            // The user dismissing a re-authentication prompt is not an error.
                            errorMessage = if (error is ReauthCancelledException) null else {
                                errorText(error)
                                    ?: error.localizedMessage
                                    ?: getApplication<Application>().getString(R.string.profile_action_failed)
                            }
                        )
                    }
                    onFinished(false)
                }
        }
    }
}
