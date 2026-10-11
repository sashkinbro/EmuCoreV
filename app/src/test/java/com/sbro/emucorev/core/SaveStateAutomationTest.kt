package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.SaveStateAutomationPreferences
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.launch
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class SaveStateAutomationTest {
    private lateinit var context: Context
    @Before fun reset() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("save_state_automation", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test fun preferencesAreDefaultOffPerGameAndSurviveRecreation() {
        val a = SaveStateAutomationPreferences(context, "GAME_A")
        val b = SaveStateAutomationPreferences(context, "GAME_B")
        assertFalse(a.current.autoSaveOnExit)
        assertFalse(a.current.autoLoadOnStart)
        a.setAutoSaveOnExit(true)
        a.setAutoLoadOnStart(true)
        assertTrue(SaveStateAutomationPreferences(context, "GAME_A").current.autoSaveOnExit)
        assertTrue(SaveStateAutomationPreferences(context, "GAME_A").current.autoLoadOnStart)
        assertFalse(b.current.autoSaveOnExit)
        assertFalse(b.current.autoLoadOnStart)
        a.reset()
        assertFalse(a.current.autoSaveOnExit)
        assertFalse(a.current.autoLoadOnStart)
    }

    @Test fun blankTitleCannotCreateAnAutomationProfile() {
        val prefs = SaveStateAutomationPreferences(context, " ")
        prefs.setAutoSaveOnExit(true)
        prefs.setAutoLoadOnStart(true)
        assertFalse(prefs.current.autoSaveOnExit)
        assertFalse(prefs.current.autoLoadOnStart)
    }

    @Test fun explicitPathHasPriorityAndDoesNotEvenInspectQuickSlot() {
        val args = arrayOf("-r", "A", "-loadstate", "/manual.ecvs")
        assertArrayEquals(args, resolveAutoLoadArguments(args, true) { error("must not inspect") })
        assertTrue(shouldSkipExitAutoSave(args, automaticLoad = false))
        assertFalse(shouldSkipExitAutoSave(args, automaticLoad = true))
    }

    @Test fun missingOrRejectedQuickStateNormalBootsAndValidQuickUsesSlotZero() {
        val args = arrayOf("-r", "A")
        assertArrayEquals(args, resolveAutoLoadArguments(args, false) { error("disabled") })
        assertArrayEquals(args, resolveAutoLoadArguments(args, true) { null })
        assertArrayEquals(arrayOf("-r", "A", "-loadstate", "/quick.ecvs"),
            resolveAutoLoadArguments(args, true) { "/quick.ecvs" })
        assertArrayEquals(emptyArray<String>(), resolveAutoLoadArguments(emptyArray(), true) { error("no game") })
    }

    @Test fun failedAutomaticLoadFallbackKeepsOtherArgumentsAndCannotRetryAutomaticSlot() {
        val args = arrayOf("-r", "A", "-loadstate", "/quick.ecvs", "--other")
        val fallback = autoLoadFallbackArguments(args)
        assertArrayEquals(arrayOf("-r", "A", "--other"), fallback)
        assertArrayEquals(fallback, resolveAutoLoadArguments(fallback, true, skipAutomatic = true) {
            error("automatic failure must not reselect its file")
        })
    }

    @Test fun exitWaitsForSaveAndDeduplicatesWithOriginalOutgoingTitle() = runBlocking {
        val coordinator = SaveStateExitCoordinator()
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val calls = mutableListOf<String>()
        val first = async {
            coordinator.run("GAME_A", true,
                save = { title -> calls += "save:$title"; entered.complete(Unit); release.await() },
                shutdown = { calls += "shutdown:A" })
        }
        entered.await()
        coordinator.run("GAME_B", true, save = { error("duplicate") }, shutdown = { error("duplicate") })
        assertEquals(listOf("save:GAME_A"), calls)
        release.complete(Unit)
        first.await()
        assertEquals(listOf("save:GAME_A", "shutdown:A"), calls)
    }

    @Test fun failedOrSkippedSaveStillShutsDownOnce() = runBlocking {
        var shutdowns = 0
        try {
            SaveStateExitCoordinator().run("A", true, save = { error("save failed") }, shutdown = { shutdowns++ })
            fail("caller must receive the save failure")
        } catch (_: IllegalStateException) { }
        SaveStateExitCoordinator().run("A", false, save = { error("explicit load must skip") }, shutdown = { shutdowns++ })
        assertEquals(2, shutdowns)
    }

    @Test fun disposingCallerCannotRunShutdownBeforeItsPendingSaveCompletes() = runBlocking {
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val calls = mutableListOf<String>()
        val job = launch {
            SaveStateExitCoordinator().run("A", true, save = {
                calls += "saving"
                entered.complete(Unit)
                release.await()
                calls += "saved"
            }, shutdown = { calls += "shutdown" })
        }
        entered.await()
        job.cancel()
        assertEquals(listOf("saving"), calls)
        release.complete(Unit)
        job.join()
        assertEquals(listOf("saving", "saved", "shutdown"), calls)
    }
}
