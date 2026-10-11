package com.sbro.emucorev.ui.emulation

import android.content.Context
import android.content.ContextWrapper
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import java.io.File
import org.junit.After
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
class TouchLayoutEditorConfigTest {
    private lateinit var context: Context
    private lateinit var runtimeRoot: File

    @Before fun setUp() {
        val app = RuntimeEnvironment.getApplication()
        runtimeRoot = File(app.cacheDir, "touch-layout-editor-config-test").apply {
            deleteRecursively()
            mkdirs()
        }
        context = IsolatedEditorContext(app, runtimeRoot)
        context.getSharedPreferences(TEST_CONFIG_PREFS, Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After fun tearDown() {
        context.getSharedPreferences(TEST_CONFIG_PREFS, Context.MODE_PRIVATE).edit().clear().commit()
        runtimeRoot.deleteRecursively()
    }

    @Test fun editorDefaultsUseGlobalAndEffectiveGameOverlaySettings() {
        val globalRepository = VitaCoreConfigRepository(context)
        val globalInput = globalRepository.load().copy(
            overlayScale = 1.5f,
            overlayOpacity = 63,
            overlayShowTouchSwitch = true
        )
        globalRepository.save(globalInput)

        val gameInput = globalInput.copy(
            overlayScale = 0.55f,
            overlayOpacity = 42,
            overlayShowTouchSwitch = false
        )
        VitaGameSettingsRepository(context).saveProfile(TEST_TITLE_ID, gameInput, null)

        val global = loadTouchLayoutEditorConfig(context, titleId = null)
        val game = loadTouchLayoutEditorConfig(context, titleId = TEST_TITLE_ID)

        assertEquals(1.5f, global.overlayScale)
        assertEquals(63, global.overlayOpacity)
        assertTrue(global.overlayShowTouchSwitch)
        assertEquals(0.55f, game.overlayScale)
        assertEquals(42, game.overlayOpacity)
        assertTrue(!game.overlayShowTouchSwitch)

        val globalDefault = defaultLayout(global.overlayScale)
        val gameDefault = defaultLayout(game.overlayScale)
        assertTrue(globalDefault.getValue(TouchControlIds.CROSS).width >
            gameDefault.getValue(TouchControlIds.CROSS).width)
    }

    private fun defaultLayout(scale: Float) = buildDefaultTouchLayout(
        canvasWidth = 1600f,
        canvasHeight = 900f,
        isLandscape = true,
        overlayScale = scale,
        density = 1f,
        sidePaddingPx = 16f,
        bottomPaddingPx = 22f,
        shoulderTopPaddingPx = 22f
    ).associateBy { it.id }

    private class IsolatedEditorContext(base: Context, private val runtimeRoot: File) : ContextWrapper(base) {
        override fun getApplicationContext(): Context = this

        override fun getSharedPreferences(name: String?, mode: Int) = super.getSharedPreferences(
            if (name == "emucorev_ui_config") TEST_CONFIG_PREFS else name,
            mode
        )

        override fun getExternalFilesDir(type: String?): File = runtimeRoot
    }

    companion object {
        private const val TEST_CONFIG_PREFS = "touch_layout_editor_config_test"
        private const val TEST_TITLE_ID = "PCSE00333"
    }
}
