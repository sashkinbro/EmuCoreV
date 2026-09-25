package com.sbro.emucorev.data

import android.app.Activity
import android.content.Context
import android.util.Log
import com.google.android.gms.tasks.Task
import com.google.firebase.Timestamp
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.auth.GoogleAuthProvider
import com.google.firebase.auth.OAuthProvider
import com.google.firebase.auth.UserProfileChangeRequest
import com.google.firebase.firestore.AggregateSource
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldPath
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.Query
import com.google.firebase.firestore.SetOptions
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

private const val DEFAULT_PROFILE_ACCENT = "gold"

data class PlayerAccount(
    val uid: String,
    val email: String?,
    val displayName: String,
    val photoURL: String?
)

data class PlayerProfile(
    val uid: String,
    val email: String?,
    val displayName: String,
    val photoURL: String?,
    val totalPlayTimeMs: Long,
    val gamesPlayed: Int,
    val lastPlayedTitle: String?,
    val lastPlayedAtMs: Long?,
    val games: List<PlayerGamePlayStat>,
    val playerTag: String = "",
    val profileAccent: String = DEFAULT_PROFILE_ACCENT,
    val favoriteGameKeys: List<String> = emptyList(),
    val isProMember: Boolean = false,
    val publicDevice: PublicPlayerDevice? = null,
    val achievementCount: Int = 0,
    val achievementPoints: Int = 0
)

data class PlayerGamePlayStat(
    val gameKey: String,
    val title: String,
    val serial: String?,
    val coverArtPath: String?,
    val totalPlayTimeMs: Long,
    val sessions: Int,
    val lastPlayedAtMs: Long?
)

data class PlayerLeaderboardEntry(
    val rank: Int?,
    val uid: String,
    val displayName: String,
    val photoURL: String?,
    val totalPlayTimeMs: Long,
    val gamesPlayed: Int,
    val lastPlayedTitle: String?,
    val playerTag: String,
    val profileAccent: String,
    val isProMember: Boolean
)

data class PlayerLeaderboardPage(
    val entries: List<PlayerLeaderboardEntry>,
    val cursor: DocumentSnapshot?,
    val hasMore: Boolean
)

data class PublicPlayerProfilePage(
    val profile: PlayerProfile?,
    val cursor: DocumentSnapshot?,
    val hasMoreGames: Boolean
)

data class PublicGamesPage(
    val games: List<PlayerGamePlayStat>,
    val cursor: DocumentSnapshot?,
    val hasMore: Boolean
)

data class PlayerActivityDay(
    val day: String,
    val playTimeMs: Long,
    val sessions: Int,
    val gameKeys: List<String>
)

data class PlayerRankInsights(
    val rank: Int,
    val totalPlayers: Int,
    val percentile: Int
)

data class PlayerPlayTimeDelta(
    val titleId: String,
    val title: String,
    val coverArtPath: String?,
    val durationMs: Long,
    val sessionCount: Long = 0L,
    val lastPlayedAtMs: Long = System.currentTimeMillis()
)

class PlayerProfileRepository(context: Context) {

    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()
    private val catalogRepository = VitaCatalogRepository(appContext)
    private val coverUrlCache = java.util.concurrent.ConcurrentHashMap<String, String>()

    fun observeAuthState(): Flow<PlayerAccount?> = callbackFlow {
        val listener = FirebaseAuth.AuthStateListener { firebaseAuth ->
            trySend(firebaseAuth.currentUser?.toPlayerAccount())
        }
        auth.addAuthStateListener(listener)
        trySend(auth.currentUser?.toPlayerAccount())
        awaitClose { auth.removeAuthStateListener(listener) }
    }

    fun hasSignedInUser(): Boolean = auth.currentUser != null

    suspend fun ensureCurrentUserProfile() {
        auth.currentUser?.let { user ->
            ensureUserProfile(
                uid = user.uid,
                email = user.email,
                displayName = user.displayName.cleanDisplayName(user.email),
                photoURL = user.bestPhotoUrl()
            )
        }
    }

