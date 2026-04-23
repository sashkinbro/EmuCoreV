package com.sbro.emucorev.core.vita.overlay

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.vita.Emulator

class InputOverlay(context: Context) {
    private val appContext = context.applicationContext
    private val repository = VitaCoreConfigRepository(appContext)
    private var latestConfig: VitaCoreConfig = repository.load()
    private var controllerAttached = false
    private val emulator = context as? Emulator

    var hasReceivedCoreState by mutableStateOf(false)
        private set

    var coreOverlayMask by mutableIntStateOf(0)
        private set

    var overlayEditMode by mutableStateOf(false)
        private set

    var overlayScale by mutableFloatStateOf(latestConfig.overlayScale)
        private set

    var overlayOpacity by mutableIntStateOf(latestConfig.overlayOpacity.coerceIn(10, 100))
        private set

    val effectiveOverlayMask: Int
        get() {
            if (!latestConfig.enableGamepadOverlay) {
                return 0
            }
            if (!hasReceivedCoreState) {
                return buildDisplayMask(latestConfig)
            }
            if (coreOverlayMask == 0) {
                return 0
            }
            return buildDisplayMask(latestConfig)
        }

    fun synchronizeConfig(config: VitaCoreConfig) {
        latestConfig = config
        overlayScale = config.overlayScale
        overlayOpacity = config.overlayOpacity.coerceIn(10, 100)
        syncControllerAttachment()
    }

    fun setState(overlayMask: Int) {
        hasReceivedCoreState = true
        coreOverlayMask = overlayMask
        syncControllerAttachment()
    }

    fun setIsInEditMode(edit: Boolean) {
        overlayEditMode = edit
        if (edit) {
            emulator?.requestOverlayMenuButtonReveal()
        }
    }

    fun resetButtonPlacement() {
        persistConfig(
            latestConfig.copy(
                overlayScale = DEFAULT_OVERLAY_SCALE,
                overlayOpacity = DEFAULT_OVERLAY_OPACITY
            )
        )
    }

    fun setScale(scale: Float) {
        persistConfig(latestConfig.copy(overlayScale = scale))
    }

    fun setOpacity(opacity: Int) {
        persistConfig(latestConfig.copy(overlayOpacity = opacity.coerceIn(10, 100)))
    }

    fun dispose() {
        if (!controllerAttached) {
            return
        }
        detachController()
        controllerAttached = false
    }

    private fun persistConfig(config: VitaCoreConfig) {
        latestConfig = config
        overlayScale = config.overlayScale
        overlayOpacity = config.overlayOpacity.coerceIn(10, 100)
        repository.save(config)
        syncControllerAttachment()
    }

    private fun syncControllerAttachment() {
        val shouldAttach = effectiveOverlayMask != 0
        if (shouldAttach == controllerAttached) {
            return
        }
        if (shouldAttach) {
            attachController()
        } else {
            detachController()
        }
        controllerAttached = shouldAttach
    }

    private fun buildDisplayMask(config: VitaCoreConfig): Int {
        if (!config.enableGamepadOverlay) {
            return 0
        }
        var mask = OVERLAY_MASK_BASIC
        if (config.pstvMode) {
            mask = mask or OVERLAY_MASK_L2R2
        }
        if (config.overlayShowTouchSwitch) {
            mask = mask or OVERLAY_MASK_TOUCH_SCREEN_SWITCH
        }
        return mask
    }

    external fun attachController()

    external fun detachController()

    external fun setAxis(axis: Int, value: Short)

    external fun setButton(button: Int, value: Boolean)

    external fun setTouchState(isBack: Boolean)

    companion object {
        const val OVERLAY_MASK_BASIC = 1
        const val OVERLAY_MASK_L2R2 = 2
        const val OVERLAY_MASK_TOUCH_SCREEN_SWITCH = 4

        private const val DEFAULT_OVERLAY_SCALE = 0.9f
        private const val DEFAULT_OVERLAY_OPACITY = 100
    }

    object ControlId {
        const val a = 0
        const val b = 1
        const val x = 2
        const val y = 3
        const val select = 4
        const val guide = 5
        const val start = 6
        const val l1 = 9
        const val r1 = 10
        const val dup = 11
        const val ddown = 12
        const val dleft = 13
        const val dright = 14
        const val l2 = -4
        const val r2 = -5
        const val touch = 1024
        const val axis_left_x = 0
        const val axis_left_y = 1
        const val axis_right_x = 2
        const val axis_right_y = 3
    }
}
