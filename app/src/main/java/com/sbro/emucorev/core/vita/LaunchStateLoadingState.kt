package com.sbro.emucorev.core.vita

/** UI state for a launch whose initial game state is being restored. */
data class LaunchStateLoadingState(
    val requested: Boolean = false,
    val active: Boolean = false,
    val progress: Float = 0f,
    val terminal: Boolean = false
) {
    fun onNativeProgress(value: Float, active: Boolean): LaunchStateLoadingState {
        if (!requested || terminal) return this
        if (!active) {
            return if (this.active) copy(active = false, progress = 1f, terminal = true) else this
        }

        val safeProgress = if (value.isFinite()) value.coerceIn(0f, 1f) else progress
        return copy(active = true, progress = maxOf(progress, safeProgress))
    }

    fun onFailure(): LaunchStateLoadingState =
        if (requested && !terminal) copy(active = false, terminal = true) else this

    companion object {
        fun fromArguments(args: Array<String>?): LaunchStateLoadingState {
            if (args == null) return LaunchStateLoadingState()
            val stateIndex = args.indexOf("-loadstate")
            val hasStatePath = stateIndex >= 0 && stateIndex + 1 < args.size && args[stateIndex + 1].isNotBlank()
            return if (hasStatePath) {
                LaunchStateLoadingState(requested = true, active = true)
            } else {
                LaunchStateLoadingState()
            }
        }
    }
}
