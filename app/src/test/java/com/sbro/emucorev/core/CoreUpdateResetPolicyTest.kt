package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class CoreUpdateResetPolicyTest {

    @Test
    fun `fresh install stores the fingerprint without prompting`() {
        assertEquals(
            CoreUpdateResetAction.STORE_SILENTLY,
            decideCoreUpdateResetAction(
                storedFingerprint = null,
                currentFingerprint = "zip:1a2b:12345",
                hadExistingInstall = false
            )
        )
    }

    @Test
    fun `existing install without a stored fingerprint prompts once`() {
        assertEquals(
            CoreUpdateResetAction.PROMPT,
            decideCoreUpdateResetAction(
                storedFingerprint = null,
                currentFingerprint = "zip:1a2b:12345",
                hadExistingInstall = true
            )
        )
    }

    @Test
    fun `unchanged core does nothing`() {
        assertEquals(
            CoreUpdateResetAction.NONE,
            decideCoreUpdateResetAction(
                storedFingerprint = "zip:1a2b:12345",
                currentFingerprint = "zip:1a2b:12345",
                hadExistingInstall = true
            )
        )
    }

    @Test
    fun `changed core prompts`() {
        assertEquals(
            CoreUpdateResetAction.PROMPT,
            decideCoreUpdateResetAction(
                storedFingerprint = "zip:1a2b:12345",
                currentFingerprint = "zip:9f8e:12345",
                hadExistingInstall = true
            )
        )
    }

    @Test
    fun `missing current fingerprint never prompts`() {
        assertEquals(
            CoreUpdateResetAction.NONE,
            decideCoreUpdateResetAction(
                storedFingerprint = "zip:1a2b:12345",
                currentFingerprint = null,
                hadExistingInstall = true
            )
        )
        assertEquals(
            CoreUpdateResetAction.NONE,
            decideCoreUpdateResetAction(
                storedFingerprint = "zip:1a2b:12345",
                currentFingerprint = "",
                hadExistingInstall = true
            )
        )
    }
}
