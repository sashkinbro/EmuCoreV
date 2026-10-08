package com.sbro.emucorev.ui.emulation

import android.app.Application
import android.content.Context
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class TouchControlLayoutRepositoryTest {
    private lateinit var context: Application
    private lateinit var repository: TouchControlLayoutRepository

    @Before fun setUp() {
        context = RuntimeEnvironment.getApplication()
        repository = TouchControlLayoutRepository(context)
        repository.reset()
    }

    @Test fun legacyLayoutKeepsPositionsAndUsesItsOriginalAction() {
        context.getSharedPreferences("touch_control_layout", Context.MODE_PRIVATE).edit()
            .putString("layout_v1", """[{"id":"cross","x":0.23,"y":0.64,"width":0.08,"height":0.12,"visible":false}]""").commit()
        val loaded = repository.load()!!.single()
        assertEquals("cross", loaded.actionId)
        assertEquals(0.23f, loaded.x, 0.0001f)
        assertFalse(loaded.visible)
        assertNull(loaded.secondaryActionId)
    }

    @Test fun duplicateComboSurvivesSaveAndMergeAlongsideCustomizedStandardButton() {
        val defaults = listOf(TouchControlElement("cross", 0.8f, 0.7f, 0.08f, 0.1f))
        val standard = defaults.single().copy(x = 0.31f)
        val custom = standard.copy(id = "custom_combo", actionId = "square", secondaryActionId = "l1", x = 0.6f)
        repository.save(listOf(standard, custom))
        assertEquals(listOf(standard, custom), mergeTouchLayout(defaults, repository.load()))
    }

    @Test fun malformedEntryDoesNotDiscardOtherCustomizedButtons() {
        context.getSharedPreferences("touch_control_layout", Context.MODE_PRIVATE).edit()
            .putString("layout_v1", """[{"id":"cross","x":0.23,"y":0.64,"width":0.08,"height":0.12}, {"id":"broken"}]""").commit()
        assertEquals(listOf("cross"), repository.load()?.map { it.id })
    }

    @Test fun invalidCustomActionsCannotReplaceAnalogControlsOrCreateUnboundedDuplicates() {
        val defaults = listOf(TouchControlElement("left_stick", 0.1f, 0.6f, 0.1f, 0.15f))
        val invalid = defaults.single().copy(actionId = "cross")
        val custom = (0..40).map { TouchControlElement("custom_$it", -1f, 2f, 0.1f, 0.1f, actionId = "cross") }
        val merged = mergeTouchLayout(defaults, listOf(invalid) + custom + custom.first())
        assertEquals(defaults.single(), merged.first())
        assertEquals(33, merged.size)
        assertTrue(merged.drop(1).all { it.x == 0f && it.y == 0.9f })
    }
}
