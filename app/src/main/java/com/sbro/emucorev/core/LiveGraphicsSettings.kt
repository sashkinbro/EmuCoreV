package com.sbro.emucorev.core

/**
 * Which graphics and performance options the in-game menu offers, and the rules for switching
 * the internal resolution of a running session.
 *
 * An option is shown only if the running core can apply it immediately and completely. Anything
 * that needs a new GPU device, driver, backend or memory layout is configured in the settings
 * screens instead; the in-game menu never shows it and never labels it.
 */
object LiveGraphicsSettings {
    const val MIN_RESOLUTION = 0.5f
    const val MAX_RESOLUTION = 8f
    const val RESOLUTION_STEP = 0.5f

    enum class Option {
        /** Internal resolution multiplier: surfaces, render targets and the surface cache are rebuilt on the render thread. */
        RESOLUTION,

        /** Screen filter: a command consumed by the render thread. */
        SCREEN_FILTER,
        STRETCH_DISPLAY,

        /** Only the OpenGL presenter honours it; the Vulkan swapchain picks its own present mode. */
        V_SYNC,
        FRAME_LIMIT,
        FPS_HACK,

        /** Only meaningful with a custom Vulkan driver (the stock driver has no turbo hook). */
        TURBO_MODE,
        DISABLE_SURFACE_SYNC,
        SHADER_NOTICE
    }

    /**
     * Options that cannot be applied to a running session. They are listed so the classification
     * stays explicit and tested; none of them is ever offered in the in-game menu.
     */
    enum class SetupOnlyOption {
        BACKEND_RENDERER,
        GRAPHICS_DEVICE,
        CUSTOM_DRIVER,
        HIGH_ACCURACY,
        MEMORY_MAPPING,
        CPU_OPT,
        AUDIO_BACKEND,
        VALIDATION_LAYER,
        ACCURATE_THREAD_SCHEDULING,
        EAGER_SURFACE_SYNC,

        /** Cached samplers keep their old anisotropy and cannot be rebuilt while draws are recorded. */
        ANISOTROPIC_FILTERING
    }

    data class Capabilities(
        val vulkan: Boolean,
        /** Largest resolution multiplier the running session accepts; 0 when it cannot switch at all. */
        val resolutionLimit: Float,
        val customDriver: Boolean
    )

    fun capabilities(config: VitaCoreConfig, nativeResolutionLimit: Float): Capabilities {
        val vulkan = config.backendRenderer.equals("Vulkan", ignoreCase = true)
        return Capabilities(
            vulkan = vulkan,
            // The native side reports 0 for anything but a running Vulkan session.
            resolutionLimit = if (vulkan && nativeResolutionLimit >= MIN_RESOLUTION) nativeResolutionLimit else 0f,
            customDriver = config.customDriverName.isNotBlank()
        )
    }

    fun liveOptions(capabilities: Capabilities): Set<Option> = buildSet {
        if (capabilities.resolutionLimit >= MIN_RESOLUTION) add(Option.RESOLUTION)
        add(Option.SCREEN_FILTER)
        add(Option.STRETCH_DISPLAY)
        if (!capabilities.vulkan) add(Option.V_SYNC)
        add(Option.FRAME_LIMIT)
        add(Option.FPS_HACK)
        if (capabilities.vulkan && capabilities.customDriver) add(Option.TURBO_MODE)
        add(Option.DISABLE_SURFACE_SYNC)
        add(Option.SHADER_NOTICE)
    }

    /** Selectable multipliers, ascending, on the 0.5x grid and not above the session limit. */
    fun resolutionChoices(limit: Float): List<Float> {
        if (limit < MIN_RESOLUTION) return emptyList()
        val top = minOf(limit, MAX_RESOLUTION)
        val count = ((top - MIN_RESOLUTION) / RESOLUTION_STEP + 1e-4f).toInt()
        return (0..count).map { MIN_RESOLUTION + it * RESOLUTION_STEP }
    }

    /** The grid value for [requested], or null when it is not an allowed choice. */
    fun normalizeResolution(requested: Float, limit: Float): Float? =
        resolutionChoices(limit).firstOrNull { kotlin.math.abs(it - requested) < 1e-3f }

    /**
     * A save-state load restores the session at the save's internal resolution (its surfaces need it).
     * The configured (profile) resolution still wins: this is the value to switch back to live after
     * the load, or null when nothing is to be done (already there, nothing rendering, or the
     * configured value is not selectable here). The profile itself is never rewritten from a save.
     */
    fun resolutionAfterLoad(configured: Float, live: Float, limit: Float): Float? {
        if (live <= 0f || kotlin.math.abs(configured - live) < 1e-3f) return null
        return normalizeResolution(configured, limit)
    }

    /** What the resolution chip shows: the live value when the session reports a selectable one, else the profile value. */
    fun displayedResolution(profile: Float, live: Float, limit: Float): Float =
        if (live > 0f) normalizeResolution(live, limit) ?: profile else profile

    fun formatResolution(value: Float): String =
        if (value == value.toInt().toFloat()) "${value.toInt()}x" else "${value}x"
}

/**
 * Order of a live resolution switch. The game is already paused by the menu; this makes sure of it,
 * hands the value to the core (which applies it on the render thread at a frame boundary) and only
 * then writes it to the game profile, so the stored value never differs from what is running.
 */
class LiveResolutionSwitch(
    private val ensurePaused: suspend () -> Unit,
    private val applyToCore: suspend (Float) -> Boolean,
    private val persist: (Float) -> Unit
) {
    enum class Outcome { APPLIED, UNCHANGED, REJECTED, FAILED }

    suspend fun switchTo(current: Float, requested: Float, limit: Float): Outcome {
        val target = LiveGraphicsSettings.normalizeResolution(requested, limit) ?: return Outcome.REJECTED
        if (kotlin.math.abs(target - current) < 1e-3f) return Outcome.UNCHANGED
        ensurePaused()
        if (!applyToCore(target)) return Outcome.FAILED
        persist(target)
        return Outcome.APPLIED
    }
}

/**
 * Applies a live resolution change at a frame boundary. The core refuses (SCENE_OPEN) while a scene is open, as
 * it is when the menu pause stopped the guest between begin and end of a scene; [advanceFrame] lets the guest run
 * briefly and pauses it again. After [maxAttempts] refusals the change is made mid-scene (the core then carries the
 * render target pixels over) so the switch can never be starved.
 */
suspend fun applyResolutionAtFrameBoundary(
    maxAttempts: Int,
    tryApply: suspend (allowMidScene: Boolean) -> Int,
    advanceFrame: suspend () -> Unit
): Boolean {
    repeat(maxAttempts) { attempt ->
        val last = attempt == maxAttempts - 1
        when (tryApply(last)) {
            APPLY_OK -> return true
            APPLY_SCENE_OPEN -> if (!last) advanceFrame()
            else -> return false
        }
    }
    return false
}

const val APPLY_OK = 1
const val APPLY_SCENE_OPEN = 2
