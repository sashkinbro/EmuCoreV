package com.sbro.emucorev.ui.shaders

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.BorderStroke
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.TextButton
import androidx.compose.material3.Text
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.common.AppDialog as Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.mutableStateMapOf
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import com.sbro.emucorev.R
import com.sbro.emucorev.data.NativeRetroArchShaderBridge
import com.sbro.emucorev.data.RetroArchShaderParameter
import com.sbro.emucorev.data.RetroArchShaderPreset
import com.sbro.emucorev.data.RetroArchShaderInstallProgress
import com.sbro.emucorev.data.RetroArchShaderPackTask
import com.sbro.emucorev.data.RetroArchShaderRepository
import com.sbro.emucorev.data.RetroArchShaderSelectionRepository
import com.sbro.emucorev.data.RetroArchShaderSessionController
import com.sbro.emucorev.data.RetroArchShaderSelectionScope
import com.sbro.emucorev.data.RetroArchShaderSelection
import com.sbro.emucorev.ui.common.AppAlertDialog as AlertDialog
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonCornerAccent
import com.sbro.emucorev.ui.theme.neon.neonAccentColor
import kotlin.math.roundToInt

private data class ShaderPresetGroup(val key: String, val title: String, val presets: List<RetroArchShaderPreset>)

private fun shaderShortLabel(label: String): String = label.substringAfterLast('/')

private fun shaderCategoryTitle(category: String): String = category
    .replace('-', ' ').replace('_', ' ').split(' ').filter(String::isNotBlank)
    .joinToString(" ") { word ->
        when (word.lowercase()) {
            "crt", "gba", "gb", "lcd", "nes", "ntsc", "pal", "snes", "vhs" -> word.uppercase()
            else -> word.replaceFirstChar { it.titlecase() }
        }
    }

private fun shaderPresetGroups(presets: List<RetroArchShaderPreset>): List<ShaderPresetGroup> = presets
    .filter { '/' in it.label }.groupBy { it.label.substringBefore('/') }
    .map { (category, items) -> ShaderPresetGroup(category, shaderCategoryTitle(category), items.sortedBy { it.label.lowercase() }) }
    .sortedBy { it.title.lowercase() }

@Composable
private fun ShaderPresetPicker(
    presets: List<RetroArchShaderPreset>,
    selectedPath: String,
    noneLabel: String,
    useGlobalLabel: String,
    inheritsGlobal: Boolean,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
    onUseGlobal: (() -> Unit)?
) {
    var query by remember { mutableStateOf("") }
    var expandedGroup by remember { mutableStateOf<String?>(null) }
    val groups = remember(presets) { shaderPresetGroups(presets) }
    val rootPresets = remember(presets) { presets.filterNot { '/' in it.label }.sortedBy { it.label.lowercase() } }
    val matches = remember(presets, query) {
        val q = query.trim()
        presets.filter { it.label.contains(q, ignoreCase = true) }
    }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Surface(
            modifier = Modifier.fillMaxWidth(0.94f).heightIn(max = 680.dp),
            shape = neonShape(24.dp), color = MaterialTheme.colorScheme.surface,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.shader_choose_preset), style = MaterialTheme.typography.titleLarge)
                OutlinedTextField(
                    value = query, onValueChange = { query = it; expandedGroup = null },
                    modifier = Modifier.fillMaxWidth(), singleLine = true,
                    placeholder = { Text(stringResource(R.string.shader_choose_preset)) }, shape = neonShape(14.dp)
                )
                LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    if (query.isNotBlank()) {
                        if (onUseGlobal != null) item(key = "global") { ShaderPickerRow(useGlobalLabel, inheritsGlobal, onUseGlobal) }
                        item(key = "none") { ShaderPickerRow(noneLabel, selectedPath.isBlank() && !inheritsGlobal, { onSelect("") }) }
                        items(matches, key = { "match:${it.absolutePath}" }) { preset ->
                            ShaderPickerRow(preset.label, preset.absolutePath == selectedPath, { onSelect(preset.absolutePath) })
                        }
                    } else if (expandedGroup == null) {
                        if (onUseGlobal != null) item(key = "global") { ShaderPickerRow(useGlobalLabel, inheritsGlobal, onUseGlobal) }
                        item(key = "none") { ShaderPickerRow(noneLabel, selectedPath.isBlank() && !inheritsGlobal, { onSelect("") }) }
                        items(rootPresets, key = { "root:${it.absolutePath}" }) { preset ->
                            ShaderPickerRow(shaderShortLabel(preset.label), preset.absolutePath == selectedPath, { onSelect(preset.absolutePath) })
                        }
                        items(groups, key = { "group:${it.key}" }) { group ->
                            ShaderPickerRow("${group.title}  ·  ${group.presets.size}", group.presets.any { it.absolutePath == selectedPath }) { expandedGroup = group.key }
                        }
                    } else {
                        item(key = "groupback") { ShaderPickerRow("‹  ${shaderCategoryTitle(expandedGroup.orEmpty())}", false) { expandedGroup = null } }
                        items(groups.firstOrNull { it.key == expandedGroup }?.presets.orEmpty(), key = { "preset:${it.absolutePath}" }) { preset ->
                            ShaderPickerRow(shaderShortLabel(preset.label), preset.absolutePath == selectedPath, { onSelect(preset.absolutePath) })
                        }
                    }
                }
                TextButton(onClick = onDismiss, shape = neonButtonShape()) { Text(stringResource(R.string.common_cancel)) }
            }
        }
    }
}

