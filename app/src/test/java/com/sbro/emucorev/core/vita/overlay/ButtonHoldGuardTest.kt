package com.sbro.emucorev.core.vita.overlay

import org.junit.Assert.assertEquals
import org.junit.Test

class ButtonHoldGuardTest {
    private var now = 0L
    private val pending = mutableListOf<Pair<Long, () -> Unit>>()
    private val events = mutableListOf<Pair<Int, Boolean>>()
    private val guard = ButtonHoldGuard(
        minHoldMs = 50L,
        clock = { now },
        schedule = { delay, action -> pending += (now + delay) to action },
        apply = { b, p -> events += b to p }
    )

    private fun advance(ms: Long) {
        now += ms
        pending.filter { it.first <= now }.also { pending.removeAll(it) }.forEach { it.second() }
    }

    @Test
    fun `quick tap is held for the minimum time`() {
        guard.send(1, true)
        advance(5)
        guard.send(1, false)
        assertEquals(listOf(1 to true), events)
        advance(44)
        assertEquals(listOf(1 to true), events)
        advance(1)
        assertEquals(listOf(1 to true, 1 to false), events)
    }

    @Test
    fun `long press releases immediately`() {
        guard.send(2, true)
        advance(200)
        guard.send(2, false)
        assertEquals(listOf(2 to true, 2 to false), events)
    }

    @Test
    fun `repress cancels the deferred release`() {
        guard.send(3, true)
        advance(10)
        guard.send(3, false)
        advance(10)
        guard.send(3, true)
        advance(100)
        assertEquals(listOf(3 to true, 3 to true), events)
        guard.send(3, false)
        assertEquals(listOf(3 to true, 3 to true, 3 to false), events)
    }
}
