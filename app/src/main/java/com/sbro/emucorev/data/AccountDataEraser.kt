package com.sbro.emucorev.data

/** How a signed-in account can prove it is still its owner before it is deleted (B066). */
enum class ReauthMethod { Password, Google, Unsupported }

/** Picks the re-authentication method from the Firebase provider ids linked to the account. */
fun reauthMethodFor(providerIds: List<String>): ReauthMethod = when {
    "password" in providerIds -> ReauthMethod.Password
    "google.com" in providerIds -> ReauthMethod.Google
    else -> ReauthMethod.Unsupported
}

/** The Firestore access [AccountDataEraser] needs; implemented over Firestore and by test fakes. */
interface AccountDataStore {
    /** Up to [limit] document paths of the collection at [collectionPath]. */
    suspend fun listDocumentPaths(collectionPath: String, limit: Int): List<String>

    /** Paths of every friendship document the account is a member of (up to [limit]). */
    suspend fun listFriendshipPaths(uid: String, limit: Int): List<String>

    /** Deletes the documents; missing documents are not an error. */
    suspend fun deleteDocuments(paths: List<String>)
}

/**
 * Removes every Firestore document EmuCoreV keeps for an account. Subcollections are emptied before
 * their parent documents so nothing is orphaned, and the whole pass is idempotent: it can be repeated
 * after a failure (for example when removing the auth user itself fails afterwards).
 */
class AccountDataEraser(private val store: AccountDataStore) {

    suspend fun erase(uid: String) {
        require(uid.isNotBlank()) { "uid is required" }
        for (sub in USER_SUBCOLLECTIONS) emptyCollection("users/$uid/$sub")
        for (sub in PUBLIC_PROFILE_SUBCOLLECTIONS) emptyCollection("publicProfiles/$uid/$sub")
        while (true) {
            val friendships = store.listFriendshipPaths(uid, BATCH_SIZE)
            if (friendships.isEmpty()) break
            store.deleteDocuments(friendships)
            if (friendships.size < BATCH_SIZE) break
        }
        store.deleteDocuments(
            listOf("leaderboardPlayTime/$uid", "publicProfiles/$uid", "profile_backups/$uid", "users/$uid")
        )
    }

    private suspend fun emptyCollection(path: String) {
        while (true) {
            val documents = store.listDocumentPaths(path, BATCH_SIZE)
            if (documents.isEmpty()) return
            store.deleteDocuments(documents)
            if (documents.size < BATCH_SIZE) return
        }
    }

    companion object {
        const val BATCH_SIZE = 400
        val USER_SUBCOLLECTIONS = listOf("activity", "blocks", "devices", "emulatorProfiles", "trophyBackups")
        val PUBLIC_PROFILE_SUBCOLLECTIONS = listOf("games", "feed")
    }
}

class ReauthCancelledException : Exception("Re-authentication was cancelled")
