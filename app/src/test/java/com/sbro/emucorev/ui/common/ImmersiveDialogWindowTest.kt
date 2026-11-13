package com.sbro.emucorev.ui.common

import android.app.Activity
import android.app.Dialog
import android.os.Build
import android.os.Looper
import android.view.View
import android.view.ViewTreeObserver
import android.view.Window
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf
import org.robolectric.RobolectricTestRunner
import org.robolectric.android.controller.ActivityController
import org.robolectric.annotation.Config
import org.robolectric.util.ReflectionHelpers
import org.robolectric.util.ReflectionHelpers.ClassParameter

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class ImmersiveDialogWindowTest : ImmersiveDialogWindowContract()

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [35])
class ImmersiveDialogWindowModernTest : ImmersiveDialogWindowContract()

@Suppress("DEPRECATION")
abstract class ImmersiveDialogWindowContract {
    private lateinit var activityController: ActivityController<Activity>
    private lateinit var activity: Activity
    private val dialogs = mutableListOf<Dialog>()
    private val bindings = mutableListOf<ImmersiveDialogWindowBinding>()

    @Before fun createActivity() {
        activityController = Robolectric.buildActivity(Activity::class.java).setup()
        activity = activityController.get()
        activity.setContentView(FrameLayout(activity))
        shadowOf(Looper.getMainLooper()).idle()
    }

    @After fun destroyActivity() {
        bindings.asReversed().forEach { it.close() }
        dialogs.asReversed().forEach { if (it.isShowing) it.dismiss() }
        shadowOf(Looper.getMainLooper()).idle()
        activityController.pause().stop().destroy()
    }

    private fun dialog(shown: Boolean = true): Dialog = Dialog(activity).also {
        it.setContentView(EditText(activity))
        dialogs += it
        if (shown) {
            it.show()
            shadowOf(Looper.getMainLooper()).idle()
        }
    }

    private fun bind(window: Window, restored: () -> Unit = {}, apply: (Window) -> Unit = ImmersiveMode::applyToDialog) =
        ImmersiveDialogWindowBinding(window, restored, apply).also { bindings += it }

    private fun focus(observer: ViewTreeObserver, focused: Boolean) {
        ReflectionHelpers.callInstanceMethod<Unit>(observer, "dispatchOnWindowFocusChange",
            ClassParameter.from(Boolean::class.javaPrimitiveType, focused))
    }

    private fun registeredFocusListeners(observer: ViewTreeObserver, binding: ImmersiveDialogWindowBinding): Int {
        val listeners: List<*>? = ReflectionHelpers.getField(observer, "mOnWindowFocusListeners")
        return listeners?.count { it === binding } ?: 0
    }

    @Test fun initialApplyTargetsDialogBeforeFirstPreDrawAndOnlyOncePerGuard() {
        val own = dialog(shown = false).window!!
        val applied = mutableListOf<Window>()
        val binding = bind(own, apply = { applied += it; ImmersiveMode.applyToDialog(it) })
        assertEquals(listOf(own), applied)
        assertNotSame(activity.window, applied.single())
        assertFalse("the guard must not cancel drawing", own.decorView.viewTreeObserver.dispatchOnPreDraw())
        assertEquals(2, applied.size)
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        assertEquals("first-frame guard must remove itself", 2, applied.size)
        binding.close()
    }

    @Test fun showingDialogDoesNotDuplicateMigratedFocusListener() {
        val modal = dialog(shown = false)
        val own = modal.window!!
        var calls = 0
        val binding = bind(own, apply = { calls++ })
        modal.show()
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(own.decorView.isAttachedToWindow)
        calls = 0
        focus(own.decorView.viewTreeObserver, true)
        assertEquals("floating observer merges on attachment; one gain must apply once", 1, calls)
        assertEquals(1, registeredFocusListeners(own.decorView.viewTreeObserver, binding))
        binding.close()
        assertEquals("cleanup must remove the migrated listener too", 0,
            registeredFocusListeners(own.decorView.viewTreeObserver, binding))
    }

    @Test fun focusLossDoesNothingAndFocusGainOnlyRefreshesOwnedWindow() {
        val own = dialog().window!!
        val applied = mutableListOf<Window>()
        bind(own, apply = { applied += it })
        applied.clear()
        focus(own.decorView.viewTreeObserver, false)
        assertTrue(applied.isEmpty())
        focus(own.decorView.viewTreeObserver, true)
        assertEquals(listOf(own), applied)
    }

