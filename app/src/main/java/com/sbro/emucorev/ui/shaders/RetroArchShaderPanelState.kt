package com.sbro.emucorev.ui.shaders

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.RetroArchShaderPreset
import com.sbro.emucorev.data.RetroArchShaderSelection
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex

/** Owned by the screen/game session, rather than the currently visible menu tab. */
internal class RetroArchShaderPanelState(initialSelection: RetroArchShaderSelection) : ViewModel() {
    var presets by mutableStateOf(emptyList<RetroArchShaderPreset>())
    var selection by mutableStateOf(initialSelection)
    var supported by mutableStateOf(false)
    var officialPackInstalled by mutableStateOf(false)
    var loading by mutableStateOf(true)
    var error by mutableStateOf<String?>(null)
    val parameterValues = mutableStateMapOf<String, Float>()
    val parameterWriteMutex = Mutex()
    val scope get() = viewModelScope
    private var loadedRevision: Pair<Boolean, Long>? = null
    private var requestedRevision: Pair<Boolean, Long>? = null
    private var refreshJob: Job? = null

    fun observeSelection(changes: Flow<RetroArchShaderSelection>) {
        viewModelScope.launch { changes.collectLatest { selection = it } }
    }

    fun loadIfNeeded(vulkan: Boolean, packRevision: Long, load: suspend () -> Unit) {
        val revision = vulkan to packRevision
        if (loadedRevision == revision || (requestedRevision == revision && refreshJob?.isActive == true)) return
        refreshJob?.cancel()
        requestedRevision = revision
        refreshJob = viewModelScope.launch {
            loading = loadedRevision == null
            try {
                load()
                loadedRevision = revision
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (failure: Exception) {
                error = failure.message
            } finally {
                if (requestedRevision == revision) loading = false
            }
        }
    }
}
