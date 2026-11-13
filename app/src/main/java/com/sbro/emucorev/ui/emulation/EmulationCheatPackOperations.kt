package com.sbro.emucorev.ui.emulation

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import com.sbro.emucorev.ui.cheats.CheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.cheatVersionCompatibility
import java.util.Locale

internal data class CheatPackTarget(val titleId: String, val version: String?)
internal enum class CheatPackFeedback { INSTALLED, FAILED, INCOMPATIBLE }
internal data class CheatPackOperationState(
    val target: CheatPackTarget? = null,
    val busy: Boolean = false,
    val feedback: CheatPackFeedback? = null,
    val installedRevision: Long = 0
)

/** Retained by the emulation Activity's ViewModel, independent of menu composition. */
internal class EmulationCheatPackOperations(private val scope: CoroutineScope) {
    private val mutableState = MutableStateFlow(CheatPackOperationState())
    val state = mutableState.asStateFlow()

    @Volatile private var revision = 0L
    private var job: Job? = null

    fun bind(target: CheatPackTarget) {
        val normalized = target.copy(titleId = target.titleId.trim().uppercase(Locale.ROOT))
        if (normalized == mutableState.value.target) return
        ++revision
        job?.cancel()
        mutableState.value = CheatPackOperationState(target = normalized)
    }
    fun install(
        titleId: String,
        version: String,
        operation: suspend (canCommit: () -> Boolean) -> Boolean
    ) {
        val initial = mutableState.value
        if (initial.busy || !scope.isActive) return
        val target = initial.target
        // Match the native safe-filename contract, then exact Vita identity.
        val safe = Regex("[A-Za-z0-9_-]{1,128}")
        if (target == null || !safe.matches(target.titleId) || !safe.matches(titleId) ||
            !target.titleId.equals(titleId, ignoreCase = true) ||
            cheatVersionCompatibility(target.version, version) == CheatVersionCompatibility.MISMATCH) {
            mutableState.update { it.copy(feedback = CheatPackFeedback.INCOMPATIBLE) }
            return
        }
        val request = ++revision
        mutableState.update { it.copy(busy = true, feedback = null) }
        job = scope.launch {
            fun current() = revision == request && mutableState.value.target == target
            val operationContext = coroutineContext
            try {
                val installed = operation { current() && operationContext.isActive }
                coroutineContext.ensureActive()
                if (current()) mutableState.update {
                    it.copy(
                        feedback = if (installed) CheatPackFeedback.INSTALLED else CheatPackFeedback.FAILED,
                        installedRevision = it.installedRevision + if (installed) 1 else 0
                    )
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                if (current()) mutableState.update { it.copy(feedback = CheatPackFeedback.FAILED) }
            } finally {
                if (current()) mutableState.update { it.copy(busy = false) }
            }
        }
    }
}