    @Test fun afterFirstPreDrawFurtherFramesDoNotRehideBars() {
        val own = dialog().window!!
        bind(own)
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        if (Build.VERSION.SDK_INT < 30) {
            @Suppress("DEPRECATION")
            own.decorView.systemUiVisibility = 0
            own.decorView.viewTreeObserver.dispatchOnPreDraw()
            @Suppress("DEPRECATION")
            assertEquals(0, own.decorView.systemUiVisibility)
        } else {
            val controller = own.decorView.windowInsetsController!!
            controller.show(android.view.WindowInsets.Type.systemBars())
            val before = ReflectionHelpers.callInstanceMethod<Int>(controller, "getRequestedVisibleTypes")
            own.decorView.viewTreeObserver.dispatchOnPreDraw()
            assertEquals(before, ReflectionHelpers.callInstanceMethod<Int>(controller, "getRequestedVisibleTypes"))
        }
    }

    @Test fun dismissAndReattachRearmsFirstDrawWithoutMultiplyingFocusListeners() {
        val modal = dialog()
        val own = modal.window!!
        var calls = 0
        val binding = bind(own, apply = { calls++ })
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        modal.dismiss()
        shadowOf(Looper.getMainLooper()).idle()
        calls = 0
        modal.show()
        shadowOf(Looper.getMainLooper()).idle()
        assertSame(own.decorView, modal.window!!.decorView)
        assertTrue("reattach plus its first draw must refresh", calls >= 2)
        calls = 0
        focus(own.decorView.viewTreeObserver, true)
        assertEquals(1, calls)
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        assertEquals("reattach guard must already have removed itself", 1, calls)
        assertEquals(1, registeredFocusListeners(own.decorView.viewTreeObserver, binding))
    }

    @Test fun closeRemovesAllListenersAndRestoresParentOnce() {
        val modal = dialog()
        val own = modal.window!!
        var calls = 0
        var restored = 0
        val binding = bind(own, restored = { restored++ }, apply = { calls++ })
        binding.close()
        binding.close()
        val closedCalls = calls
        focus(own.decorView.viewTreeObserver, true)
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        modal.dismiss()
        shadowOf(Looper.getMainLooper()).idle()
        modal.show()
        shadowOf(Looper.getMainLooper()).idle()
        focus(own.decorView.viewTreeObserver, true)
        own.decorView.viewTreeObserver.dispatchOnPreDraw()
        assertEquals(closedCalls, calls)
        assertEquals(1, restored)
        assertEquals(0, registeredFocusListeners(own.decorView.viewTreeObserver, binding))
    }

    @Test fun oldRegistrationCloseDoesNotRemoveReplacementListeners() {
        val own = dialog().window!!
        var oldCalls = 0
        var newCalls = 0
        val old = bind(own, apply = { oldCalls++ })
        val replacement = bind(own, apply = { newCalls++ })
        old.close()
        oldCalls = 0
        newCalls = 0
        focus(own.decorView.viewTreeObserver, true)
        assertEquals(0, oldCalls)
        assertEquals(1, newCalls)
        assertEquals(0, registeredFocusListeners(own.decorView.viewTreeObserver, old))
        assertEquals(1, registeredFocusListeners(own.decorView.viewTreeObserver, replacement))
    }

    @Test fun actualAncestorResolverFindsClosestDialogAndNestedRestoreUsesIt() {
        val parent = dialog().window!!
        val child = dialog().window!!
        class Provider(window: Window) : FrameLayout(activity), DialogWindowProvider {
            override val window: Window = window
        }
        val outer = Provider(activity.window)
        val inner = Provider(parent)
        val content = FrameLayout(activity)
        outer.addView(inner)
        inner.addView(content)
        val resolver = Class.forName("com.sbro.emucorev.ui.common.AppDialogKt")
            .getDeclaredMethod("findDialogWindow", View::class.java).apply { isAccessible = true }
        val nearest = resolver.invoke(null, content) as Window
        assertSame(parent, nearest)
        val restored = mutableListOf<Window>()
        bind(child, restored = { restored += nearest; ImmersiveMode.applyToDialog(nearest) }).close()
        assertEquals(listOf(parent), restored)
        assertNotSame(activity.window, restored.single())
    }

