package com.sbro.emucorev.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppPreferencesQuickBarTest {
    private val context: Context get() = RuntimeEnvironment.getApplication()

    @Before fun reset() {
        context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
            .edit().remove("show_emulation_quick_bar").commit()
    }

    @Test fun quickBarDefaultsOffAndExplicitChoicePersists() {
        val preferences = AppPreferences(context)
        assertEquals(false, preferences.showEmulationQuickBar)
        preferences.showEmulationQuickBar = true
        assertEquals(true, AppPreferences(context).showEmulationQuickBar)
        preferences.showEmulationQuickBar = false
        assertEquals(false, AppPreferences(context).showEmulationQuickBar)
    }

    @Test fun flowReportsDefaultAndLiveChanges() = runBlocking {
        val preferences = AppPreferences(context)
        val values = mutableListOf<Boolean>()
        val collector = launch(Dispatchers.Unconfined) {
            preferences.showEmulationQuickBarFlow.take(2).toList(values)
        }
        preferences.showEmulationQuickBar = true
        withTimeout(1_000) { collector.join() }
        assertEquals(listOf(false, true), values)
    }
}
