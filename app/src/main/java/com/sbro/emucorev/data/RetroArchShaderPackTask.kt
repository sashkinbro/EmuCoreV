package com.sbro.emucorev.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class RetroArchShaderPackTaskState(
    val busy: Boolean = false,
    val progress: RetroArchShaderInstallProgress? = null,
    val error: String? = null,
    val completion: Long = 0L
)

/** Process owned pack work survives closing the game menu or switching settings tabs. */
object RetroArchShaderPackTask {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val mutableState = MutableStateFlow(RetroArchShaderPackTaskState())
    val state: StateFlow<RetroArchShaderPackTaskState> = mutableState.asStateFlow()
    private var job: Job? = null

    @Synchronized
    fun start(operation: suspend ((RetroArchShaderInstallProgress) -> Unit) -> Result<Int>) {
        if (job?.isActive == true) return
        mutableState.value = RetroArchShaderPackTaskState(busy = true)
        job = scope.launch {
            try {
                val result = operation { progress ->
                    mutableState.value = mutableState.value.copy(progress = progress)
                }
                result.fold(
                    onSuccess = {
                        mutableState.value = RetroArchShaderPackTaskState(completion = mutableState.value.completion + 1)
                    },
                    onFailure = { failure ->
                        mutableState.value = RetroArchShaderPackTaskState(
                            error = failure.message ?: "Shader pack operation failed",
                            completion = mutableState.value.completion + 1
                        )
                    }
                )
            } catch (cancelled: CancellationException) {
                mutableState.value = RetroArchShaderPackTaskState(completion = mutableState.value.completion)
                throw cancelled
            } catch (failure: Exception) {
                mutableState.value = RetroArchShaderPackTaskState(
                    error = failure.message ?: "Shader pack operation failed",
                    completion = mutableState.value.completion + 1
                )
            } finally {
                mutableState.value = mutableState.value.copy(busy = false, progress = null)
                synchronized(this@RetroArchShaderPackTask) { job = null }
            }
        }
    }

    @Synchronized
    fun cancel() {
        job?.cancel()
    }
}
