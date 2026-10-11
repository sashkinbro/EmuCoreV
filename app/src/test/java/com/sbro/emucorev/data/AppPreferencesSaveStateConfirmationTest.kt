package com.sbro.emucorev.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppPreferencesSaveStateConfirmationTest {
    private lateinit var context: Context

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit()
            .remove(KEY_CONFIRM_SAVE_STATE_ACTIONS)
            .commit()
    }

    @Test
    fun confirmationsDefaultOnAndPersistAcrossPreferenceInstances() {
        val preferences = AppPreferences(context)
        assertTrue(preferences.confirmSaveStateActions)

        preferences.confirmSaveStateActions = false

        assertEquals(false, AppPreferences(context).confirmSaveStateActions)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun confirmationFlowEmitsTheStoredDefaultAndLaterChanges() = runBlocking {
        val preferences = AppPreferences(context)
        val values = mutableListOf<Boolean>()
        val collector = launch(Dispatchers.Unconfined) {
            preferences.confirmSaveStateActionsFlow.take(2).toList(values)
        }

        preferences.confirmSaveStateActions = false
        withTimeout(1_000) { collector.join() }

        assertEquals(listOf(true, false), values)
    }

    private companion object {
        const val PREFERENCES = "emucorev_prefs"
        const val KEY_CONFIRM_SAVE_STATE_ACTIONS = "confirm_save_state_actions"
    }
}
