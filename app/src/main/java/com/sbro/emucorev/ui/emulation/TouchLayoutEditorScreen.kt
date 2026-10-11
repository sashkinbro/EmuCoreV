package com.sbro.emucorev.ui.emulation

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.ActivityInfo
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.ui.theme.LocalCustomizationSettings

/** Opens the exact in-game controls editor for either the global or selected-game profile. */
@Composable
fun TouchLayoutEditorScreen(
    titleId: String? = null,
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val activity = remember(context) { context.findActivity() }
    val customization = LocalCustomizationSettings.current
    DisposableEffect(activity) {
        val previousOrientation = activity?.requestedOrientation
        if (activity != null) {
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        }
        onDispose {
            if (activity != null && previousOrientation != null) {
                activity.requestedOrientation = previousOrientation
            }
        }
    }

    val layoutRepository = remember(context) { TouchControlLayoutRepository(context) }
    val globalConfigRepository = remember(context) { VitaCoreConfigRepository(context) }
    val gameSettingsRepository = remember(context) { VitaGameSettingsRepository(context) }
    var profileConfig by remember(context, titleId) {
        mutableStateOf(loadTouchLayoutEditorConfig(context, titleId))
    }
    var savedLayout by remember(layoutRepository, titleId) {
        mutableStateOf(loadTouchLayoutProfile(layoutRepository, titleId))
    }
    var profileReloadKey by remember(titleId) { mutableIntStateOf(0) }

    OnScreenControls(
        modifier = Modifier.fillMaxSize(),
        overlayScale = profileConfig.overlayScale,
        overlayOpacity = profileConfig.overlayOpacity,
        showTouchSwitch = profileConfig.overlayShowTouchSwitch,
        touchMode = 0,
        visualStyle = customization.touchControlVisualStyle,
        pressEffect = customization.touchControlPressEffect,
        touchHaptics = false,
        touchHapticsPreset = profileConfig.touchHapticsPreset,
        touchHapticsStrength = profileConfig.touchHapticsStrength,
        editMode = true,
        savedLayout = savedLayout,
        onLayoutChange = { updated ->
            savedLayout = updated
            if (titleId == null) layoutRepository.saveGlobal(updated)
            else layoutRepository.saveForGame(titleId, updated)
        },
        onEditDone = onBackClick,
        onEditReset = {
            if (titleId == null) {
                layoutRepository.resetGlobal()
            } else {
                layoutRepository.clearForGame(titleId)
            }
            val resetConfig = profileConfig.copy(
                overlayScale = DEFAULT_OVERLAY_SCALE,
                overlayOpacity = 100
            )
            if (titleId == null) globalConfigRepository.save(resetConfig)
            else gameSettingsRepository.save(titleId, resetConfig)
            profileConfig = resetConfig
            savedLayout = loadTouchLayoutProfile(layoutRepository, titleId)
            profileReloadKey++
        },
        onBackTouchToggle = {},
        onButtonChange = { _, _ -> },
        onAxisChange = { _, _ -> },
        editorProfileRevision = profileReloadKey
    )
}

internal fun loadTouchLayoutEditorConfig(context: Context, titleId: String?): VitaCoreConfig =
    if (titleId.isNullOrBlank()) {
        VitaCoreConfigRepository(context).ensureDefaultsPersisted()
    } else {
        VitaGameSettingsRepository(context).loadEffective(titleId)
    }

private fun loadTouchLayoutProfile(repository: TouchControlLayoutRepository, titleId: String?) =
    if (titleId.isNullOrBlank()) repository.loadGlobal() else repository.loadEffective(titleId)

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

private const val DEFAULT_OVERLAY_SCALE = 0.9f
