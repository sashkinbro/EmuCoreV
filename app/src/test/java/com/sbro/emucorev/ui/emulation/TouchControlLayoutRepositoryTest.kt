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

    @Test fun legacyLayoutRemainsTheGlobalFallbackAndGameOverridesStayIsolated() {
        val legacy = listOf(TouchControlElement("cross", 0.21f, 0.32f, 0.08f, 0.1f))
        repository.saveGlobal(legacy)

        val gameA = listOf(legacy.single().copy(x = 0.71f))
        repository.saveForGame("PCSE00333", gameA)

        assertEquals(legacy, repository.loadGlobal())
        assertEquals(gameA, repository.loadEffective("PCSE00333"))
        assertEquals(legacy, repository.loadEffective("PCSE00383"))
        assertTrue(repository.hasGameOverride("PCSE00333"))
        assertFalse(repository.hasGameOverride("PCSE00383"))
    }

    @Test fun clearingGameOverrideRestoresGlobalInheritanceWithoutDeletingGlobalLayout() {
        val global = listOf(TouchControlElement("cross", 0.25f, 0.35f, 0.08f, 0.1f))
        repository.saveGlobal(global)
        repository.saveForGame("PCSE00333", listOf(global.single().copy(x = 0.75f)))

        repository.clearForGame("PCSE00333")

        assertFalse(repository.hasGameOverride("PCSE00333"))
        assertEquals(global, repository.loadEffective("PCSE00333"))
        assertEquals(global, repository.loadGlobal())
    }

    @Test fun titleIdMatchingIsCaseInsensitiveAndGameplayRepositoryUsesTitleOverride() {
        val global = listOf(TouchControlElement("cross", 0.2f, 0.3f, 0.08f, 0.1f))
        val game = listOf(global.single().copy(x = 0.8f))
        repository.saveGlobal(global)
        repository.saveForGame("pcse00333", game)
        val gameplayRepository = TouchControlLayoutRepository(context) { " PCSE00333 " }

        assertEquals(game, gameplayRepository.load())
        gameplayRepository.save(listOf(game.single().copy(y = 0.65f)))

        assertEquals(0.65f, repository.loadForGame("PCSE00333")!!.single().y, 0.0001f)
        assertEquals(global, repository.loadGlobal())
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

    @Test fun sharedEditorUsesSameSafeAreaPaddingRulesAsGameplay() {
        val portrait = touchLayoutSafeAreaPadding(
            isLandscape = false,
            density = 2f,
            leftInsetPx = 20f,
            rightInsetPx = 30f,
            topInsetPx = 40f,
            bottomInsetPx = 50f
        )
        assertEquals(50f, portrait.sidePx, 0.001f)
        assertEquals(82f, portrait.bottomPx, 0.001f)
        assertEquals(72f, portrait.shoulderTopPx, 0.001f)

        val landscape = touchLayoutSafeAreaPadding(
            isLandscape = true,
            density = 2f,
            leftInsetPx = 20f,
            rightInsetPx = 30f,
            topInsetPx = 40f,
            bottomInsetPx = 50f
        )
        assertEquals(62f, landscape.sidePx, 0.001f)
        assertEquals(94f, landscape.bottomPx, 0.001f)
        assertEquals(84f, landscape.shoulderTopPx, 0.001f)
    }
}
