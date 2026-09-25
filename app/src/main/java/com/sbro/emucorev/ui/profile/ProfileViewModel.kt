package com.sbro.emucorev.ui.profile

import android.app.Activity
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.firestore.DocumentSnapshot
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.data.CloudEmulatorProfile
import com.sbro.emucorev.data.CloudEmulatorSettingsRepository
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
import com.sbro.emucorev.data.TrophyCloudRepository
import com.sbro.emucorev.data.VitaCatalogRepository
import com.sbro.emucorev.data.VitaTrophyProfileRepository
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
    private var leaderboardCursor: DocumentSnapshot? = null
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
                leaderboardCursor = null
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
                        hasMoreLeaderboard = true,
                        hasLoadedActivity = false,
                        hasAttemptedActivityLoad = false,
                        hasLoadedTrophies = false,
                        errorMessage = null
                    )
                }
                if (account != null) {
                    viewModelScope.launch {
                        runCatching {
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
                        runCatching { repository.updateProMembership(proState.isProUnlocked) }
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

    fun updateDisplayName(displayName: String) {
        runAuthAction {
            repository.updateDisplayName(displayName)
            "profile_name_updated"
        }
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
            "Leaderboard" -> if (!_uiState.value.hasLoadedLeaderboard) refreshLeaderboard()
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
            runCatching { cloudSettingsRepository.loadProfiles() }
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
            runCatching { trophyRepository.loadSummary() }
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
            runCatching { trophyRepository.syncPublicSummary() }
        }
    }

    fun refreshLeaderboard() {
        leaderboardCursor = null
        _uiState.update {
            it.copy(
                leaderboard = emptyList(),
                isLeaderboardLoading = true,
                isLeaderboardLoadingMore = false,
                hasMoreLeaderboard = true,
                hasLoadedLeaderboard = false,
                errorMessage = null
            )
        }
        loadLeaderboardPage(reset = true)
    }

    fun loadMoreLeaderboard() {
        val state = _uiState.value
        if (!state.hasMoreLeaderboard || state.isLeaderboardLoading || state.isLeaderboardLoadingMore) return
        _uiState.update { it.copy(isLeaderboardLoadingMore = true) }
        loadLeaderboardPage(reset = false)
    }

    private fun loadLeaderboardPage(reset: Boolean) {
        viewModelScope.launch {
            runCatching {
                repository.loadLeaderboardPage(
                    cursor = if (reset) null else leaderboardCursor,
                    rankOffset = if (reset) 0 else _uiState.value.leaderboard.size
                )
            }.onSuccess { page ->
                leaderboardCursor = page.cursor
                _uiState.update { state ->
                    state.copy(
                        leaderboard = if (reset) page.entries else (state.leaderboard + page.entries).distinctBy { it.uid },
                        isLeaderboardLoading = false,
                        isLeaderboardLoadingMore = false,
                        hasMoreLeaderboard = page.hasMore,
                        hasLoadedLeaderboard = true
                    )
                }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        isLeaderboardLoading = false,
                        isLeaderboardLoadingMore = false,
                        hasLoadedLeaderboard = true,
                        errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_leaderboard_load_failed)
                    )
                }
            }
        }
    }

    fun updatePlayerSearch(query: String) {
        searchJob?.cancel()
        val trimmed = query.take(32)
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
            runCatching { repository.searchPlayers(trimmed) }
                .onSuccess { results ->
                    if (_uiState.value.leaderboardSearchQuery == trimmed) {
                        _uiState.update { it.copy(searchResults = results, isPlayerSearchLoading = false) }
                    }
                }
                .onFailure { error ->
                    if (_uiState.value.leaderboardSearchQuery == trimmed) {
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
            runCatching { repository.loadPlayerActivity() }
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
            runCatching { repository.loadPublicProfile(uid) }
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
            runCatching { repository.loadPublicGamesPage(profile.uid, viewedGamesCursor) }
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
                runCatching {
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
            runCatching {
                repository.observeProfile(uid).collect { profile ->
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
    }

    private fun observeProfileFeatures(uid: String) {
        devicesJob = viewModelScope.launch {
            runCatching {
                deviceRepository.observeDevices().collect { devices ->
                    _uiState.update { it.copy(devices = devices) }
                }
            }
        }
        friendshipsJob = viewModelScope.launch {
            runCatching {
                socialRepository.observeFriendships().collect { friendships ->
                    _uiState.update { it.copy(friendships = friendships) }
                    hydrateSocialProfiles(friendships.map { it.otherUid })
                }
            }
        }
        feedJob = viewModelScope.launch {
            runCatching {
                socialRepository.observeFeed(uid).collect { feed ->
                    _uiState.update { it.copy(feed = feed) }
                }
            }
        }
        blocksJob = viewModelScope.launch {
            runCatching {
                socialRepository.observeBlockedUids().collect { blocked ->
                    _uiState.update { it.copy(blockedUids = blocked) }
                    hydrateSocialProfiles(blocked)
                }
            }
        }
    }

    private suspend fun hydrateSocialProfiles(uids: List<String>) {
        val missing = uids.distinct().take(50).filterNot { it in _uiState.value.socialProfiles }
        if (missing.isEmpty()) return
        val loaded = runCatching { repository.loadPublicProfileSummaries(missing) }.getOrDefault(emptyMap())
        if (loaded.isNotEmpty()) {
            _uiState.update { it.copy(socialProfiles = it.socialProfiles + loaded) }
        }
    }

    private fun runFeatureAction(successKey: String, action: suspend () -> Unit) {
        viewModelScope.launch {
            _uiState.update { it.copy(isFeatureActionLoading = true, messageKey = null, errorMessage = null) }
            runCatching { action() }
                .onSuccess { _uiState.update { it.copy(isFeatureActionLoading = false, messageKey = successKey) } }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isFeatureActionLoading = false,
                            errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_action_failed)
                        )
                    }
                }
        }
    }

    private fun loadRankInsights(totalPlayTimeMs: Long) {
        rankJob?.cancel()
        rankJob = viewModelScope.launch {
            runCatching { repository.loadRankInsights(totalPlayTimeMs) }
                .onSuccess { insights -> _uiState.update { it.copy(rankInsights = insights) } }
        }
    }

    private fun runAuthAction(action: suspend () -> String) {
        viewModelScope.launch {
            _uiState.update { it.copy(isAuthLoading = true, messageKey = null, errorMessage = null) }
            runCatching { action() }
                .onSuccess { messageKey ->
                    _uiState.update { it.copy(isAuthLoading = false, messageKey = messageKey) }
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isAuthLoading = false,
                            errorMessage = error.localizedMessage ?: getApplication<Application>().getString(R.string.profile_action_failed)
                        )
                    }
                }
        }
    }
}
