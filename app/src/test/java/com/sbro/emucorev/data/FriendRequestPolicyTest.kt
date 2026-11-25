package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Test

class FriendRequestPolicyTest {
    private val me = "me"

    @Test fun noRelationshipCreatesARequest() {
        assertEquals(FriendRequestAction.CreateRequest, FriendRequestPolicy.decide(me, null, iBlockedOther = false))
    }

    @Test fun acceptedFriendshipIsNeverReset() {
        val accepted = ExistingFriendship("accepted", "other")
        assertEquals(FriendRequestAction.NoChange, FriendRequestPolicy.decide(me, accepted, false))
        assertEquals(FriendRequestAction.NoChange, FriendRequestPolicy.decide(me, ExistingFriendship("accepted", me), false))
    }

    @Test fun myOwnPendingRequestIsLeftAlone() {
        assertEquals(FriendRequestAction.NoChange, FriendRequestPolicy.decide(me, ExistingFriendship("pending", me), false))
    }

    @Test fun theOtherPlayersPendingRequestIsAcceptedNotOverwritten() {
        assertEquals(FriendRequestAction.AcceptIncoming, FriendRequestPolicy.decide(me, ExistingFriendship("pending", "other"), false))
    }

    @Test fun blockedPlayersCannotBeRequested() {
        assertEquals(FriendRequestAction.Blocked, FriendRequestPolicy.decide(me, null, iBlockedOther = true))
        assertEquals(FriendRequestAction.Blocked, FriendRequestPolicy.decide(me, ExistingFriendship("pending", "other"), true))
    }

    @Test fun unknownStatusIsReplacedByAFreshRequest() {
        assertEquals(FriendRequestAction.CreateRequest, FriendRequestPolicy.decide(me, ExistingFriendship("weird", "other"), false))
    }
}