@Composable
private fun ShaderPickerRow(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(
        onClick = onClick, modifier = Modifier.fillMaxWidth(), shape = neonShape(12.dp),
        color = if (selected) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.32f) else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.52f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Text(label, modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp), style = MaterialTheme.typography.bodyMedium)
    }
}

enum class RetroArchShaderUiContext { GLOBAL, PER_GAME, IN_GAME }

data class RetroArchShaderUiState(
    val supported: Boolean,
    val context: RetroArchShaderUiContext,
    val enabled: Boolean,
    val selectedPresetPath: String,
    val presets: List<RetroArchShaderPreset>,
    val inheritsGlobal: Boolean = false,
    val installing: Boolean = false,
    val installFraction: Float? = null,
    val error: String? = null,
    val parameterValues: Map<String, Float> = emptyMap()
)

/** Labels are supplied from Android resources by the owning screen; no strings are embedded here. */
data class RetroArchShaderUiStrings(
    val title: String,
    val description: String,
    val globalScope: String,
    val gameScope: String,
    val inGameScope: String,
    val inherited: String,
    val enabled: String,
    val unsupported: String,
    val choosePreset: String,
    val noPreset: String,
    val noPresets: String,
    val downloadPack: String,
    val importPack: String,
    val useGlobal: String,
    val installing: String,
    val parameters: String,
    val resetParameters: String
)

