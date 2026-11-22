package com.sbro.emucorev.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InputGateWatchdogTest {
    private var now = 0L
    private val watchdog = InputGateWatchdog(15_000L) { now }

    @Test fun gateExpiresOnceAfterTheTimeout() {
        assertFalse(watchdog.check(true))
        now += 14_999
        assertFalse(watchdog.check(true))
        now += 1
        assertTrue(watchdog.check(true))
        now += 60_000
        assertFalse("expiry is reported once per blocking period", watchdog.check(true))
    }

    @Test fun releasingTheGateRestartsTheClock() {
        assertFalse(watchdog.check(true))
        now += 10_000
        assertFalse(watchdog.check(false))
        now += 10_000
        assertFalse(watchdog.check(true))
        now += 14_999
        assertFalse(watchdog.check(true))
        now += 1
        assertTrue(watchdog.check(true))
    }

    @Test fun anotherBlockingPeriodCanExpireAgain() {
        watchdog.check(true)
        now += 15_000
        assertTrue(watchdog.check(true))
        watchdog.check(false)
        watchdog.check(true)
        now += 15_000
        assertTrue(watchdog.check(true))
    }
}
