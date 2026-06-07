package com.sbro.emucorev.data

import com.google.android.gms.tasks.Task
import com.google.firebase.Timestamp
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

class FirebaseProfileBackupRepository(
    private val auth: FirebaseAuth = FirebaseAuth.getInstance(),
    private val firestore: FirebaseFirestore = FirebaseFirestore.getInstance()
) {
    private val backups = firestore.collection("profile_backups")

    suspend fun backup(entries: List<ProfileGameListEntry>) {
        val user = auth.currentUser ?: error("Sign in before backing up your profile.")
        val payload = mapOf(
            "uid" to user.uid,
            "version" to 1,
            "items" to entries.map { entry ->
                mapOf(
                    "igdbId" to entry.igdbId,
                    "status" to (entry.status?.name ?: ""),
                    "favorite" to entry.isFavorite,
                    "addedAt" to entry.addedAtEpochMillis,
                    "updatedAt" to entry.updatedAtEpochMillis
                )
            },
            "updatedAt" to FieldValue.serverTimestamp()
        )
        backups.document(user.uid).set(payload).await()
    }

    suspend fun restore(): List<ProfileGameListEntry> {
        val user = auth.currentUser ?: error("Sign in before restoring your profile.")
        val document = backups.document(user.uid).get().await()
        val items = document.get("items") as? List<*> ?: return emptyList()
        return items.mapNotNull { raw ->
            val item = raw as? Map<*, *> ?: return@mapNotNull null
            val igdbId = item["igdbId"].toLongOrNull()?.takeIf { it > 0L } ?: return@mapNotNull null
            val statusName = item["status"] as? String
            val status = ProfileGameStatus.entries.firstOrNull { it.name == statusName }
            val isFavorite = item["favorite"] as? Boolean ?: false
            if (status == null && !isFavorite) return@mapNotNull null
            ProfileGameListEntry(
                igdbId = igdbId,
                status = status,
                isFavorite = isFavorite,
                addedAtEpochMillis = item["addedAt"].toLongOrNull() ?: 0L,
                updatedAtEpochMillis = item["updatedAt"].toLongOrNull()
                    ?: (document.getTimestamp("updatedAt") ?: Timestamp.now()).toDate().time
            )
        }
    }

    private fun Any?.toLongOrNull(): Long? {
        return when (this) {
            is Long -> this
            is Int -> toLong()
            is Double -> toLong()
            is String -> toLongOrNull()
            else -> null
        }
    }
}

suspend fun <T> Task<T>.await(): T {
    return suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { result ->
            continuation.resume(result)
        }
        addOnFailureListener { exception ->
            continuation.resumeWithException(exception)
        }
        addOnCanceledListener {
            continuation.cancel()
        }
    }
}
