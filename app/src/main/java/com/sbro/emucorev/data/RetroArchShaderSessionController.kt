package com.sbro.emucorev.data

import android.content.Context
import java.io.File
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers

/** Resolves global/per-title selection and keeps renderer recreation independent of Vita savestates. */
class RetroArchShaderSessionController(context: Context) {
    private val appContext = context.applicationContext
    private val selections = RetroArchShaderSelectionRepository(appContext)
    private val repository = RetroArchShaderRepository(appContext)
    private val applyMutex = Mutex()
    private val packRoot: File get() = File(appContext.filesDir, "retroarch-shaders")
    private var lastAppliedParameters: AppliedParameters? = null

    private data class AppliedParameters(val presetPath: String?, val values: Map<String, Float>)

    suspend fun applyForTitle(titleId: String, stillCurrent: () -> Boolean): Boolean {
        applyMutex.lock()
        try {
            if (!stillCurrent()) return false
            val (selection, preset) = withContext(Dispatchers.IO) {
                val effective = selections.effectiveFor(titleId)
                val selectedPreset = if (effective.enabled) repository.listPresets()
                    .firstOrNull { it.absolutePath == effective.presetPath } else null
                effective to selectedPreset
            }
            if (!stillCurrent()) return false
            val selectedPath = selection.presetPath.takeIf { selection.enabled }
            val applied = repository.applyPreset(selectedPath)
            if (!applied && selectedPath != null && stillCurrent()) {
                repository.applyPreset(null)
            }
            if (!applied) lastAppliedParameters = null
            if (applied && stillCurrent()) {
                val values = withContext(Dispatchers.IO) {
                    if (preset == null) emptyMap()
                    else {
                        val parameterScope = if (selection.inheritsGlobal) {
                            RetroArchShaderSelectionScope.GLOBAL
                        } else RetroArchShaderSelectionScope.GAME_OVERRIDE
                        selections.parameterOverrides(parameterScope, titleId, preset)
                    }
                }
                val desired = AppliedParameters(selectedPath, values)
                if (!stillCurrent()) return false
                if (desired != lastAppliedParameters) {
                    withContext(Dispatchers.IO) {
                        // Clear values left by another title/scope, then restore this effective selection.
                        repository.resetParameters()
                        if (preset != null) {
                            values.forEach { (id, value) -> repository.setParameter(preset, id, value) }
                        }
                    }
                    lastAppliedParameters = desired
                }
            }
            return applied
        } finally {
            applyMutex.unlock()
        }
    }

    /** Clears the process-wide pending selection when the emulator session ends. */
    fun stop() {
        runCatching {
            if (NativeRetroArchShaderBridge.isSupported()) {
                NativeRetroArchShaderBridge.setPreset(packRoot.canonicalPath, "")
                NativeRetroArchShaderBridge.resetParameters()
            }
            lastAppliedParameters = null
        }
    }
}
