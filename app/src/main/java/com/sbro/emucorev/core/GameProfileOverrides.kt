package com.sbro.emucorev.core

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import java.lang.reflect.Field
import java.lang.reflect.Modifier

/**
 * A per-game profile stores the effective config plus the names of the fields the user really changed
 * (`user-overrides`). Only those fields are applied over the global settings, here and in the native core
 * (config/profile_overrides.h holds the shared name table); every other field keeps following the global settings.
 */
internal object ConfigOverrides {
    /** Driver choice has its own override mode in the profile. */
    private val excluded = setOf("customDriverName")

    /**
     * What a legacy profile (written before `user-overrides` existed) still pins: the renderer, as in the native
     * core's legacy_pinned, plus the app's own input-mapping fields. Everything else follows the global settings.
     */
    val LEGACY_PINNED: Set<String> = setOf(
        "backendRenderer",
        "enableGamepadOverlay", "overlayShowTouchSwitch", "overlayScale", "overlayOpacity", "disableMotion",
        "touchHaptics", "touchHapticsPreset", "touchHapticsStrength",
        "gyroMode", "gyroSensitivity", "gyroSmoothing", "gyroInvertX", "gyroInvertY",
        "analogMultiplier", "gamepadDeadzone", "gamepadTriggerThreshold", "gamepadButtonProfile",
        "gamepadVibration", "gamepadVibrationStrength", "deviceVibrationFallback", "gamepadSwapSticks",
        "gamepadInvertLeftX", "gamepadInvertLeftY", "gamepadInvertRightX", "gamepadInvertRightY",
        "showTouchpadCursor"
    )

    private val fields: List<Field> by lazy {
        VitaCoreConfig::class.java.declaredFields
            .filter { !Modifier.isStatic(it.modifiers) && !it.isSynthetic }
            .onEach { it.isAccessible = true }
    }

    fun changed(before: VitaCoreConfig, after: VitaCoreConfig): Set<String> =
        fields.filter { it.name !in excluded && it.get(before) != it.get(after) }.mapTo(linkedSetOf()) { it.name }

    /** [base] with the listed fields taken from [stored]. */
    fun merge(base: VitaCoreConfig, stored: VitaCoreConfig, names: Set<String>): VitaCoreConfig {
        val result = base.copy()
        fields.forEach { if (it.name in names && it.name !in excluded) it.set(result, it.get(stored)) }
        return result
    }

    fun encode(names: Set<String>): String = names.sorted().joinToString(",")

    fun decode(raw: String): Set<String> {
        val known = fields.mapTo(hashSetOf()) { it.name }
        return raw.split(',').map { it.trim() }.filter { it in known }.toSet()
    }
}

/**
 * Coalesces menu changes into background writes: the newest config wins, changed field names accumulate,
 * and one write runs at a time. Callers never block.
 */
class ProfileWriteQueue(
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher,
    private val write: (config: VitaCoreConfig, changed: Set<String>) -> Unit
) {
    private val lock = Any()
    private var pendingConfig: VitaCoreConfig? = null
    private var pendingFields = linkedSetOf<String>()
    private var running = false

    fun submit(config: VitaCoreConfig, changed: Set<String>) {
        if (changed.isEmpty()) return
        synchronized(lock) {
            pendingConfig = config
            pendingFields.addAll(changed)
            if (running) return
            running = true
        }
        scope.launch(dispatcher) { drain() }
    }

    private fun drain() {
        while (true) {
            val config: VitaCoreConfig
            val fields: Set<String>
            synchronized(lock) {
                config = pendingConfig ?: run { running = false; return }
                fields = pendingFields
                pendingConfig = null
                pendingFields = linkedSetOf()
            }
            runCatching { write(config, fields) }
        }
    }
}
