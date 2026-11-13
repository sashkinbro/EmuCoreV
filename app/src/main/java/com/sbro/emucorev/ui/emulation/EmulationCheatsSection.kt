package com.sbro.emucorev.ui.emulation

import android.app.Application
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.core.VitaCheatSnapshot

@Composable
internal fun rememberEmulationCheatPacksViewModel(): EmulationCheatPacksViewModel {
    val application = LocalContext.current.applicationContext as Application
    // SDL supplies a bare ViewModelStoreOwner without Application creation extras.
    // Keep its retained store, but explicitly supply the AndroidViewModel factory.
    val factory = remember(application) {
        ViewModelProvider.AndroidViewModelFactory.getInstance(application)
    }
    return viewModel(key = "emulation-cheat-packs", factory = factory)
}

@Composable
internal fun EmulationCheatsSection(
    gameId: String,
    cheats: VitaCheatSnapshot,
    onInstalled: suspend () -> Unit
): Boolean {
    val model = rememberEmulationCheatPacksViewModel()
    val state by model.state.collectAsState()
    var acknowledgedRevision by remember(gameId) { mutableLongStateOf(0L) }
    LaunchedEffect(gameId) { model.bind(gameId) }
    LaunchedEffect(state.operation.target, state.operation.installedRevision) {
        val revision = state.operation.installedRevision
        if (revision > 0 && state.operation.target?.titleId.equals(gameId, true)) {
            onInstalled()
            acknowledgedRevision = revision
        }
    }
    // Old row indexes stay disabled until the authoritative overlay snapshot
    // has been refreshed after an atomic pack replacement.
    val refreshing = state.operation.installedRevision > acknowledgedRevision ||
        !state.operation.target?.titleId.equals(gameId, true)
    EmulationCheatPacksSection(
        state = state,
        hasInstalledCheats = cheats.cheats.isNotEmpty(),
        installedEntryId = model.installedEntryId(cheats),
        refreshing = refreshing,
        onLoad = model::load,
        onDownload = model::download
    )
    return state.operation.busy || state.identityLoading || refreshing
}