@Composable
fun RetroArchShaderSettingsSection(
    state: RetroArchShaderUiState,
    strings: RetroArchShaderUiStrings,
    onEnabledChange: (Boolean) -> Unit,
    onPresetChange: (String) -> Unit,
    onDownloadPack: () -> Unit,
    onImportPack: () -> Unit,
    onParameterChange: (String, Float) -> Unit,
    onResetParameters: () -> Unit,
    onRemovePreset: ((String) -> Unit)? = null,
    onUseGlobal: (() -> Unit)? = null,
    onCancelInstall: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    var pendingRemovePath by remember { mutableStateOf<String?>(null) }
    val currentPreset = remember(state.presets, state.selectedPresetPath) {
        state.presets.firstOrNull { it.absolutePath == state.selectedPresetPath }
    }
    Surface(
        modifier = modifier.fillMaxWidth().neonCornerAccent(
            accent = neonAccentColor(strings.title.hashCode().mod(3)), markSize = 9.dp
        ),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(strings.title, style = MaterialTheme.typography.titleMedium)
            Text(strings.description, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(
                when (state.context) {
                    RetroArchShaderUiContext.GLOBAL -> strings.globalScope
                    RetroArchShaderUiContext.PER_GAME -> strings.gameScope
                    RetroArchShaderUiContext.IN_GAME -> strings.inGameScope
                },
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary
            )
            if (!state.supported) {
                Text(strings.unsupported, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(strings.enabled, style = MaterialTheme.typography.bodyLarge)
                    Switch(checked = state.enabled, onCheckedChange = onEnabledChange)
                }
                if (state.context != RetroArchShaderUiContext.GLOBAL && onUseGlobal != null) {
                    if (state.inheritsGlobal) {
                        Text(strings.inherited, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    OutlinedButton(onClick = onUseGlobal, enabled = !state.inheritsGlobal, shape = neonButtonShape()) { Text(strings.useGlobal) }
                }

                var pickerOpen by remember { mutableStateOf(false) }
                val selectedLabel = currentPreset?.label?.let(::shaderShortLabel) ?: strings.noPreset
                OutlinedButton(onClick = { pickerOpen = true }, shape = neonButtonShape()) {
                    Text("${strings.choosePreset}: $selectedLabel")
                }
                if (pickerOpen) {
                    ShaderPresetPicker(
                        presets = state.presets,
                        selectedPath = state.selectedPresetPath,
                        noneLabel = strings.noPreset,
                        useGlobalLabel = strings.useGlobal,
                        inheritsGlobal = state.inheritsGlobal,
                        onDismiss = { pickerOpen = false },
                        onSelect = { path -> onPresetChange(path); pickerOpen = false },
                        onUseGlobal = onUseGlobal?.let { action -> { action(); pickerOpen = false } }
                    )
                }

                if (state.presets.isEmpty()) {
                    Text(strings.noPresets, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if (!state.selectedPresetPath.isBlank() && onRemovePreset != null) {
                    OutlinedButton(onClick = { pendingRemovePath = state.selectedPresetPath }, shape = neonButtonShape()) {
                        Text(stringResource(R.string.controls_editor_delete))
                    }
                }
                Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    Button(onClick = onDownloadPack, enabled = !state.installing, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) { Text(strings.downloadPack) }
                    OutlinedButton(onClick = onImportPack, enabled = !state.installing, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) { Text(strings.importPack) }
                }
                if (state.installing) {
                    Text(strings.installing, style = MaterialTheme.typography.bodySmall)
                    state.installFraction?.let { LinearProgressIndicator(progress = { it.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth()) }
                    onCancelInstall?.let { action ->
                        TextButton(onClick = action, shape = neonButtonShape()) { Text(stringResource(R.string.common_cancel)) }
                    }
                }
                state.error?.takeIf(String::isNotBlank)?.let {
                    Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
                }
                if (state.enabled && currentPreset?.parameters.orEmpty().isNotEmpty()) {
                    Text(strings.parameters, style = MaterialTheme.typography.titleSmall)
                    currentPreset?.parameters.orEmpty().forEach { parameter ->
                        ShaderParameterControl(
                            parameter = parameter,
                            value = state.parameterValues[parameter.id] ?: parameter.initialValue,
                            onValueChange = { onParameterChange(parameter.id, it) }
                        )
                    }
                    OutlinedButton(onClick = onResetParameters, shape = neonButtonShape()) { Text(strings.resetParameters) }
                }
            }
        }
    }
    pendingRemovePath?.let { path ->
        AlertDialog(
            onDismissRequest = { pendingRemovePath = null },
            title = { Text(stringResource(R.string.controls_editor_delete)) },
            text = { Text(currentPreset?.label?.substringAfterLast('/') ?: strings.noPreset) },
            confirmButton = {
                TextButton(onClick = { pendingRemovePath = null; onRemovePreset?.invoke(path) }, shape = neonButtonShape()) {
                    Text(stringResource(R.string.controls_editor_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingRemovePath = null }, shape = neonButtonShape()) {
                    Text(stringResource(R.string.common_cancel))
                }
            }
        )
    }
}

@Composable
fun RetroArchShaderInGamePanel(
    state: RetroArchShaderUiState,
    strings: RetroArchShaderUiStrings,
    onEnabledChange: (Boolean) -> Unit,
    onPresetChange: (String) -> Unit,
    onDownloadPack: () -> Unit,
    onImportPack: () -> Unit,
    onParameterChange: (String, Float) -> Unit,
    onResetParameters: () -> Unit,
    onRemovePreset: ((String) -> Unit)? = null,
    onUseGlobal: (() -> Unit)? = null,
    onCancelInstall: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) = RetroArchShaderSettingsSection(
    state = state.copy(context = RetroArchShaderUiContext.IN_GAME),
    strings = strings,
    onEnabledChange = onEnabledChange,
    onPresetChange = onPresetChange,
    onDownloadPack = onDownloadPack,
    onImportPack = onImportPack,
    onParameterChange = onParameterChange,
    onResetParameters = onResetParameters,
    onRemovePreset = onRemovePreset,
    onUseGlobal = onUseGlobal,
    onCancelInstall = onCancelInstall,
    modifier = modifier
)

@Composable
private fun ShaderParameterControl(
    parameter: RetroArchShaderParameter,
    value: Float,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(parameter.description.ifBlank { parameter.id }, style = MaterialTheme.typography.bodyMedium)
            Text("${parameter.id}: ${"%.2f".format(value)}", style = MaterialTheme.typography.labelMedium)
        }
        if (parameter.maximum > parameter.minimum) {
            Slider(
                value = value.coerceIn(parameter.minimum, parameter.maximum),
                onValueChange = { raw ->
                    val stepIndex = ((raw - parameter.minimum) / parameter.step).roundToInt()
                    onValueChange((parameter.minimum + stepIndex * parameter.step).coerceIn(parameter.minimum, parameter.maximum))
                },
                valueRange = parameter.minimum..parameter.maximum
            )
        }
    }
}

/** Stateful, usable surface shared by global, per-game and in-game settings. */
@Composable
fun RetroArchShaderFeaturePanel(
    context: RetroArchShaderUiContext,
    titleId: String? = null,
    vulkanRenderer: Boolean = true,
    modifier: Modifier = Modifier
) {
    val androidContext = LocalContext.current
    val appContext = androidContext.applicationContext
    val repository = remember(appContext) { RetroArchShaderRepository(appContext) }
    val selections = remember(appContext) { RetroArchShaderSelectionRepository(appContext) }
    val sessionController = remember(appContext) { RetroArchShaderSessionController(appContext) }
    val scope = rememberCoroutineScope()
    var presets by remember { mutableStateOf(emptyList<RetroArchShaderPreset>()) }
    var selection by remember(titleId, context) {
        mutableStateOf(
            runCatching {
                if (context == RetroArchShaderUiContext.GLOBAL) selections.global()
                else titleId?.takeIf(String::isNotBlank)?.let(selections::effectiveFor)
                    ?: selections.global()
            }.getOrElse {
                RetroArchShaderSelection(false, "", RetroArchShaderSelectionScope.GLOBAL, false)
            }
        )
    }
    var supported by remember(vulkanRenderer) { mutableStateOf(vulkanRenderer && runCatching { NativeRetroArchShaderBridge.isSupported() }.getOrDefault(false)) }
    var loading by remember { mutableStateOf(true) }
    val packTask by RetroArchShaderPackTask.state.collectAsState()
    var error by remember { mutableStateOf<String?>(null) }
    val parameterValues = remember { mutableStateMapOf<String, Float>() }
    val parameterWriteMutex = remember { Mutex() }
    val currentTitleId by rememberUpdatedState(titleId)

    suspend fun refreshParameterValues() {
        val preset = presets.firstOrNull { it.absolutePath == selection.presetPath }
        val saved = if (preset != null) withContext(Dispatchers.IO) {
            selections.parameterOverrides(selection.scope, currentTitleId, preset)
        } else emptyMap()
        parameterValues.clear()
        preset?.parameters?.forEach { parameter ->
            parameterValues[parameter.id] = saved[parameter.id] ?: parameter.initialValue
        }
    }

    suspend fun refresh() {
        supported = vulkanRenderer && runCatching { NativeRetroArchShaderBridge.isSupported() }.getOrDefault(false)
        presets = withContext(Dispatchers.IO) { repository.listPresets() }
        selection = withContext(Dispatchers.IO) {
            if (context == RetroArchShaderUiContext.GLOBAL) selections.global()
            else titleId?.takeIf(String::isNotBlank)?.let(selections::effectiveFor) ?: selections.global()
        }
        refreshParameterValues()
        loading = false
    }

    LaunchedEffect(titleId, context, vulkanRenderer) {
        loading = true
        try {
            refresh()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            error = failure.message
        } finally {
            loading = false
        }
    }

    LaunchedEffect(packTask.completion) {
        if (packTask.completion > 0L) {
            try { refresh() } catch (cancelled: CancellationException) { throw cancelled }
            catch (failure: Exception) { error = failure.message }
        }
    }

    LaunchedEffect(supported, selection.enabled) {
        if (!supported || !selection.enabled) return@LaunchedEffect
        while (true) {
            delay(750L)
            val nativeError = runCatching { repository.lastApplyError() }.getOrNull()
            if (!nativeError.isNullOrBlank()) error = nativeError
        }
    }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            try {
                androidContext.contentResolver.takePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) { }
            error = null
            RetroArchShaderPackTask.start { onProgress -> repository.importArchive(uri, onProgress) }
        }
    }

    fun installOfficialPack() {
        error = null
        RetroArchShaderPackTask.start { onProgress -> repository.downloadOfficialPack(onProgress) }
    }

    fun applyCurrentTitle() {
        val title = currentTitleId?.takeIf(String::isNotBlank) ?: return
        scope.launch {
            val applied = try {
                sessionController.applyForTitle(title) { title.equals(currentTitleId, ignoreCase = true) }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                false
            }
            if (!applied && selection.enabled) {
                error = runCatching { repository.lastApplyError() }.getOrNull()
                    ?: androidContext.getString(R.string.shader_unsupported)
            } else if (!selection.enabled) {
                error = null
            }
        }
    }

    fun persistSelection(enabled: Boolean, path: String) {
        scope.launch {
            val selectedPath = path.takeIf { selected -> presets.any { it.absolutePath == selected } }.orEmpty()
            try {
                withContext(Dispatchers.IO) {
                    if (context == RetroArchShaderUiContext.GLOBAL) {
                        selections.setGlobal(enabled, selectedPath)
                    } else {
                        val game = currentTitleId?.takeIf(String::isNotBlank) ?: return@withContext
                        selections.setGameOverride(game, enabled, selectedPath)
                    }
                }
                selection = withContext(Dispatchers.IO) {
                    if (context == RetroArchShaderUiContext.GLOBAL) selections.global()
                    else currentTitleId?.takeIf(String::isNotBlank)?.let(selections::effectiveFor) ?: selections.global()
                }
                refreshParameterValues()
                error = null
                applyCurrentTitle()
            } catch (failure: Exception) {
                if (failure is CancellationException) throw failure
                error = failure.message
            }
        }
    }

    fun removePreset(path: String) {
        scope.launch {
            try {
                withContext(Dispatchers.IO) {
                    repository.removePresets(setOf(path)).getOrThrow()
                    selections.clearSelectionsForRemovedPresets(setOf(path))
                }
                refresh()
                applyCurrentTitle()
            } catch (failure: Exception) {
                if (failure is CancellationException) throw failure
                error = failure.message
            }
        }
    }

    val uiState = RetroArchShaderUiState(
        supported = supported,
        context = context,
        enabled = selection.enabled,
        selectedPresetPath = selection.presetPath,
        presets = presets,
        inheritsGlobal = selection.inheritsGlobal,
        installing = packTask.busy,
        installFraction = packTask.progress?.fraction,
        error = error ?: packTask.error,
        parameterValues = parameterValues.toMap()
    )
    val strings = RetroArchShaderUiStrings(
        title = stringResource(R.string.shader_title),
        description = stringResource(R.string.shader_description),
        globalScope = stringResource(R.string.shader_scope_global),
        gameScope = stringResource(R.string.shader_scope_game),
        inGameScope = stringResource(R.string.shader_scope_ingame),
        inherited = stringResource(R.string.shader_inherited),
        useGlobal = stringResource(R.string.shader_use_global),
        enabled = stringResource(R.string.shader_enabled),
        unsupported = stringResource(R.string.shader_unsupported),
        choosePreset = stringResource(R.string.shader_choose_preset),
        noPreset = stringResource(R.string.shader_none),
        noPresets = stringResource(R.string.shader_no_presets),
        downloadPack = stringResource(R.string.shader_download),
        importPack = stringResource(R.string.shader_import),
        installing = stringResource(R.string.shader_installing),
        parameters = stringResource(R.string.shader_parameters),
        resetParameters = stringResource(R.string.shader_reset_parameters)
    )

    if (!loading) {
        RetroArchShaderSettingsSection(
            state = uiState,
            strings = strings,
            onEnabledChange = { enabled -> persistSelection(enabled, selection.presetPath) },
            onPresetChange = { path -> persistSelection(path.isNotBlank(), path) },
            onDownloadPack = ::installOfficialPack,
            onImportPack = { importLauncher.launch(arrayOf("application/zip", "application/octet-stream")) },
            onParameterChange = { id, value ->
                presets.firstOrNull { it.absolutePath == selection.presetPath }?.let { preset ->
                    parameterValues[id] = value
                    val inheritedValues = parameterValues.toMap()
                    val title = currentTitleId
                    scope.launch {
                        parameterWriteMutex.withLock {
                            try {
                                val createOverride = context != RetroArchShaderUiContext.GLOBAL && selection.inheritsGlobal
                                val targetScope = if (context == RetroArchShaderUiContext.GLOBAL) {
                                    RetroArchShaderSelectionScope.GLOBAL
                                } else RetroArchShaderSelectionScope.GAME_OVERRIDE
                                withContext(Dispatchers.IO) {
                                    if (createOverride && !title.isNullOrBlank()) {
                                        selections.setGameOverride(title, selection.enabled, preset.absolutePath)
                                        inheritedValues.forEach { (parameter, savedValue) ->
                                            selections.setParameterOverride(targetScope, title, preset, parameter, savedValue)
                                        }
                                    } else {
                                        selections.setParameterOverride(targetScope, title, preset, id, value)
                                    }
                                }
                                if (createOverride && !title.isNullOrBlank()) {
                                    selection = withContext(Dispatchers.IO) { selections.effectiveFor(title) }
                                }
                                repository.setParameter(preset, id, value)
                            } catch (cancelled: CancellationException) { throw cancelled }
                            catch (failure: Exception) { error = failure.message }
                        }
                    }
                }
            },
            onResetParameters = {
                presets.firstOrNull { it.absolutePath == selection.presetPath }?.let { preset ->
                    scope.launch {
                        parameterWriteMutex.withLock {
                            try {
                                val title = currentTitleId
                                val targetScope = if (context == RetroArchShaderUiContext.GLOBAL) {
                                    RetroArchShaderSelectionScope.GLOBAL
                                } else RetroArchShaderSelectionScope.GAME_OVERRIDE
                                withContext(Dispatchers.IO) {
                                    if (selection.inheritsGlobal && !title.isNullOrBlank()) {
                                        selections.setGameOverride(title, selection.enabled, preset.absolutePath)
                                    }
                                    selections.clearParameterOverrides(targetScope, title, preset)
                                }
                                if (context != RetroArchShaderUiContext.GLOBAL && !title.isNullOrBlank()) {
                                    selection = withContext(Dispatchers.IO) { selections.effectiveFor(title) }
                                }
                                refreshParameterValues()
                                repository.resetParameters()
                                applyCurrentTitle()
                            } catch (cancelled: CancellationException) { throw cancelled }
                            catch (failure: Exception) { error = failure.message }
                        }
                    }
                }
            },
            onRemovePreset = ::removePreset,
            onCancelInstall = RetroArchShaderPackTask::cancel,
            onUseGlobal = if (context == RetroArchShaderUiContext.PER_GAME || context == RetroArchShaderUiContext.IN_GAME) {
                {
                    scope.launch {
                        currentTitleId?.let { withContext(Dispatchers.IO) { selections.clearGameOverride(it) } }
                        refresh()
                        applyCurrentTitle()
                    }
                }
            } else null,
            modifier = modifier
        )
    }
}
