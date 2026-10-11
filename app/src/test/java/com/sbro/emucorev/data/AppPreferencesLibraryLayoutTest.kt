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
class AppPreferencesLibraryLayoutTest {
    private lateinit var context: Context

    @Before fun clearLayoutOnly() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
            .edit().remove("library_layout_mode").commit()
    }

    @Test fun gridAndListSurvivePreferenceRecreation() {
        assertEquals(LibraryLayoutMode.LIST, AppPreferences(context).libraryLayoutMode)
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.GRID
        assertEquals(LibraryLayoutMode.GRID, AppPreferences(context).libraryLayoutMode)
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.LIST
        assertEquals(LibraryLayoutMode.LIST, AppPreferences(context).libraryLayoutMode)
    }

    @Test fun unknownOrWrongTypedPersistedLayoutFallsBackWithoutThrowing() {
        val stored = context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
        stored.edit().putString("library_layout_mode", "UNKNOWN").commit()
        assertEquals(LibraryLayoutMode.LIST, AppPreferences(context).libraryLayoutMode)
        stored.edit().putInt("library_layout_mode", 1).commit()
        assertEquals(LibraryLayoutMode.LIST, AppPreferences(context).libraryLayoutMode)
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.GRID
        assertEquals(LibraryLayoutMode.GRID, AppPreferences(context).libraryLayoutMode)
    }

    @Test fun flowStartsWithStoredGridAndTracksOtherPreferenceInstances() = runBlocking {
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.GRID
        val recreated = AppPreferences(context)
        val values = mutableListOf<LibraryLayoutMode>()
        val collector = launch(Dispatchers.Unconfined) {
            recreated.libraryLayoutModeFlow.take(3).toList(values)
        }
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.LIST
        AppPreferences(context).libraryLayoutMode = LibraryLayoutMode.GRID
        withTimeout(1_000) { collector.join() }
        assertEquals(listOf(LibraryLayoutMode.GRID, LibraryLayoutMode.LIST, LibraryLayoutMode.GRID), values)
    }
}
