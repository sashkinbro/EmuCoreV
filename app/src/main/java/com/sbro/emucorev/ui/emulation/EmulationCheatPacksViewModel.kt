package com.sbro.emucorev.ui.emulation

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.VitaCheatSnapshot
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.ui.cheats.CheatCatalogEntry
import com.sbro.emucorev.ui.cheats.CheatCatalogRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

internal data class EmulationCheatPacksState(
    val operation: CheatPackOperationState = CheatPackOperationState(),
    val identityLoading: Boolean = true,
    val catalog: List<CheatCatalogEntry> = emptyList(),
    val loading: Boolean = false,
    val failed: Boolean = false
)

/** Activity ViewModelStore retains downloads while the menu is dismissed. */
internal class EmulationCheatPacksViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = CheatCatalogRepository(application)
    private val operations = EmulationCheatPackOperations(viewModelScope)
    private val mutableState = MutableStateFlow(EmulationCheatPacksState())
    val state = mutableState.asStateFlow()
    private var boundTitle = ""
    private var catalogJob: Job? = null
    private var catalogRevision = 0L

    init {
        viewModelScope.launch {
            operations.state.collect { operation -> mutableState.update { it.copy(operation = operation) } }
        }
    }

    fun bind(titleId: String) {
        val title = titleId.trim().uppercase(Locale.ROOT)
        if (boundTitle == title) return
        boundTitle = title
        operations.bind(CheatPackTarget(title, null))
        mutableState.update { it.copy(identityLoading = true) }
        viewModelScope.launch {
            val game = withContext(Dispatchers.IO) {
                try { InstalledGameRepository().findByTitleId(getApplication(), title) }
                catch (cancelled: CancellationException) { throw cancelled }
                catch (_: Exception) { null }
            }
            if (boundTitle != title) return@launch
            operations.bind(CheatPackTarget(title, game?.takeIf { it.titleId.equals(title, true) }?.version))
            mutableState.update { it.copy(identityLoading = false) }
        }
        if (mutableState.value.catalog.isEmpty()) load()
    }

    fun load(force: Boolean = false) {
        val request = ++catalogRevision
        catalogJob?.cancel()
        mutableState.update { it.copy(loading = true, failed = false) }
        catalogJob = viewModelScope.launch {
            try {
                val entries = repository.load(force)
                if (request == catalogRevision) mutableState.update {
                    it.copy(catalog = entries, failed = entries.isEmpty())
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                if (request == catalogRevision) mutableState.update { it.copy(failed = true) }
            } finally {
                if (request == catalogRevision) mutableState.update { it.copy(loading = false) }
            }
        }
    }

    fun download(entry: CheatCatalogEntry) {
        if (mutableState.value.identityLoading) return
        operations.install(entry.titleId, entry.version) { canCommit ->
            repository.download(entry, canCommit) != null
        }
    }

    fun installedEntryId(snapshot: VitaCheatSnapshot): String? =
        repository.installedEntryId(boundTitle, snapshot)
}