    fun observeProfile(uid: String): Flow<PlayerProfile?> = callbackFlow {
        val registration = firestore.collection(USERS_COLLECTION)
            .document(uid)
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    close(error)
                    return@addSnapshotListener
                }
                trySend(snapshot?.toPlayerProfile())
            }
        awaitClose { registration.remove() }
    }

    suspend fun loadLeaderboardPage(
        cursor: DocumentSnapshot? = null,
        rankOffset: Int = 0
    ): PlayerLeaderboardPage {
        if (rankOffset >= LEADERBOARD_MAX_ENTRIES) {
            return PlayerLeaderboardPage(emptyList(), cursor, hasMore = false)
        }
        var query: Query = firestore.collection(LEADERBOARD_COLLECTION)
            .orderBy(FIELD_TOTAL_PLAY_TIME_MS, Query.Direction.DESCENDING)
            .limit(LEADERBOARD_PAGE_SIZE)
        cursor?.let { query = query.startAfter(it) }

        val snapshot = query.get().await()
        val remaining = LEADERBOARD_MAX_ENTRIES - rankOffset
        val documents = snapshot.documents.take(remaining)
        val entries = documents.mapIndexedNotNull { index, document ->
            document.toPlayerLeaderboardEntry(rankOffset + index + 1)
        }
        return PlayerLeaderboardPage(
            entries = entries,
            cursor = documents.lastOrNull(),
            hasMore = snapshot.size().toLong() == LEADERBOARD_PAGE_SIZE &&
                rankOffset + entries.size < LEADERBOARD_MAX_ENTRIES
        )
    }

    suspend fun searchPlayers(rawQuery: String): List<PlayerLeaderboardEntry> {
        val trimmed = rawQuery.trim()
        if (trimmed.length < MIN_PLAYER_SEARCH_LENGTH) return emptyList()
        val collection = firestore.collection(LEADERBOARD_COLLECTION)
        if (trimmed.startsWith(PLAYER_TAG_PREFIX, ignoreCase = true)) {
            return collection
                .whereEqualTo(FIELD_PLAYER_TAG, trimmed.uppercase(Locale.ROOT))
                .limit(PLAYER_SEARCH_LIMIT)
                .get()
                .await()
                .documents
                .mapNotNull { it.toPlayerLeaderboardEntry(rank = null) }
        }

        val normalized = normalizeSearchName(trimmed)
        return collection
            .orderBy(FIELD_SEARCH_NAME, Query.Direction.ASCENDING)
            .startAt(normalized)
            .endAt(normalized + "\uf8ff")
            .limit(PLAYER_SEARCH_LIMIT)
            .get()
            .await()
            .documents
            .mapNotNull { it.toPlayerLeaderboardEntry(rank = null) }
    }

    suspend fun loadRankInsights(totalPlayTimeMs: Long): PlayerRankInsights {
        val collection = firestore.collection(LEADERBOARD_COLLECTION)
        val ahead = collection
            .whereGreaterThan(FIELD_TOTAL_PLAY_TIME_MS, totalPlayTimeMs)
            .count()
            .get(AggregateSource.SERVER)
            .await()
            .count
        val total = collection.count().get(AggregateSource.SERVER).await().count.coerceAtLeast(1L)
        val rank = (ahead + 1L).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
        val percentile = (((total - rank + 1L).coerceAtLeast(1L) * 100L) / total)
            .coerceIn(1L, 100L)
            .toInt()
        return PlayerRankInsights(rank = rank, totalPlayers = total.toInt(), percentile = percentile)
    }

    suspend fun loadPublicProfile(uid: String): PublicPlayerProfilePage {
        val profileDocument = firestore.collection(PUBLIC_PROFILES_COLLECTION).document(uid).get().await()
        if (!profileDocument.exists()) return PublicPlayerProfilePage(null, null, false)
        val gamesPage = loadPublicGamesPage(uid = uid, cursor = null)
        val gamesByKey = gamesPage.games.associateByTo(LinkedHashMap()) { it.gameKey }
        profileDocument.stringList(FIELD_FAVORITE_GAME_KEYS).forEach { gameKey ->
            if (gameKey !in gamesByKey) {
                firestore.collection(PUBLIC_PROFILES_COLLECTION)
                    .document(uid)
                    .collection(PUBLIC_GAMES_COLLECTION)
                    .document(gameKey)
                    .get()
                    .await()
                    .data
                    .toPlayerGamePlayStat(gameKey)
                    ?.let { gamesByKey[gameKey] = it }
            }
        }
        return PublicPlayerProfilePage(
            profile = profileDocument.toPublicPlayerProfile(gamesByKey.values.toList()),
            cursor = gamesPage.cursor,
            hasMoreGames = gamesPage.hasMore
        )
    }

    suspend fun loadPublicProfileSummaries(uids: List<String>): Map<String, PlayerProfile> {
        val uniqueUids = uids.filter { it.isNotBlank() }.distinct().take(50)
        if (uniqueUids.isEmpty()) return emptyMap()
        return buildMap {
            uniqueUids.chunked(10).forEach { chunk ->
                firestore.collection(PUBLIC_PROFILES_COLLECTION)
                    .whereIn(FieldPath.documentId(), chunk)
                    .get().await().documents
                    .forEach { document -> document.toPublicPlayerProfile(emptyList())?.let { put(it.uid, it) } }
            }
        }
    }

    suspend fun loadPublicGamesPage(
        uid: String,
        cursor: DocumentSnapshot?
    ): PublicGamesPage {
        var query: Query = firestore.collection(PUBLIC_PROFILES_COLLECTION)
            .document(uid)
            .collection(PUBLIC_GAMES_COLLECTION)
            .orderBy(GAME_TOTAL_MS, Query.Direction.DESCENDING)
            .limit(PUBLIC_GAMES_PAGE_SIZE)
        cursor?.let { query = query.startAfter(it) }
        val snapshot = query.get().await()
        return PublicGamesPage(
            games = snapshot.documents.mapNotNull { it.data.toPlayerGamePlayStat(it.id) },
            cursor = snapshot.documents.lastOrNull(),
            hasMore = snapshot.size().toLong() == PUBLIC_GAMES_PAGE_SIZE
        )
    }

    suspend fun loadPlayerActivity(limit: Long = ACTIVITY_HISTORY_DAYS): List<PlayerActivityDay> {
        val uid = auth.currentUser?.uid ?: return emptyList()
        return firestore.collection(USERS_COLLECTION)
            .document(uid)
            .collection(ACTIVITY_COLLECTION)
            .orderBy(FieldPath.documentId(), Query.Direction.DESCENDING)
            .limit(limit)
            .get()
            .await()
            .documents
            .map { document ->
                PlayerActivityDay(
                    day = document.id,
                    playTimeMs = document.getLong(ACTIVITY_PLAY_TIME_MS) ?: 0L,
                    sessions = (document.getLong(ACTIVITY_SESSIONS) ?: 0L).toInt(),
                    gameKeys = document.stringList(ACTIVITY_GAME_KEYS)
                )
            }
            .sortedBy { it.day }
    }

    suspend fun updateProProfile(profileAccent: String, favoriteGameKeys: List<String>) {
        val user = auth.currentUser ?: return
        val accent = profileAccent.takeIf { it in PROFILE_ACCENTS } ?: DEFAULT_PROFILE_ACCENT
        val favorites = favoriteGameKeys.distinct().take(MAX_FAVORITE_GAMES)
        val profilePatch = mapOf(
            FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
            FIELD_PROFILE_ACCENT to accent,
            FIELD_FAVORITE_GAME_KEYS to favorites,
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        val leaderboardPatch = mapOf(
            FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
            FIELD_PROFILE_ACCENT to accent,
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        firestore.runBatch { batch ->
            batch.set(firestore.collection(USERS_COLLECTION).document(user.uid), profilePatch, SetOptions.merge())
            batch.set(firestore.collection(PUBLIC_PROFILES_COLLECTION).document(user.uid), profilePatch, SetOptions.merge())
            batch.set(firestore.collection(LEADERBOARD_COLLECTION).document(user.uid), leaderboardPatch, SetOptions.merge())
        }.await()
    }

    suspend fun updateProMembership(enabled: Boolean) {
        val uid = auth.currentUser?.uid ?: return
        val patch = mapOf(
            FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
            FIELD_PRO_MEMBER to enabled,
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        firestore.runBatch { batch ->
            batch.set(firestore.collection(PUBLIC_PROFILES_COLLECTION).document(uid), patch, SetOptions.merge())
            batch.set(firestore.collection(LEADERBOARD_COLLECTION).document(uid), patch, SetOptions.merge())
        }.await()
    }

    suspend fun updateAchievementSummary(achievementCount: Int, achievementPoints: Int) {
        val uid = auth.currentUser?.uid ?: return
        val patch = mapOf(
            FIELD_ACHIEVEMENT_COUNT to achievementCount.coerceAtLeast(0),
            FIELD_ACHIEVEMENT_POINTS to achievementPoints.coerceAtLeast(0),
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        firestore.runBatch { batch ->
            batch.set(firestore.collection(USERS_COLLECTION).document(uid), patch, SetOptions.merge())
            batch.set(firestore.collection(PUBLIC_PROFILES_COLLECTION).document(uid), patch, SetOptions.merge())
        }.await()
    }

    suspend fun signIn(email: String, password: String) {
        auth.signInWithEmailAndPassword(email.trim(), password).await()
        auth.currentUser?.let { user ->
            ensureUserProfile(user.uid, user.email, user.displayName.cleanDisplayName(user.email), user.bestPhotoUrl())
        }
    }

    suspend fun signInWithGoogle(activity: Activity) {
        val provider = OAuthProvider.newBuilder(GoogleAuthProvider.PROVIDER_ID)
            .addCustomParameter("prompt", "select_account")
            .build()
        val result = (auth.pendingAuthResult ?: auth.startActivityForSignInWithProvider(activity, provider)).await()
        result.user?.let { user ->
            ensureUserProfile(user.uid, user.email, user.displayName.cleanDisplayName(user.email), user.bestPhotoUrl())
        }
    }

    suspend fun createAccount(email: String, password: String, displayName: String) {
        val result = auth.createUserWithEmailAndPassword(email.trim(), password).await()
        val user = result.user ?: return
        val cleanName = displayName.cleanDisplayName(user.email)
        user.updateProfile(
            UserProfileChangeRequest.Builder()
                .setDisplayName(cleanName)
                .build()
        ).await()
        ensureUserProfile(user.uid, user.email, cleanName, user.bestPhotoUrl())
    }

    suspend fun sendPasswordReset(email: String) {
        auth.sendPasswordResetEmail(email.trim()).await()
    }

    suspend fun updateDisplayName(displayName: String) {
        val user = auth.currentUser ?: return
        val cleanName = displayName.cleanDisplayName(user.email)
        user.updateProfile(
            UserProfileChangeRequest.Builder()
                .setDisplayName(cleanName)
                .build()
        ).await()
        val userPatch = mapOf(
            FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
            FIELD_DISPLAY_NAME to cleanName,
            FIELD_SEARCH_NAME to normalizeSearchName(cleanName),
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        firestore.runBatch { batch ->
            batch.set(firestore.collection(USERS_COLLECTION).document(user.uid), userPatch, SetOptions.merge())
            batch.set(firestore.collection(PUBLIC_PROFILES_COLLECTION).document(user.uid), userPatch, SetOptions.merge())
            batch.set(firestore.collection(LEADERBOARD_COLLECTION).document(user.uid), userPatch, SetOptions.merge())
        }.await()
    }

    fun signOut() {
        auth.signOut()
    }

    suspend fun recordPlayTimeBatch(entries: List<PlayerPlayTimeDelta>) {
        val user = auth.currentUser ?: return
        val validEntries = entries
            .filter { entry ->
                entry.titleId.isNotBlank() && entry.durationMs > 0L
            }
            .groupBy { entry -> buildGameKey(entry.titleId) }
            .map { (gameKey, gameEntries) ->
                val latestEntry = gameEntries.maxBy { it.lastPlayedAtMs }
                gameKey to latestEntry.copy(
                    durationMs = gameEntries.sumOf { it.durationMs },
                    sessionCount = gameEntries.sumOf { it.sessionCount }
                )
            }
        if (validEntries.isEmpty()) return

        val latestEntry = validEntries.maxBy { it.second.lastPlayedAtMs }.second
        val latestTitle = latestEntry.title.ifBlank { latestEntry.titleId }
        val cleanName = user.displayName.cleanDisplayName(user.email)
        val photoURL = user.bestPhotoUrl().orEmpty()
        val searchName = normalizeSearchName(cleanName)
        val playerTag = buildPlayerTag(user.uid)
        val userRef = firestore.collection(USERS_COLLECTION).document(user.uid)
        val publicProfileRef = firestore.collection(PUBLIC_PROFILES_COLLECTION).document(user.uid)
        val leaderboardRef = firestore.collection(LEADERBOARD_COLLECTION).document(user.uid)
        val activityGroups = validEntries.groupBy { (_, entry) -> activityDayKey(entry.lastPlayedAtMs) }
        val activityRefs = activityGroups.mapValues { (day, _) ->
            userRef.collection(ACTIVITY_COLLECTION).document(day)
        }

        firestore.runTransaction { transaction ->
            val snapshot = transaction.get(userRef)
            val activitySnapshots = activityRefs.mapValues { (_, reference) -> transaction.get(reference) }
            val existingGames = snapshot.getGamesMap().toMutableMap()
            val changedGames = LinkedHashMap<String, Map<String, Any>>()
            validEntries.forEach { (gameKey, entry) ->
                val cleanTitle = entry.title.ifBlank { entry.titleId }
                val existingGame = existingGames[gameKey].toGameMap()
                val nextGame = buildMap<String, Any> {
                    put(GAME_TITLE, cleanTitle.take(MAX_GAME_TITLE_LENGTH))
                    put(GAME_TOTAL_MS, existingGame.longValue(GAME_TOTAL_MS) + entry.durationMs)
                    put(GAME_SESSIONS, existingGame.longValue(GAME_SESSIONS) + entry.sessionCount)
                    put(GAME_LAST_PLAYED_AT_MS, entry.lastPlayedAtMs)
                    entry.titleId.takeIf { it.isNotBlank() }?.let { put(GAME_SERIAL, it.take(MAX_SERIAL_LENGTH)) }
                    buildShareableCoverPath(entry.coverArtPath, entry.titleId, cleanTitle)?.let {
                        put(GAME_COVER_ART_PATH, it.take(MAX_COVER_PATH_LENGTH))
                    }
                }
                existingGames[gameKey] = nextGame
                changedGames[gameKey] = nextGame
            }

            val totalPlayTimeMs = (snapshot.getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: 0L) + validEntries.sumOf { it.second.durationMs }
            val gamesPlayed = existingGames.size.toLong()
            val profileAccent = snapshot.getString(FIELD_PROFILE_ACCENT)
                ?.takeIf { it in PROFILE_ACCENTS }
                ?: DEFAULT_PROFILE_ACCENT
            val favoriteGameKeys = snapshot.stringList(FIELD_FAVORITE_GAME_KEYS)
                .distinct()
                .take(MAX_FAVORITE_GAMES)
            val userData = mapOf(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to user.uid,
                FIELD_EMAIL to (user.email ?: ""),
                FIELD_DISPLAY_NAME to cleanName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL,
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_LAST_PLAYED_TITLE to latestTitle.take(MAX_GAME_TITLE_LENGTH),
                FIELD_LAST_PLAYED_AT_MS to latestEntry.lastPlayedAtMs,
                FIELD_GAMES to existingGames,
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_FAVORITE_GAME_KEYS to favoriteGameKeys,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp(),
                FIELD_CREATED_AT to (snapshot.getTimestamp(FIELD_CREATED_AT) ?: FieldValue.serverTimestamp())
            )
            val leaderboardData = mapOf(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to user.uid,
                FIELD_DISPLAY_NAME to cleanName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL,
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_LAST_PLAYED_TITLE to latestTitle.take(MAX_GAME_TITLE_LENGTH),
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp()
            )
            val publicProfileData = mapOf(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to user.uid,
                FIELD_DISPLAY_NAME to cleanName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL,
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_LAST_PLAYED_TITLE to latestTitle.take(MAX_GAME_TITLE_LENGTH),
                FIELD_LAST_PLAYED_AT_MS to latestEntry.lastPlayedAtMs,
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_FAVORITE_GAME_KEYS to favoriteGameKeys,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp()
            )

            transaction.set(userRef, userData, SetOptions.merge())
            transaction.set(publicProfileRef, publicProfileData, SetOptions.merge())
            transaction.set(leaderboardRef, leaderboardData, SetOptions.merge())
            changedGames.forEach { (gameKey, gameData) ->
                transaction.set(
                    publicProfileRef.collection(PUBLIC_GAMES_COLLECTION).document(gameKey),
                    gameData,
                    SetOptions.merge()
                )
            }
            activityGroups.forEach { (day, entriesForDay) ->
                val activitySnapshot = activitySnapshots.getValue(day)
                val existingGameKeys = activitySnapshot.stringList(ACTIVITY_GAME_KEYS)
                val newGameKeys = entriesForDay.map { it.first }
                transaction.set(
                    activityRefs.getValue(day),
                    mapOf(
                        ACTIVITY_PLAY_TIME_MS to ((activitySnapshot.getLong(ACTIVITY_PLAY_TIME_MS) ?: 0L) +
                            entriesForDay.sumOf { it.second.durationMs }),
                        ACTIVITY_SESSIONS to ((activitySnapshot.getLong(ACTIVITY_SESSIONS) ?: 0L) +
                            entriesForDay.sumOf { it.second.sessionCount }),
                        ACTIVITY_GAME_KEYS to (existingGameKeys + newGameKeys).distinct().take(MAX_DAILY_GAMES),
                        FIELD_UPDATED_AT to FieldValue.serverTimestamp()
                    ),
                    SetOptions.merge()
                )
            }
        }.await()
        // Trophy sync is additive and must never make play-time persistence fail.
        runCatching { VitaTrophyProfileRepository(appContext).syncPublicSummary() }
    }

    private suspend fun ensureUserProfile(uid: String, email: String?, displayName: String, photoURL: String?) {
        val userRef = firestore.collection(USERS_COLLECTION).document(uid)
        val publicProfileRef = firestore.collection(PUBLIC_PROFILES_COLLECTION).document(uid)
        val leaderboardRef = firestore.collection(LEADERBOARD_COLLECTION).document(uid)
        firestore.runTransaction { transaction ->
            val snapshot = transaction.get(userRef)
            val existingGames = snapshot.getGamesMap()
            val totalPlayTimeMs = snapshot.getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: 0L
            val gamesPlayed = if (snapshot.exists()) {
                snapshot.getLong(FIELD_GAMES_PLAYED) ?: existingGames.size.toLong()
            } else {
                0L
            }
            val playerTag = buildPlayerTag(uid)
            val searchName = normalizeSearchName(displayName)
            val profileAccent = snapshot.getString(FIELD_PROFILE_ACCENT)
                ?.takeIf { it in PROFILE_ACCENTS }
                ?: DEFAULT_PROFILE_ACCENT
            val favoriteGameKeys = snapshot.stringList(FIELD_FAVORITE_GAME_KEYS)
                .distinct()
                .take(MAX_FAVORITE_GAMES)
            val userData = mutableMapOf<String, Any?>(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to uid,
                FIELD_EMAIL to (email ?: ""),
                FIELD_DISPLAY_NAME to displayName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL.orEmpty(),
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_GAMES to existingGames,
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_FAVORITE_GAME_KEYS to favoriteGameKeys,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp()
            )
            if (!snapshot.exists()) {
                userData[FIELD_CREATED_AT] = FieldValue.serverTimestamp()
            }
            val leaderboardData = mapOf(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to uid,
                FIELD_DISPLAY_NAME to displayName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL.orEmpty(),
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_LAST_PLAYED_TITLE to snapshot.getString(FIELD_LAST_PLAYED_TITLE).orEmpty(),
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp()
            )
            val publicProfileData = mapOf(
                FIELD_PROFILE_SCHEMA_VERSION to PROFILE_SCHEMA_VERSION,
                FIELD_UID to uid,
                FIELD_DISPLAY_NAME to displayName,
                FIELD_SEARCH_NAME to searchName,
                FIELD_PLAYER_TAG to playerTag,
                FIELD_PHOTO_URL to photoURL.orEmpty(),
                FIELD_TOTAL_PLAY_TIME_MS to totalPlayTimeMs,
                FIELD_GAMES_PLAYED to gamesPlayed,
                FIELD_LAST_PLAYED_TITLE to snapshot.getString(FIELD_LAST_PLAYED_TITLE).orEmpty(),
                FIELD_LAST_PLAYED_AT_MS to (snapshot.getLong(FIELD_LAST_PLAYED_AT_MS) ?: 0L),
                FIELD_PROFILE_ACCENT to profileAccent,
                FIELD_FAVORITE_GAME_KEYS to favoriteGameKeys,
                FIELD_UPDATED_AT to FieldValue.serverTimestamp()
            )
            transaction.set(userRef, userData, SetOptions.merge())
            transaction.set(publicProfileRef, publicProfileData, SetOptions.merge())
            transaction.set(leaderboardRef, leaderboardData, SetOptions.merge())
            existingGames.entries
                .sortedByDescending { (_, value) -> value.toGameMap().longValue(GAME_TOTAL_MS) }
                .take(PUBLIC_MIGRATION_GAME_LIMIT)
                .forEach { (gameKey, gameData) ->
                    transaction.set(
                        publicProfileRef.collection(PUBLIC_GAMES_COLLECTION).document(gameKey),
                        gameData.toGameMap().toPublicGameData(),
                        SetOptions.merge()
                    )
                }
        }.await()
    }

    private fun DocumentSnapshot.toPlayerProfile(): PlayerProfile? {
        if (!exists()) return null
        val uid = getString(FIELD_UID) ?: id
        val games = getGamesMap()
            .mapNotNull { (key, value) -> value.toPlayerGamePlayStat(key) }
            .sortedByDescending { it.totalPlayTimeMs }
        return PlayerProfile(
            uid = uid,
            email = getString(FIELD_EMAIL),
            displayName = getString(FIELD_DISPLAY_NAME).cleanDisplayName(getString(FIELD_EMAIL)),
            photoURL = getString(FIELD_PHOTO_URL),
            totalPlayTimeMs = getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: games.sumOf { it.totalPlayTimeMs },
            gamesPlayed = (getLong(FIELD_GAMES_PLAYED) ?: games.size.toLong()).toInt(),
            lastPlayedTitle = getString(FIELD_LAST_PLAYED_TITLE),
            lastPlayedAtMs = getLong(FIELD_LAST_PLAYED_AT_MS) ?: getTimestamp(FIELD_LAST_PLAYED_AT)?.toMillisCompat(),
            games = games,
            playerTag = getString(FIELD_PLAYER_TAG) ?: buildPlayerTag(uid),
            profileAccent = getString(FIELD_PROFILE_ACCENT) ?: DEFAULT_PROFILE_ACCENT,
            favoriteGameKeys = stringList(FIELD_FAVORITE_GAME_KEYS),
            isProMember = getBoolean(FIELD_PRO_MEMBER) == true,
            publicDevice = publicDevice(),
            achievementCount = (getLong(FIELD_ACHIEVEMENT_COUNT) ?: 0L).toInt(),
            achievementPoints = (getLong(FIELD_ACHIEVEMENT_POINTS) ?: 0L).toInt()
        )
    }

    private fun DocumentSnapshot.toPublicPlayerProfile(games: List<PlayerGamePlayStat>): PlayerProfile? {
        if (!exists()) return null
        val uid = getString(FIELD_UID) ?: id
        return PlayerProfile(
            uid = uid,
            email = null,
            displayName = getString(FIELD_DISPLAY_NAME).cleanDisplayName(null),
            photoURL = getString(FIELD_PHOTO_URL),
            totalPlayTimeMs = getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: games.sumOf { it.totalPlayTimeMs },
            gamesPlayed = (getLong(FIELD_GAMES_PLAYED) ?: games.size.toLong()).toInt(),
            lastPlayedTitle = getString(FIELD_LAST_PLAYED_TITLE),
            lastPlayedAtMs = getLong(FIELD_LAST_PLAYED_AT_MS),
            games = games,
            playerTag = getString(FIELD_PLAYER_TAG) ?: buildPlayerTag(uid),
            profileAccent = getString(FIELD_PROFILE_ACCENT) ?: DEFAULT_PROFILE_ACCENT,
            favoriteGameKeys = stringList(FIELD_FAVORITE_GAME_KEYS),
            isProMember = getBoolean(FIELD_PRO_MEMBER) == true,
            publicDevice = publicDevice(),
            achievementCount = (getLong(FIELD_ACHIEVEMENT_COUNT) ?: 0L).toInt(),
            achievementPoints = (getLong(FIELD_ACHIEVEMENT_POINTS) ?: 0L).toInt()
        )
    }

    private fun DocumentSnapshot.publicDevice(): PublicPlayerDevice? {
        if (getBoolean("deviceVisibility") != true) return null
        val device = get("primaryDevice") as? Map<*, *> ?: return null
        return PublicPlayerDevice(
            displayName = device["displayName"] as? String ?: return null,
            soc = device["soc"] as? String ?: "",
            gpuFamily = device["gpuFamily"] as? String ?: "",
            ramMb = (device["ramMb"] as? Number)?.toLong() ?: 0L,
            androidVersion = device["androidVersion"] as? String ?: "",
            appVersion = device["appVersion"] as? String ?: "",
            coreVersion = device["coreVersion"] as? String ?: ""
        )
    }

    private fun DocumentSnapshot.toPlayerLeaderboardEntry(rank: Int?): PlayerLeaderboardEntry? {
        if (!exists()) return null
        val uid = getString(FIELD_UID) ?: id
        return PlayerLeaderboardEntry(
            rank = rank,
            uid = uid,
            displayName = getString(FIELD_DISPLAY_NAME).cleanDisplayName(null),
            photoURL = getString(FIELD_PHOTO_URL),
            totalPlayTimeMs = getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: 0L,
            gamesPlayed = (getLong(FIELD_GAMES_PLAYED) ?: 0L).toInt(),
            lastPlayedTitle = getString(FIELD_LAST_PLAYED_TITLE),
            playerTag = getString(FIELD_PLAYER_TAG) ?: buildPlayerTag(uid),
            profileAccent = getString(FIELD_PROFILE_ACCENT) ?: DEFAULT_PROFILE_ACCENT,
            isProMember = getBoolean(FIELD_PRO_MEMBER) == true
        )
    }

    private fun Any?.toPlayerGamePlayStat(gameKey: String): PlayerGamePlayStat? {
        val map = toGameMap()
        val title = map.stringValue(GAME_TITLE).takeIf { it.isNotBlank() } ?: return null
        val serial = map.stringValue(GAME_SERIAL).takeIf { it.isNotBlank() }
        return PlayerGamePlayStat(
            gameKey = gameKey,
            title = title,
            serial = serial,
            coverArtPath = resolveReadableCoverPath(map.stringValue(GAME_COVER_ART_PATH), serial, title),
            totalPlayTimeMs = map.longValue(GAME_TOTAL_MS),
            sessions = map.longValue(GAME_SESSIONS).toInt(),
            lastPlayedAtMs = map.longValue(GAME_LAST_PLAYED_AT_MS).takeIf { it > 0L }
        )
    }

    private fun DocumentSnapshot.getGamesMap(): Map<String, Any?> {
        @Suppress("UNCHECKED_CAST")
        return (get(FIELD_GAMES) as? Map<String, Any?>).orEmpty()
    }

    private fun DocumentSnapshot.stringList(field: String): List<String> {
        return (get(field) as? List<*>)
            .orEmpty()
            .filterIsInstance<String>()
    }

    private fun Any?.toGameMap(): Map<String, Any?> {
        @Suppress("UNCHECKED_CAST")
        return this as? Map<String, Any?> ?: emptyMap()
    }

    private fun Map<String, Any?>.stringValue(key: String): String {
        return this[key] as? String ?: ""
    }

    private fun Map<String, Any?>.longValue(key: String): Long {
        return when (val value = this[key]) {
            is Long -> value
            is Int -> value.toLong()
            is Double -> value.toLong()
            is Number -> value.toLong()
            else -> 0L
        }
    }

    private fun Map<String, Any?>.toPublicGameData(): Map<String, Any> {
        val source = this
        return buildMap {
            put(GAME_TITLE, source.stringValue(GAME_TITLE).ifBlank { DEFAULT_GAME_TITLE }.take(MAX_GAME_TITLE_LENGTH))
            put(GAME_TOTAL_MS, source.longValue(GAME_TOTAL_MS).coerceAtLeast(0L))
            put(GAME_SESSIONS, source.longValue(GAME_SESSIONS).coerceAtLeast(0L))
            put(GAME_LAST_PLAYED_AT_MS, source.longValue(GAME_LAST_PLAYED_AT_MS).coerceAtLeast(0L))
            source.stringValue(GAME_SERIAL).takeIf { it.isNotBlank() }?.let {
                put(GAME_SERIAL, it.take(MAX_SERIAL_LENGTH))
            }
            source.stringValue(GAME_COVER_ART_PATH)
                .takeIf { it.startsWith("https://") || it.startsWith("http://") }
                ?.let { put(GAME_COVER_ART_PATH, it.take(MAX_COVER_PATH_LENGTH)) }
        }
    }

    private fun com.google.firebase.auth.FirebaseUser.toPlayerAccount(): PlayerAccount {
        return PlayerAccount(
            uid = uid,
            email = email,
            displayName = displayName.cleanDisplayName(email),
            photoURL = bestPhotoUrl()
        )
    }

    private fun com.google.firebase.auth.FirebaseUser.bestPhotoUrl(): String? {
        return photoUrl?.toString()
            ?: providerData.firstNotNullOfOrNull { it.photoUrl?.toString() }
    }

    private fun String?.cleanDisplayName(email: String?): String {
        val fallback = email?.substringBefore('@')?.takeIf { it.isNotBlank() } ?: DEFAULT_DISPLAY_NAME
        return this?.trim()?.takeIf { it.isNotBlank() }?.take(MAX_DISPLAY_NAME_LENGTH) ?: fallback
    }

    private fun buildGameKey(titleId: String): String {
        val key = titleId
            .trim()
            .uppercase()
            .replace(Regex("[^A-Z0-9_-]"), "_")
            .takeIf { it.isNotBlank() }
        return key ?: "T_${titleId.sha256().take(32)}"
    }

    private fun buildPlayerTag(uid: String): String {
        return PLAYER_TAG_PREFIX + uid.sha256().take(7).uppercase(Locale.ROOT)
    }

    private fun normalizeSearchName(value: String): String {
        return value.trim()
            .lowercase(Locale.ROOT)
            .replace(Regex("\\s+"), " ")
            .take(MAX_DISPLAY_NAME_LENGTH)
    }

    private fun activityDayKey(timestampMs: Long): String {
        return SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date(timestampMs))
    }

    private fun buildShareableCoverPath(coverArtPath: String?, titleId: String?, title: String?): String? {
        val cover = coverArtPath?.trim().orEmpty()
        return when {
            cover.startsWith("http://") || cover.startsWith("https://") -> cover
            else -> catalogCoverUrl(titleId, title)
        }
    }

    private fun resolveReadableCoverPath(coverArtPath: String, titleId: String?, title: String?): String? {
        val cover = coverArtPath.trim()
        return when {
            cover.startsWith("http://") || cover.startsWith("https://") -> cover
            cover.startsWith("content://") -> cover
            File(cover).exists() -> cover
            else -> catalogCoverUrl(titleId, title)
        }
    }

    private fun catalogCoverUrl(titleId: String?, title: String?): String? {
        val key = "${titleId.orEmpty()}|${title.orEmpty()}"
        coverUrlCache[key]?.let { return it.ifEmpty { null } }
        val resolved = catalogRepository.findBySerial(titleId.orEmpty())?.coverUrl
            ?: title?.takeIf { it.isNotBlank() }?.let { name -> catalogRepository.findBestMatch(name)?.coverUrl }
        coverUrlCache[key] = resolved.orEmpty()
        return resolved
    }

    private fun String.sha256(): String {
        val digest = MessageDigest.getInstance("SHA-256")
        return digest.digest(toByteArray()).joinToString("") { "%02x".format(it) }
    }

    private fun Timestamp.toMillisCompat(): Long = seconds * 1000L + nanoseconds / 1_000_000L

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { result ->
            continuation.resume(result)
        }
        addOnFailureListener { error ->
            continuation.resumeWithException(error)
        }
        addOnCanceledListener {
            continuation.cancel()
        }
    }

    companion object {
        private const val TAG = "PlayerProfileRepository"
        private const val USERS_COLLECTION = "users"
        private const val PUBLIC_PROFILES_COLLECTION = "publicProfiles"
        private const val PUBLIC_GAMES_COLLECTION = "games"
        private const val ACTIVITY_COLLECTION = "activity"
        private const val LEADERBOARD_COLLECTION = "leaderboardPlayTime"
        private const val LEADERBOARD_PAGE_SIZE = 50L
        private const val LEADERBOARD_MAX_ENTRIES = 1000
        private const val PLAYER_SEARCH_LIMIT = 20L
        private const val MIN_PLAYER_SEARCH_LENGTH = 2
        private const val PUBLIC_GAMES_PAGE_SIZE = 20L
        private const val PUBLIC_MIGRATION_GAME_LIMIT = 100
        private const val ACTIVITY_HISTORY_DAYS = 120L
        private const val MAX_FAVORITE_GAMES = 3
        private const val MAX_DAILY_GAMES = 100
        private const val PLAYER_TAG_PREFIX = "EV-"
        private const val PROFILE_SCHEMA_VERSION = 2
        private const val MAX_DISPLAY_NAME_LENGTH = 32
        private const val MAX_GAME_TITLE_LENGTH = 120
        private const val MAX_SERIAL_LENGTH = 32
        private const val MAX_COVER_PATH_LENGTH = 500
        private const val DEFAULT_DISPLAY_NAME = "Player"
        private const val DEFAULT_GAME_TITLE = "Unknown game"

        private const val FIELD_UID = "uid"
        private const val FIELD_PROFILE_SCHEMA_VERSION = "profileSchemaVersion"
        private const val FIELD_EMAIL = "email"
        private const val FIELD_DISPLAY_NAME = "displayName"
        private const val FIELD_SEARCH_NAME = "searchName"
        private const val FIELD_PLAYER_TAG = "playerTag"
        private const val FIELD_PHOTO_URL = "photoURL"
        private const val FIELD_TOTAL_PLAY_TIME_MS = "totalPlayTimeMs"
        private const val FIELD_GAMES_PLAYED = "gamesPlayed"
        private const val FIELD_LAST_PLAYED_TITLE = "lastPlayedTitle"
        private const val FIELD_LAST_PLAYED_AT = "lastPlayedAt"
        private const val FIELD_LAST_PLAYED_AT_MS = "lastPlayedAtMs"
        private const val FIELD_UPDATED_AT = "updatedAt"
        private const val FIELD_CREATED_AT = "createdAt"
        private const val FIELD_GAMES = "games"
        private const val FIELD_PROFILE_ACCENT = "profileAccent"
        private const val FIELD_FAVORITE_GAME_KEYS = "favoriteGameKeys"
        private const val FIELD_PRO_MEMBER = "proMember"
        private const val FIELD_ACHIEVEMENT_COUNT = "achievementCount"
        private const val FIELD_ACHIEVEMENT_POINTS = "achievementPoints"

        private const val ACTIVITY_PLAY_TIME_MS = "playTimeMs"
        private const val ACTIVITY_SESSIONS = "sessions"
        private const val ACTIVITY_GAME_KEYS = "gameKeys"

        private val PROFILE_ACCENTS = setOf("gold", "crimson", "blue", "violet", "emerald")

        private const val GAME_TITLE = "t"
        private const val GAME_TOTAL_MS = "ms"
        private const val GAME_SESSIONS = "n"
        private const val GAME_LAST_PLAYED_AT_MS = "lp"
        private const val GAME_SERIAL = "s"
        private const val GAME_COVER_ART_PATH = "c"
    }
}
