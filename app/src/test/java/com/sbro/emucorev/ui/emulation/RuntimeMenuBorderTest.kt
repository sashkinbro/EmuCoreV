package com.sbro.emucorev.ui.emulation

import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color
import com.sbro.emucorev.data.GameMenuLayoutStyle
import org.junit.Assert.*
import org.junit.Test

class RuntimeMenuBorderTest {
    private val scheme = darkColorScheme(primary = Color.Cyan, error = Color.Red,
        onSurface = Color.White, outlineVariant = Color.Gray)

    @Test fun flatContentDoesNotAcquireNestedFramesInAnyLayout() {
        for (style in GameMenuLayoutStyle.entries) {
            for (role in listOf(RuntimeMenuSurfaceRole.Section, RuntimeMenuSurfaceRole.Slider,
                RuntimeMenuSurfaceRole.Achievement, RuntimeMenuSurfaceRole.SaveSummary, RuntimeMenuSurfaceRole.Artwork)) {
                assertNull("$style/$role", runtimeMenuBorder(role, scheme, style))
            }
        }
    }

    @Test fun onlyActualCardAndNavigationRolesOwnFrames() {
        for (role in listOf(RuntimeMenuSurfaceRole.Outer, RuntimeMenuSurfaceRole.Header,
            RuntimeMenuSurfaceRole.Settings, RuntimeMenuSurfaceRole.Action,
            RuntimeMenuSurfaceRole.Navigation, RuntimeMenuSurfaceRole.RailNavigation)) {
            assertNotNull(role.name, runtimeMenuBorder(role, scheme))
        }
    }

    @Test fun navigationFocusWinsOverSelectedAndDestructiveState() {
        for (role in listOf(RuntimeMenuSurfaceRole.Navigation, RuntimeMenuSurfaceRole.RailNavigation)) {
            val idle = runtimeMenuBorder(role, scheme)!!
            val focused = runtimeMenuBorder(role, scheme, selected = true, destructive = true, focused = true)!!
            val selected = runtimeMenuBorder(role, scheme, selected = true)!!
            val destructive = runtimeMenuBorder(role, scheme, destructive = true)!!
            assertTrue(focused.width > idle.width)
            assertEquals(selected.width, destructive.width)
            assertNotEquals(idle.brush, selected.brush)
            assertNotEquals(selected.brush, destructive.brush)
            assertEquals(runtimeMenuBorder(role, scheme, focused = true), focused)
        }
    }

    @Test fun layoutShellsRetainTheirDifferentReferenceRoles() {
        val borders = GameMenuLayoutStyle.entries.map { runtimeMenuBorder(RuntimeMenuSurfaceRole.Outer, scheme, it) }
        assertEquals(GameMenuLayoutStyle.entries.size, borders.distinct().size)
    }
}