    @Test fun hidePolicyPreservesWindowInputSecureTypeSizeTitleAndCallback() {
        for (fits in listOf(false, true)) {
            val own = dialog().window!!
            WindowCompat.setDecorFitsSystemWindows(own, fits)
            // Target/theme policy may resolve a request differently (for example enforced edge-to-edge).
            val beforeResolvedFits = if (Build.VERSION.SDK_INT >= 30) {
                ReflectionHelpers.callInstanceMethod<Boolean>(own, "decorFitsSystemWindows")
            } else null
            own.setLayout(453, 671)
            own.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE or
                WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
            own.addFlags(WindowManager.LayoutParams.FLAG_SECURE or WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
            own.setTitle("owned edit dialog")
            val callback = own.callback
            val before = WindowManager.LayoutParams().apply { copyFrom(own.attributes) }
            @Suppress("DEPRECATION") val beforeLayout = own.decorView.systemUiVisibility and
                (View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
            val binding = bind(own)
            binding.refresh()
            focus(own.decorView.viewTreeObserver, true)
            own.decorView.viewTreeObserver.dispatchOnPreDraw()
            binding.close()
            val after = own.attributes
            assertSame(callback, own.callback)
            assertEquals(before.width, after.width)
            assertEquals(before.height, after.height)
            assertEquals(before.softInputMode, after.softInputMode)
            assertEquals(before.flags, after.flags)
            assertEquals(before.type, after.type)
            assertSame(before.token, after.token)
            assertEquals(before.title.toString(), after.title.toString())
            if (Build.VERSION.SDK_INT >= 30) {
                assertEquals("hide-only must preserve the platform-resolved decor policy (requested=$fits)",
                    beforeResolvedFits, ReflectionHelpers.callInstanceMethod<Boolean>(own, "decorFitsSystemWindows"))
            } else {
                @Suppress("DEPRECATION") val afterLayout = own.decorView.systemUiVisibility and
                    (View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
                assertEquals(beforeLayout, afterLayout)
            }
        }
    }

    @Test fun actualPolicyHidesSystemBarsWithoutChangingImeRequest() {
        val own = dialog().window!!
        if (Build.VERSION.SDK_INT < 30) {
            // App themes may already request fullscreen; hiding bars must not add OR clear that flag.
            val beforeFullscreen = own.attributes.flags and WindowManager.LayoutParams.FLAG_FULLSCREEN
            ImmersiveMode.applyToDialog(own)
            @Suppress("DEPRECATION") val flags = own.decorView.systemUiVisibility
            assertTrue(flags and View.SYSTEM_UI_FLAG_FULLSCREEN != 0)
            assertTrue(flags and View.SYSTEM_UI_FLAG_HIDE_NAVIGATION != 0)
            assertTrue(flags and View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY != 0)
            assertEquals(beforeFullscreen, own.attributes.flags and WindowManager.LayoutParams.FLAG_FULLSCREEN)
        } else {
            val controller = own.decorView.windowInsetsController!!
            val mask = android.view.WindowInsets.Type.statusBars() or android.view.WindowInsets.Type.navigationBars() or android.view.WindowInsets.Type.ime()
            ReflectionHelpers.callInstanceMethod<Unit>(controller, "setRequestedVisibleTypes",
                ClassParameter.from(Int::class.javaPrimitiveType, mask), ClassParameter.from(Int::class.javaPrimitiveType, mask))
            val before = ReflectionHelpers.callInstanceMethod<Int>(controller, "getRequestedVisibleTypes")
            ImmersiveMode.applyToDialog(own)
            val after = ReflectionHelpers.callInstanceMethod<Int>(controller, "getRequestedVisibleTypes")
            assertTrue(before and android.view.WindowInsets.Type.ime() != 0)
            assertEquals(before and android.view.WindowInsets.Type.ime(), after and android.view.WindowInsets.Type.ime())
            assertEquals(0, after and (android.view.WindowInsets.Type.statusBars() or android.view.WindowInsets.Type.navigationBars()))
        }
        assertEquals(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE,
            WindowInsetsControllerCompat(own, own.decorView).systemBarsBehavior)
    }
}
