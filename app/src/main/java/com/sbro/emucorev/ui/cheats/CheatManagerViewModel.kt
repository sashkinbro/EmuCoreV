package com.sbro.emucorev.ui.cheats

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.R
import com.sbro.emucorev.core.CheatBridge
import com.sbro.emucorev.core.VitaCheatSnapshot
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class CheatManagerViewModel(application: Application) : AndroidViewModel(application) {

    data class UiState(
        val games: List<InstalledVitaGame> = emptyList(),
        val selectedTitleId: String = "",
        val snapshot: VitaCheatSnapshot = VitaCheatSnapshot.EMPTY,
        val catalog: List<CheatCatalogEntry> = emptyList(),
        val catalogLoading: Boolean = false,
        val catalogFailed: Boolean = false,
        val installed: Boolean = false,
        val installedCatalogId: String? = null,
        val busy: Boolean = false,
        val messageRes: Int? = null
    )

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    private val catalogRepository = CheatCatalogRepository(application)
    private val coreConfigRepository = VitaCoreConfigRepository(application)

    init {
        viewModelScope.launch {
            val games = withContext(Dispatchers.IO) {
                InstalledGameRepository().loadInstalledGames(getApplication())
            }
            _state.update { it.copy(games = games, selectedTitleId = games.firstOrNull()?.titleId.orEmpty()) }
            refreshCheats()
            loadCatalog()
        }
    }

    fun selectGame(titleId: String) {
        if (titleId == _state.value.selectedTitleId) return
        _state.update { it.copy(selectedTitleId = titleId) }
        refreshCheats()
    }

    fun refreshCheats() {
        val titleId = _state.value.selectedTitleId
        if (titleId.isBlank()) {
            _state.update { it.copy(snapshot = VitaCheatSnapshot.EMPTY, installed = false, installedCatalogId = null) }
            return
        }
        val snapshot = CheatBridge.snapshot(titleId)
        val path = CheatBridge.getCheatFilePath(titleId)
        _state.update { it.copy(snapshot = snapshot, installed = !path.isNullOrBlank(), installedCatalogId = catalogRepository.installedEntryId(titleId, snapshot)) }
    }

    fun setMasterEnabled(enabled: Boolean) {
        CheatBridge.setCheatsEnabled(enabled)
        coreConfigRepository.save(coreConfigRepository.load().copy(enableCheats = enabled))
        refreshCheats()
    }

    fun setCheatEnabled(index: Int, enabled: Boolean) {
        val titleId = _state.value.selectedTitleId
        if (titleId.isBlank()) return
        CheatBridge.setCheatEnabled(titleId, index, enabled)
        CheatBridge.saveCheats(titleId)
        refreshCheats()
    }

    fun setAllCheatsEnabled(enabled: Boolean) {
        val titleId = _state.value.selectedTitleId
        if (titleId.isBlank()) return
        CheatBridge.setAllCheatsEnabled(titleId, enabled)
        CheatBridge.saveCheats(titleId)
        refreshCheats()
    }

    fun importFromUri(uri: Uri) {
        val titleId = _state.value.selectedTitleId
        if (titleId.isBlank() || _state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val name = queryDisplayName(uri)
                    val temp = File.createTempFile("cheat_import_", ".tmp", getApplication<Application>().cacheDir)
                    try {
                        val input = getApplication<Application>().contentResolver.openInputStream(uri)
                            ?: return@runCatching VitaCheatSnapshot.EMPTY
                        input.use { stream ->
                            temp.outputStream().use { output -> stream.copyTo(output) }
                        }
                        CheatBridge.importFile(titleId, temp.absolutePath, name ?: "$titleId.psv")
                    } finally {
                        temp.delete()
                    }
                }.getOrDefault(VitaCheatSnapshot.EMPTY)
            }
            val success = result.cheats.isNotEmpty()
            _state.update {
                it.copy(
                    busy = false,
                    messageRes = if (success) R.string.emulation_cheats_import_success else R.string.emulation_cheats_import_failed
                )
            }
            refreshCheats()
        }
    }

    fun download(entry: CheatCatalogEntry) {
        val game = _state.value.games.firstOrNull { it.titleId.equals(_state.value.selectedTitleId, ignoreCase = true) }
        if (game == null || !entry.titleId.equals(game.titleId, ignoreCase = true) ||
            cheatVersionCompatibility(game.version, entry.version) == CheatVersionCompatibility.MISMATCH) {
            _state.update { it.copy(messageRes = R.string.cheat_catalog_version_mismatch) }
            return
        }
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            val snapshot = try {
                catalogRepository.download(entry)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                null
            }
            val success = snapshot != null
            _state.update {
                it.copy(
                    busy = false,
                    messageRes = if (success) R.string.cheat_catalog_download_success else R.string.cheat_catalog_download_failed
                )
            }
            if (!success) return@launch
            if (entry.titleId.equals(_state.value.selectedTitleId, ignoreCase = true))
                refreshCheats()
        }
    }

    fun deleteCheats() {
        val titleId = _state.value.selectedTitleId
        if (titleId.isBlank()) return
        val deleted = CheatBridge.deleteCheats(titleId)
        _state.update {
            it.copy(messageRes = if (deleted) R.string.cheat_manager_delete_success else R.string.cheat_manager_delete_failed)
        }
        refreshCheats()
    }

    fun loadCatalog(force: Boolean = false) {
        viewModelScope.launch {
            _state.update { it.copy(catalogLoading = true, catalogFailed = false) }
            val entries = catalogRepository.load(force)
            _state.update { it.copy(catalog = entries, catalogLoading = false, catalogFailed = entries.isEmpty()) }
        }
    }

    fun consumeMessage() {
        _state.update { it.copy(messageRes = null) }
    }

    private fun queryDisplayName(uri: Uri): String? = runCatching {
        getApplication<Application>().contentResolver
            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
    }.getOrNull()
}
