package com.sbro.emucorev.data

import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.Query
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class FriendshipStatus { PendingIncoming, PendingOutgoing, Accepted }

data class ProfileFriendship(
    val id: String,
    val otherUid: String,
    val status: FriendshipStatus,
    val updatedAtMs: Long?
)

data class ProfileFeedEvent(
    val id: String,
    val uid: String,
    val type: String,
    val achievementId: String?,
    val gameTitle: String?,
    val createdAtMs: Long?
)

class ProfileSocialRepository {
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()

    fun observeFriendships(): Flow<List<ProfileFriendship>> = callbackFlow {
        val uid = auth.currentUser?.uid
        if (uid == null) {
            trySend(emptyList())
            close()
            return@callbackFlow
        }
        val registration = firestore.collection(FRIENDSHIPS)
            .whereArrayContains("members", uid)
            .orderBy("updatedAt", Query.Direction.DESCENDING)
            .limit(MAX_FRIENDSHIPS.toLong())
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    close(error)
                    return@addSnapshotListener
                }
                trySend(snapshot?.documents.orEmpty().mapNotNull { it.toFriendship(uid) })
            }
        awaitClose { registration.remove() }
    }

    fun observeFeed(profileUid: String): Flow<List<ProfileFeedEvent>> = callbackFlow {
        if (auth.currentUser == null) {
            trySend(emptyList())
            close()
            return@callbackFlow
        }
        val registration = firestore.collection(PUBLIC_PROFILES).document(profileUid).collection(FEED)
            .whereEqualTo("visibility", "public")
            .orderBy("createdAt", Query.Direction.DESCENDING)
            .limit(MAX_FEED.toLong())
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    close(error)
                    return@addSnapshotListener
                }
                trySend(snapshot?.documents.orEmpty().map { it.toFeedEvent() })
            }
        awaitClose { registration.remove() }
    }

    fun observeBlockedUids(): Flow<List<String>> = callbackFlow {
        val uid = auth.currentUser?.uid
        if (uid == null) {
            trySend(emptyList())
            close()
            return@callbackFlow
        }
        val registration = firestore.collection(USERS).document(uid).collection(BLOCKS)
            .orderBy("createdAt", Query.Direction.DESCENDING)
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    close(error)
                    return@addSnapshotListener
                }
                trySend(snapshot?.documents.orEmpty().mapNotNull { it.getString("blockedUid") })
            }
        awaitClose { registration.remove() }
    }

    suspend fun sendFriendRequest(otherUid: String) {
        val uid = auth.currentUser?.uid ?: error("Sign in is required")
        require(otherUid.isNotBlank() && otherUid != uid) { "Invalid player" }
        val members = listOf(uid, otherUid).sorted()
        val id = friendshipId(uid, otherUid)
        firestore.collection(FRIENDSHIPS).document(id).set(mapOf(
            "id" to id,
            "members" to members,
            "requestedBy" to uid,
            "status" to "pending",
            "createdAt" to FieldValue.serverTimestamp(),
            "updatedAt" to FieldValue.serverTimestamp()
        )).await()
    }

    suspend fun acceptFriendRequest(friendshipId: String) {
        firestore.collection(FRIENDSHIPS).document(friendshipId).update(mapOf(
            "status" to "accepted",
            "updatedAt" to FieldValue.serverTimestamp()
        )).await()
    }

    suspend fun removeFriendship(friendshipId: String) {
        firestore.collection(FRIENDSHIPS).document(friendshipId).delete().await()
    }

    suspend fun block(otherUid: String) {
        val uid = auth.currentUser?.uid ?: error("Sign in is required")
        require(otherUid.isNotBlank() && otherUid != uid) { "Invalid player" }
        firestore.collection(USERS).document(uid).collection(BLOCKS).document(otherUid).set(
            mapOf("uid" to uid, "blockedUid" to otherUid, "createdAt" to FieldValue.serverTimestamp())
        ).await()
        // A relationship may not exist; blocking must still succeed in that case.
        runCatching { firestore.collection(FRIENDSHIPS).document(friendshipId(uid, otherUid)).delete().await() }
    }

    suspend fun unblock(otherUid: String) {
        val uid = auth.currentUser?.uid ?: return
        firestore.collection(USERS).document(uid).collection(BLOCKS).document(otherUid).delete().await()
    }

    suspend fun isBlocked(otherUid: String): Boolean {
        val uid = auth.currentUser?.uid ?: return false
        return firestore.collection(USERS).document(uid).collection(BLOCKS).document(otherUid).get().await().exists()
    }

    private fun DocumentSnapshot.toFriendship(uid: String): ProfileFriendship? {
        val members = (get("members") as? List<*>)?.filterIsInstance<String>().orEmpty()
        val otherUid = members.firstOrNull { it != uid } ?: return null
        val requestedBy = getString("requestedBy") ?: return null
        val status = when (getString("status")) {
            "accepted" -> FriendshipStatus.Accepted
            "pending" -> if (requestedBy == uid) FriendshipStatus.PendingOutgoing else FriendshipStatus.PendingIncoming
            else -> return null
        }
        return ProfileFriendship(id, otherUid, status, getTimestamp("updatedAt")?.toDate()?.time)
    }

    private fun DocumentSnapshot.toFeedEvent() = ProfileFeedEvent(
        id = id,
        uid = getString("uid").orEmpty(),
        type = getString("type").orEmpty(),
        achievementId = getString("achievementId"),
        gameTitle = getString("gameTitle"),
        createdAtMs = getTimestamp("createdAt")?.toDate()?.time
    )

    private fun friendshipId(firstUid: String, secondUid: String) = listOf(firstUid, secondUid).sorted().joinToString("_")

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { continuation.resume(it) }
        addOnFailureListener { continuation.resumeWithException(it) }
        addOnCanceledListener { continuation.cancel() }
    }

    private companion object {
        const val USERS = "users"
        const val PUBLIC_PROFILES = "publicProfiles"
        const val FRIENDSHIPS = "friendships"
        const val BLOCKS = "blocks"
        const val FEED = "feed"
        const val MAX_FRIENDSHIPS = 100
        const val MAX_FEED = 30
    }
}
