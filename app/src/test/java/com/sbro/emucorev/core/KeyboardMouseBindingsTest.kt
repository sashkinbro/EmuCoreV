package com.sbro.emucorev.core

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class KeyboardMouseBindingsTest {
    @Test
    fun classicKeyboardDefaultsAreAvailableWithoutUserSetup() {
        assertEquals(KeyEvent.KEYCODE_K, KeyboardMouseBindings.resolveKeyboardBinding("cross", emptyMap()))
        assertEquals(KeyEvent.KEYCODE_J, KeyboardMouseBindings.resolveKeyboardBinding("square", emptyMap()))
        assertEquals(KeyEvent.KEYCODE_DPAD_UP, KeyboardMouseBindings.resolveKeyboardBinding("dpad_up", emptyMap()))
        assertEquals(KeyEvent.KEYCODE_W, KeyboardMouseBindings.resolveKeyboardBinding("left_stick_up", emptyMap()))
        assertEquals(KeyEvent.KEYCODE_ENTER, KeyboardMouseBindings.resolveKeyboardBinding("start", emptyMap()))
        assertEquals(KeyEvent.KEYCODE_DEL, KeyboardMouseBindings.resolveKeyboardBinding("select", emptyMap()))
    }

    @Test
    fun classicMouseDefaultsAreAvailableWithoutUserSetup() {
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_LEFT,
            KeyboardMouseBindings.resolveMouseBinding("cross", emptyMap())
        )
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_RIGHT,
            KeyboardMouseBindings.resolveMouseBinding("circle", emptyMap())
        )
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_MIDDLE,
            KeyboardMouseBindings.resolveMouseBinding("square", emptyMap())
        )
    }

    @Test
    fun customBindingOverridesDefaultAndUnbindDisablesIt() {
        val overridden = mapOf("cross" to KeyEvent.KEYCODE_SPACE)
        assertEquals(KeyEvent.KEYCODE_SPACE, KeyboardMouseBindings.resolveKeyboardBinding("cross", overridden))
        val unbound = KeyboardMouseBindings.clear(overridden, "cross")
        assertEquals(KeyboardMouseBindings.UNBOUND, unbound["cross"])
        assertNull(KeyboardMouseBindings.resolveKeyboardBinding("cross", unbound))
    }

    @Test
    fun clearingADefaultBindingDisablesIt() {
        val unbound = KeyboardMouseBindings.clear(emptyMap(), "cross")
        assertEquals(KeyboardMouseBindings.UNBOUND, unbound["cross"])
        assertNull(KeyboardMouseBindings.resolveKeyboardBinding("cross", unbound))
    }

    @Test
    fun assigningOneKeyRemovesTheConflictingAction() {
        val bindings = mapOf(
            "square" to KeyEvent.KEYCODE_J,
            "cross" to KeyEvent.KEYCODE_K
        )
        val updated = KeyboardMouseBindings.assign(bindings, "cross", KeyEvent.KEYCODE_J)
        assertEquals(KeyEvent.KEYCODE_J, updated["cross"])
        assertFalse(updated.containsKey("square"))
    }

    @Test
    fun mouseBindingAssignmentUsesTheSameConflictRules() {
        val bindings = mapOf("cross" to KeyboardMouseBindings.MOUSE_BUTTON_LEFT)
        val updated = KeyboardMouseBindings.assign(
            bindings,
            "circle",
            KeyboardMouseBindings.MOUSE_BUTTON_LEFT
        )
        assertEquals(KeyboardMouseBindings.MOUSE_BUTTON_LEFT, updated["circle"])
        assertFalse(updated.containsKey("cross"))
    }

    @Test
    fun mouseDeltaRespectsSensitivityAndClampsToStickRange() {
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 10f,
            dy = -10f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 0
        )
        assertEquals(0.45f, x, 0.0001f)
        assertEquals(-0.45f, y, 0.0001f)

        val (fastX, _) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 100f,
            dy = 0f,
            sensitivityPercent = 300,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 0
        )
        assertEquals(1f, fastX, 0.0001f)
    }

    @Test
    fun mouseDeltaAppliesIndependentAxisInversion() {
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 10f,
            dy = 10f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = true,
            invertY = true,
            deadzonePercent = 0
        )
        assertEquals(-0.45f, x, 0.0001f)
        assertEquals(-0.45f, y, 0.0001f)
    }

    @Test
    fun tinyMouseNoiseIsDiscardedByTheAimDeadzone() {
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 0.1f,
            dy = -0.1f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 2
        )
        assertEquals(0f, x, 0.0001f)
        assertEquals(0f, y, 0.0001f)
    }

    @Test
    fun configuredDeadzoneOnlyRemovesMovementInsideItsRadius() {
        val (insideX, insideY) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 4f,
            dy = 0f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 20
        )
        assertEquals(0f, insideX, 0.0001f)
        assertEquals(0f, insideY, 0.0001f)

        val (outsideX, outsideY) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 10f,
            dy = 0f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 10
        )
        assertEquals(0.3889f, outsideX, 0.001f)
        assertEquals(0f, outsideY, 0.0001f)
    }

    @Test
    fun deadzoneZeroPreservesTheScaledMouseMagnitude() {
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 10f,
            dy = 0f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 100,
            invertX = false,
            invertY = false,
            deadzonePercent = 0
        )
        assertEquals(0.45f, x, 0.0001f)
        assertEquals(0f, y, 0.0001f)
    }

    @Test
    fun verticalSensitivityScalesOnlyTheYAxis() {
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = 10f,
            dy = 10f,
            sensitivityPercent = 100,
            verticalSensitivityPercent = 200,
            invertX = false,
            invertY = false,
            deadzonePercent = 0
        )
        assertEquals(0.45f, x, 0.0001f)
        assertEquals(0.9f, y, 0.0001f)
    }

    @Test
    fun smoothingMovesGraduallyTowardTheTarget() {
        assertEquals(1f, KeyboardMouseBindings.smoothAimValue(0f, 1f, 0), 0.0001f)
        assertEquals(0.5f, KeyboardMouseBindings.smoothAimValue(0f, 1f, 50), 0.0001f)
        assertEquals(0.9f, KeyboardMouseBindings.smoothAimValue(1f, 0f, 90), 0.0001f)
    }

    @Test
    fun leftHandedModeSwapsOnlyPrimaryAndSecondaryButtons() {
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_RIGHT,
            KeyboardMouseBindings.swapMouseButton(KeyboardMouseBindings.MOUSE_BUTTON_LEFT, true)
        )
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_LEFT,
            KeyboardMouseBindings.swapMouseButton(KeyboardMouseBindings.MOUSE_BUTTON_RIGHT, true)
        )
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_MIDDLE,
            KeyboardMouseBindings.swapMouseButton(KeyboardMouseBindings.MOUSE_BUTTON_MIDDLE, true)
        )
        assertEquals(
            KeyboardMouseBindings.MOUSE_BUTTON_LEFT,
            KeyboardMouseBindings.swapMouseButton(KeyboardMouseBindings.MOUSE_BUTTON_LEFT, false)
        )
    }

    @Test
    fun wheelModesMapToTheExpectedControllerActions() {
        assertEquals(null to null, KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_OFF))
        assertEquals("dpad_up" to "dpad_down", KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_DPAD))
        assertEquals("r2" to "l2", KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_TRIGGERS))
        assertEquals(
            "left_stick_up" to "left_stick_down",
            KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_LEFT_STICK)
        )
        assertEquals(
            "right_stick_up" to "right_stick_down",
            KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_RIGHT_STICK)
        )
        assertEquals(
            "r1" to "l1",
            KeyboardMouseBindings.mouseWheelActions(KeyboardMouseBindings.MOUSE_WHEEL_SHOULDERS)
        )
    }

    @Test
    fun commonKeyAndMouseLabelsAreReadable() {
        assertEquals("K", KeyboardMouseBindings.keyboardKeyLabel(KeyEvent.KEYCODE_K))
        assertEquals("Up", KeyboardMouseBindings.keyboardKeyLabel(KeyEvent.KEYCODE_DPAD_UP))
        assertEquals("Left click", KeyboardMouseBindings.mouseButtonLabel(KeyboardMouseBindings.MOUSE_BUTTON_LEFT))
        assertTrue(KeyboardMouseBindings.mouseButtonLabel(KeyboardMouseBindings.MOUSE_BUTTON_FORWARD).contains("Forward"))
    }
}
class KeyboardMouseActionsTest {
    @Test
    fun actionCatalogCoversDefaultsShortcutsAndWheelTargets() {
        val ids = KeyboardMouseActions.all.map { it.id }
        assertEquals(ids.size, ids.distinct().size)
        assertTrue(ids.containsAll(KeyboardMouseBindings.keyboardDefaults.keys))
        assertTrue(ids.containsAll(KeyboardMouseBindings.mouseDefaults.keys))
        assertTrue(ids.contains(KeyboardMouseActions.ACTION_QUICK_SAVE))
        assertTrue(ids.contains(KeyboardMouseActions.ACTION_QUICK_LOAD))
        val wheelIds = (1..KeyboardMouseBindings.MOUSE_WHEEL_SHOULDERS)
            .flatMap { mode -> KeyboardMouseBindings.mouseWheelActions(mode).toList() }
            .filterNotNull()
        assertTrue(ids.containsAll(wheelIds))
    }

    @Test
    fun digitalActionsUseVitaControllerBitsAndAnalogActionsUseDistinctAxes() {
        val digital = KeyboardMouseActions.all.filter { it.buttonMask != 0 }
        assertTrue(digital.all { it.buttonMask > 0 })
        assertEquals(digital.size, digital.map { it.buttonMask }.distinct().size)
        val analog = KeyboardMouseActions.all.filter { it.axisIndex >= 0 }
        assertTrue(analog.all { it.axisIndex in 0..3 })
        assertTrue(analog.all { it.axisValue == -1f || it.axisValue == 1f })
    }
}