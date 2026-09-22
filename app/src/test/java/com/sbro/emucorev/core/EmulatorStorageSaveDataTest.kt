package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class EmulatorStorageSaveDataTest {
    @Test
    fun configuredUserWinsWhileItExists() {
        assertEquals("01", EmulatorStorage.resolveActiveUserId("01", listOf("00", "01")))
    }

    @Test
    fun staleConfiguredUserFallsBackToExistingUser() {
        assertEquals("00", EmulatorStorage.resolveActiveUserId("02", listOf("00", "01")))
    }

    @Test
    fun missingConfigurationUsesFirstUserLikeTheNativeCore() {
        assertEquals("00", EmulatorStorage.resolveActiveUserId(null, listOf("01", "00")))
    }

    @Test
    fun noUsersDefaultsToCanonicalUserId() {
        assertEquals(EmulatorStorage.DEFAULT_USER_ID, EmulatorStorage.resolveActiveUserId(null, emptyList()))
    }

    @Test
    fun parsesQuotedUserIdFromConfig() {
        assertEquals(
            "01",
            EmulatorStorage.parseConfiguredUserId(listOf("# comment", "  user-id: \"01\"  "))
        )
    }

    @Test
    fun rejectsUnsafeOrEmptyUserIdFromConfig() {
        assertNull(EmulatorStorage.parseConfiguredUserId(listOf("user-id: \"\"")))
        assertNull(EmulatorStorage.parseConfiguredUserId(listOf("user-id: ../01")))
        assertNull(EmulatorStorage.parseConfiguredUserId(listOf("other-key: value")))
    }
}
