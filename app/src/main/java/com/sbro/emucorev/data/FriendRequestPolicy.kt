package com.sbro.emucorev.data

/** What a friend request to another player should do, given the relationship that already exists (B068). */
enum class FriendRequestAction {
    /** No relationship yet: create the pending request. */
    CreateRequest,

    /** The other player already asked us: accepting is the expected outcome. */
    AcceptIncoming,

    /** Already friends, or already asked: nothing to write. */
    NoChange,

    /** The requester has blocked the other player. */
    Blocked
}

/** Existing friendship document reduced to what the decision needs. */
data class ExistingFriendship(val status: String?, val requestedBy: String?)

object FriendRequestPolicy {
    fun decide(uid: String, existing: ExistingFriendship?, iBlockedOther: Boolean): FriendRequestAction {
        if (iBlockedOther) return FriendRequestAction.Blocked
        if (existing == null) return FriendRequestAction.CreateRequest
        return when (existing.status) {
            "accepted" -> FriendRequestAction.NoChange
            "pending" -> if (existing.requestedBy == uid) FriendRequestAction.NoChange else FriendRequestAction.AcceptIncoming
            else -> FriendRequestAction.CreateRequest
        }
    }
}

/** Thrown when a friend request is refused because the requester blocked the other player. */
class FriendRequestBlockedException : IllegalStateException("Player is blocked")
