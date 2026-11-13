package com.sbro.emucorev.ui.settings

import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.controllerFocus
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.annotation.StringRes
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.AdrenoFamily
import com.sbro.emucorev.core.DeviceGpuInfoProvider
import com.sbro.emucorev.core.GpuInfoSource
import com.sbro.emucorev.core.GpuVendor
import com.sbro.emucorev.core.GpuDriverCompatibilityState
import com.sbro.emucorev.core.GpuDriverSelectionDecision
import com.sbro.emucorev.core.GpuDriverSelectionResult
import com.sbro.emucorev.core.GpuDriverCompatibility
import com.sbro.emucorev.core.GpuDriverMatch
import com.sbro.emucorev.core.GpuDriverRecommendations
import com.sbro.emucorev.core.GpuDriverVendor
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.RemoteGpuDriver
import com.sbro.emucorev.core.SnapdragonGpuProfile
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.vendor
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
@OptIn(ExperimentalLayoutApi::class)
fun GpuDriverScreen(
    onBackClick: () -> Unit,
    targetTitleId: String? = null,
    viewModel: SettingsViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val scope = rememberCoroutineScope()
    val gameSettingsRepository = remember(context) { VitaGameSettingsRepository(context) }
    val gameScopedTitleId = targetTitleId?.takeIf(String::isNotBlank)
    var gameConfig by remember(gameScopedTitleId) { mutableStateOf<VitaCoreConfig?>(null) }
    var gameDriverOverride by remember(gameScopedTitleId) { mutableStateOf<String?>(null) }
    val activeConfig = gameConfig ?: uiState.coreConfig
    val publishedDevice by DeviceGpuInfoProvider.snapshot.collectAsState()
    val device = publishedDevice ?: DeviceGpuInfoProvider.get(context)
    val selection = GpuDriverCompatibility.resolveSelection(
        uiState.coreConfig.customDriverName, if (gameScopedTitleId != null) gameDriverOverride else null,
        device, uiState.installedGpuDrivers
    )
    val selectedDriver = uiState.installedGpuDrivers.singleOrNull { it.name == selection.requestedName }
    val isGameScoped = gameScopedTitleId != null
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val installFailedMessage = stringResource(R.string.settings_gpu_driver_install_failed)
    val installSuccessTemplate = stringResource(R.string.settings_gpu_driver_install_success, "%s")
    val backClick = rememberDebouncedClick(onClick = onBackClick)
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    var filtersVisible by rememberSaveable { mutableStateOf(false) }
    var variantFilter by rememberSaveable { mutableStateOf(GPU_DRIVER_FILTER_ALL) }
    var sourceFilter by rememberSaveable { mutableStateOf(GPU_DRIVER_FILTER_ALL) }
    var expandedDriverId by rememberSaveable { mutableStateOf<String?>(null) }
    var cpuName by remember { mutableStateOf(DeviceGpuInfoProvider.cpuName()) }
    val deviceVendor = when (device.model?.vendor) {
        GpuVendor.ADRENO -> GpuDriverVendor.ADRENO
        GpuVendor.MALI -> GpuDriverVendor.MALI
        GpuVendor.XCLIPSE -> GpuDriverVendor.XCLIPSE
        GpuVendor.POWERVR -> GpuDriverVendor.POWERVR
        else -> GpuDriverVendor.UNKNOWN
    }
    val deviceProfile = GpuDriverRecommendations.profileForGpu(cpuName, device.model)
    val isAdrenoDevice = deviceVendor == GpuDriverVendor.ADRENO
    val deviceHintRes = if (isAdrenoDevice) {
        R.string.settings_gpu_driver_device_hint
    } else {
        R.string.settings_gpu_driver_device_hint_generic
    }
    val compatibilityTitleRes = when (deviceVendor) {
        GpuDriverVendor.ADRENO -> R.string.settings_gpu_driver_compatibility
        GpuDriverVendor.MALI -> R.string.settings_gpu_driver_compatibility_mali
        GpuDriverVendor.XCLIPSE -> R.string.settings_gpu_driver_compatibility_xclipse
        else -> R.string.settings_gpu_driver_compatibility_generic
    }
    val otherFamilyLabelRes = if (isAdrenoDevice) {
        R.string.settings_gpu_driver_other_family
    } else {
        R.string.settings_gpu_driver_other_family_generic
    }
    val remoteDrivers = uiState.remoteGpuDrivers.filter { driver ->
        driver.matchesSearch(searchQuery) &&
            (variantFilter == GPU_DRIVER_FILTER_ALL || driver.variant.equals(variantFilter, ignoreCase = true)) &&
            (sourceFilter == GPU_DRIVER_FILTER_ALL || driver.sourceLabel().equals(sourceFilter, ignoreCase = true))
    }.sortedByDescending { it.recommendationRank(deviceProfile, deviceVendor) }
    val variantFilters = buildList {
        add(GPU_DRIVER_FILTER_ALL)
        addAll(uiState.remoteGpuDrivers.map { it.variant }.filter { it.isNotBlank() }.distinct().sorted())
    }
    val sourceFilters = buildList {
        add(GPU_DRIVER_FILTER_ALL)
        addAll(uiState.remoteGpuDrivers.map { it.sourceLabel() }.distinct().sorted())
    }
    fun refreshGameDriverState() {
        val titleId = gameScopedTitleId ?: return
        scope.launch {
            val profile = withContext(Dispatchers.IO) {
                gameSettingsRepository.loadProfile(titleId)
            }
            gameConfig = profile.config
            gameDriverOverride = profile.customDriverOverride
        }
    }
    fun applyGameDriverOverride(driverName: String?) {
        val titleId = gameScopedTitleId ?: return
        if (gameConfig == null) return
        if (!driverName.isNullOrBlank() && !GpuDriverCompatibility.resolveSelection(
                viewModel.uiState.value.coreConfig.customDriverName, driverName,
                DeviceGpuInfoProvider.get(context), viewModel.uiState.value.installedGpuDrivers
            ).requestable) return
        scope.launch {
            val profile = withContext(Dispatchers.IO) {
                val current = gameSettingsRepository.loadProfile(titleId)
                val effectiveConfig = current.config.copy(
                    customDriverName = driverName ?: uiState.coreConfig.customDriverName
                )
                gameSettingsRepository.saveProfile(titleId, effectiveConfig, driverName)
                gameSettingsRepository.loadProfile(titleId)
            }
            gameConfig = profile.config
            gameDriverOverride = profile.customDriverOverride
        }
    }
    fun applyDriver(driverName: String) {
        if (isGameScoped) {
            applyGameDriverOverride(driverName)
        } else {
            viewModel.selectGpuDriver(driverName)
        }
    }
    fun useSystemDriver() {
        if (isGameScoped) {
            applyGameDriverOverride("")
        } else {
            viewModel.useSystemGpuDriver()
        }
    }

    val localDriverPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.installGpuDriver(uri, applyGlobally = !isGameScoped) { result ->
            result.onSuccess { driverName ->
                if (isGameScoped) {
                    applyGameDriverOverride(driverName)
                }
                Toast.makeText(context, installSuccessTemplate.format(driverName), Toast.LENGTH_SHORT).show()
            }.onFailure { error ->
                Toast.makeText(context, error.message ?: installFailedMessage, Toast.LENGTH_LONG).show()
            }
        }
    }

    LaunchedEffect(gameScopedTitleId, uiState.coreConfig.customDriverName) {
        refreshGameDriverState()
    }

    LaunchedEffect(context) {
        DeviceGpuInfoProvider.ensureLoaded(context)
        // The CPU may refine while the GPU snapshot remains equal (no StateFlow emission).
        cpuName = DeviceGpuInfoProvider.cpuName()
    }

    LaunchedEffect(Unit) {
        if (uiState.remoteGpuDrivers.isEmpty() && !uiState.gpuDriverCatalogLoading) {
            viewModel.refreshGpuDriverCatalog()
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            top = topInset,
            end = ScreenHorizontalPadding,
            bottom = 40.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            GpuDriverHeader(
                title = if (isGameScoped) {
                    stringResource(R.string.settings_gpu_driver_game_title)
                } else {
                    stringResource(R.string.settings_gpu_driver_manager_title)
                },
                onBackClick = backClick,
                searchActive = searchVisible || searchQuery.isNotBlank(),
                filtersActive = variantFilter != GPU_DRIVER_FILTER_ALL || sourceFilter != GPU_DRIVER_FILTER_ALL,
                onSearchClick = { searchVisible = !searchVisible },
                onFiltersClick = { filtersVisible = !filtersVisible }
            )
        }
        item {
            ActiveDriverCard(
                selectedDriver = selectedDriver,
                selection = selection,
                loading = !uiState.gpuDriverPackagesLoaded || (isGameScoped && gameConfig == null),
                backendRenderer = activeConfig.backendRenderer,
                isGameScoped = isGameScoped,
                gameDriverOverride = gameDriverOverride,
                onUseGlobal = { applyGameDriverOverride(null) },
                onUseSystem = ::useSystemDriver,
                onInstallFromFile = { localDriverPicker.launch(arrayOf("application/zip", "*/*")) },
                onRemove = {
                    val driverName = selection.requestedName
                    if (driverName.isNotBlank()) {
                        viewModel.removeGpuDriver(driverName)
                    }
                }
            )
        }
        item {
            DeviceCompatibilityCard(
                socName = cpuName,
                gpuName = when (device.source) {
                    GpuInfoSource.SOC_CATALOG -> stringResource(R.string.settings_gpu_driver_device_catalog, device.model?.displayName.orEmpty())
                    GpuInfoSource.UNKNOWN -> stringResource(R.string.settings_gpu_driver_device_unknown)
                    else -> device.model?.displayName ?: stringResource(R.string.settings_gpu_driver_device_unknown)
                },
                hintRes = deviceHintRes
            )
        }
        if (uiState.installedGpuDrivers.isNotEmpty()) {
            item {
                SectionLabel(text = stringResource(R.string.settings_gpu_driver_installed))
            }
            items(uiState.installedGpuDrivers, key = { it.name }) { driver ->
                InstalledDriverRow(
                    driver = driver,
                    selection = GpuDriverCompatibility.resolveSelection(driver.name, null, device, uiState.installedGpuDrivers),
                    selected = if (isGameScoped) gameDriverOverride == driver.name else uiState.coreConfig.customDriverName == driver.name,
                    onSelect = { applyDriver(driver.name) },
                    onRemove = { viewModel.removeGpuDriver(driver.name) }
                )
            }
        }
        item {
            AnimatedVisibility(
                visible = searchVisible,
                enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
            ) {
                DriverSearchField(
                    searchQuery = searchQuery,
                    onSearchQueryChange = { searchQuery = it },
                    onClose = {
                        searchQuery = ""
                        searchVisible = false
                    }
                )
            }
        }
        item {
            AnimatedVisibility(
                visible = filtersVisible && uiState.remoteGpuDrivers.isNotEmpty(),
                enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
            ) {
                DriverFilterPanel(
                    variantFilter = variantFilter,
                    onVariantFilterChange = { variantFilter = it },
                    sourceFilter = sourceFilter,
                    onSourceFilterChange = { sourceFilter = it },
                    variantFilters = variantFilters,
                    sourceFilters = sourceFilters
                )
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                SectionLabel(
                    text = stringResource(R.string.settings_gpu_driver_catalog),
                    modifier = Modifier.weight(1f)
                )
                OutlinedButton(
                    shape = neonButtonShape(),
                    onClick = viewModel::refreshGpuDriverCatalog,
                    enabled = !uiState.gpuDriverCatalogLoading
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_refresh),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
        }
        if (uiState.gpuDriverCatalogLoading) {
            item {
                LoadingCard(text = stringResource(R.string.settings_gpu_driver_catalog_loading))
            }
        }
        uiState.gpuDriverCatalogError?.let { error ->
            item {
                ErrorCard(
                    title = stringResource(R.string.settings_gpu_driver_catalog_failed),
                    message = error
                )
            }
        }
        if (!uiState.gpuDriverCatalogLoading && uiState.remoteGpuDrivers.isEmpty() && uiState.gpuDriverCatalogError == null) {
            item {
                LoadingCard(text = stringResource(R.string.settings_gpu_driver_catalog_empty))
            }
        }
        if (!uiState.gpuDriverCatalogLoading && uiState.remoteGpuDrivers.isNotEmpty() && remoteDrivers.isEmpty()) {
            item {
                ErrorCard(
                    title = stringResource(R.string.settings_gpu_driver_no_matches),
                    message = stringResource(R.string.settings_gpu_driver_no_matches_desc)
                )
            }
        }
        items(remoteDrivers, key = { it.id }) { driver ->
            val installedDriver = uiState.installedGpuDrivers.firstOrNull { it.name == driver.installedDriverName() }
            val downloadingProgress = uiState.gpuDriverDownloads[driver.id]
            val catalogCompatibility = GpuDriverCompatibility.catalogCompatibility(driver, device)
            val installedSelection = installedDriver?.let {
                GpuDriverCompatibility.resolveSelection(it.name, null, device, uiState.installedGpuDrivers)
            }
            RemoteDriverRow(
                driver = driver,
                match = when (catalogCompatibility.state) {
                    GpuDriverCompatibilityState.COMPATIBLE -> GpuDriverRecommendations.match(driver, deviceProfile, deviceVendor)
                    GpuDriverCompatibilityState.INCOMPATIBLE -> GpuDriverMatch.OTHER_FAMILY
                    GpuDriverCompatibilityState.UNVERIFIED -> GpuDriverMatch.UNKNOWN
                },
                canApply = installedSelection?.requestable ?: (catalogCompatibility.state == GpuDriverCompatibilityState.COMPATIBLE),
                availability = installedSelection?.let { gpuDriverSelectionSummary(it, "Vulkan", false).second.takeIf(String::isNotEmpty) }
                    ?: if (catalogCompatibility.state == GpuDriverCompatibilityState.COMPATIBLE) null
                    else stringResource(if (catalogCompatibility.state == GpuDriverCompatibilityState.INCOMPATIBLE)
                        R.string.settings_gpu_driver_status_incompatible_selection else R.string.settings_gpu_driver_status_unverified_selection),
                compatibilityTitleRes = compatibilityTitleRes,
                otherFamilyLabelRes = otherFamilyLabelRes,
                expanded = expandedDriverId == driver.id,
                installedDriver = installedDriver,
                selected = if (isGameScoped) {
                    installedDriver?.name == gameDriverOverride
                } else {
                    installedDriver?.name == uiState.coreConfig.customDriverName
                },
                downloading = downloadingProgress != null,
                progress = downloadingProgress ?: 0f,
                onToggleExpanded = {
                    expandedDriverId = if (expandedDriverId == driver.id) null else driver.id
                },
                onDownload = {
                    viewModel.installRemoteGpuDriver(driver, applyGlobally = !isGameScoped) { result ->
                        result.onSuccess { driverName ->
                            if (isGameScoped) {
                                applyGameDriverOverride(driverName)
                            }
                            Toast.makeText(context, installSuccessTemplate.format(driverName), Toast.LENGTH_SHORT).show()
                        }.onFailure { error ->
                            Toast.makeText(context, error.message ?: installFailedMessage, Toast.LENGTH_LONG).show()
                        }
                    }
                },
                onSelect = {
                    installedDriver?.let { applyDriver(it.name) }
                },
                onRemove = {
                    installedDriver?.let { viewModel.removeGpuDriver(it.name) }
                }
            )
        }
    }
}

@Composable
private fun DriverSearchField(
    searchQuery: String,
    onSearchQueryChange: (String) -> Unit,
    onClose: () -> Unit
) {
    OutlinedTextField(
        value = searchQuery,
        onValueChange = onSearchQueryChange,
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
        trailingIcon = {
            IconButton(onClick = onClose) {
                Icon(Icons.Rounded.Close, contentDescription = null)
            }
        },
        placeholder = { Text(stringResource(R.string.settings_gpu_driver_search)) },
        shape = neonShape(28.dp)
    )
}

@Composable
private fun DriverFilterPanel(
    variantFilter: String,
    onVariantFilterChange: (String) -> Unit,
    sourceFilter: String,
    onSourceFilterChange: (String) -> Unit,
    variantFilters: List<String>,
    sourceFilters: List<String>
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(vertical = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            FilterChipRow(
                filters = variantFilters,
                selected = variantFilter,
                onSelected = onVariantFilterChange
            )
            FilterChipRow(
                filters = sourceFilters,
                selected = sourceFilter,
                onSelected = onSourceFilterChange
            )
        }
    }
}

@Composable
private fun FilterChipRow(
    filters: List<String>,
    selected: String,
    onSelected: (String) -> Unit
) {
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(filters) { filter ->
            FilterChip(
                shape = neonChipShape(),
        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
                selected = selected == filter,
                onClick = { onSelected(filter) },
                label = { Text(if (filter == GPU_DRIVER_FILTER_ALL) stringResource(R.string.settings_gpu_driver_filter_all) else filter) }
            )
        }
    }
}

@Composable
private fun GpuDriverHeader(
    title: String,
    onBackClick: () -> Unit,
    searchActive: Boolean,
    filtersActive: Boolean,
    onSearchClick: () -> Unit,
    onFiltersClick: () -> Unit
) {
    ScreenTopBar(
        title = title,
        showNeonDivider = true,
        onBackClick = onBackClick,
        actions = {
            HeaderIconButton(
                selected = searchActive,
                onClick = onSearchClick
            ) {
                Icon(
                    imageVector = Icons.Rounded.Search,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
            }
            HeaderIconButton(
                selected = filtersActive,
                onClick = onFiltersClick
            ) {
                Icon(
                    imageVector = Icons.Rounded.MoreVert,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
            }
        }
    )
}

@Composable
private fun HeaderIconButton(
    selected: Boolean,
    onClick: () -> Unit,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = (Modifier.size(40.dp)).controllerFocus(shape = neonShape(14.dp)),
        shape = neonShape(14.dp),
        color = if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface,
        contentColor = if (selected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurface,
        tonalElevation = 3.dp,
        shadowElevation = 5.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            content()
        }
    }
}

@Composable
private fun ActiveDriverCard(
    selectedDriver: InstalledGpuDriver?,
    selection: GpuDriverSelectionResult,
    loading: Boolean,
    backendRenderer: String,
    isGameScoped: Boolean,
    gameDriverOverride: String?,
    onUseGlobal: () -> Unit,
    onUseSystem: () -> Unit,
    onInstallFromFile: () -> Unit,
    onRemove: () -> Unit
) {
    val canRemove = !isGameScoped || !gameDriverOverride.isNullOrBlank()
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 3.dp,
        shadowElevation = 8.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(
                text = stringResource(R.string.settings_gpu_driver_active_title),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            if (loading) {
                Text(stringResource(R.string.common_loading), style = MaterialTheme.typography.bodyMedium)
            } else {
                DriverStatusText(selection, backendRenderer, isGameScoped)
            }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (isGameScoped) {
                    FilterChip(
                        shape = neonChipShape(),
        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
                        selected = gameDriverOverride == null,
                        onClick = onUseGlobal,
                        enabled = !loading,
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text(stringResource(R.string.settings_gpu_driver_global)) }
                    )
                }
                FilterChip(
                    shape = neonChipShape(),
        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
                    selected = selection.requestedName.isEmpty() && (!isGameScoped || !selection.inheritsGlobal),
                    onClick = onUseSystem,
                    enabled = !loading,
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.settings_gpu_driver_system)) }
                )
                Button(
                    shape = neonButtonShape(),
                    onClick = onInstallFromFile,
                    enabled = !loading,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.FolderZip, contentDescription = null)
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_install_from_file),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
            if (!loading && selectedDriver != null && canRemove) {
                OutlinedButton(
                    shape = neonButtonShape(),
                    onClick = onRemove,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.Delete, contentDescription = null)
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_remove),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
        }
    }
}

/** Requested selection and eligibility only: no native session evidence is supplied here. */
internal data class GpuDriverSelectionText(
    @StringRes val titleRes: Int,
    val titleArgument: String? = null,
    @StringRes val detailRes: Int?
)

internal fun gpuDriverSelectionText(
    selection: GpuDriverSelectionResult,
    backendRenderer: String,
    isGameScoped: Boolean
): GpuDriverSelectionText {
    val missing = selection.decision == GpuDriverSelectionDecision.MISSING_PACKAGE ||
        selection.decision == GpuDriverSelectionDecision.AMBIGUOUS_PACKAGE
    val titleRes = when {
        missing -> R.string.settings_gpu_driver_status_missing_selection
        isGameScoped && selection.inheritsGlobal && selection.requestedName.isEmpty() -> R.string.settings_gpu_driver_status_global_system
        isGameScoped && selection.inheritsGlobal -> R.string.settings_gpu_driver_status_global
        isGameScoped && selection.requestedName.isEmpty() -> R.string.settings_gpu_driver_status_game_system
        selection.requestedName.isEmpty() -> R.string.settings_gpu_driver_status_system
        else -> R.string.settings_gpu_driver_status_active
    }
    val titleArgument = selection.requestedName.takeIf {
        missing || titleRes == R.string.settings_gpu_driver_status_global || titleRes == R.string.settings_gpu_driver_status_active
    }
    val detail = when (selection.decision) {
        GpuDriverSelectionDecision.MISSING_PACKAGE, GpuDriverSelectionDecision.AMBIGUOUS_PACKAGE -> null
        GpuDriverSelectionDecision.INVALID_FILE, GpuDriverSelectionDecision.MISSING_FILE -> R.string.settings_gpu_driver_status_invalid_selection
        GpuDriverSelectionDecision.UNVERIFIED_FILE, GpuDriverSelectionDecision.UNVERIFIED_HARDWARE -> R.string.settings_gpu_driver_status_unverified_selection
        GpuDriverSelectionDecision.INCOMPATIBLE_HARDWARE -> R.string.settings_gpu_driver_status_incompatible_selection
        GpuDriverSelectionDecision.NATIVE_UNSUPPORTED -> R.string.settings_gpu_driver_status_native_unsupported
        GpuDriverSelectionDecision.SYSTEM -> R.string.settings_gpu_driver_status_system_desc
        else -> when {
            backendRenderer != "Vulkan" -> R.string.settings_gpu_driver_status_renderer_desc
            selection.decision == GpuDriverSelectionDecision.UNVERIFIED_NATIVE -> R.string.settings_gpu_driver_status_native_unverified
            else -> R.string.settings_gpu_driver_status_active_desc
        }
    }
    return GpuDriverSelectionText(titleRes, titleArgument, detail)
}

@Composable
internal fun gpuDriverSelectionSummary(
    selection: GpuDriverSelectionResult,
    backendRenderer: String,
    isGameScoped: Boolean
): Pair<String, String> {
    val text = gpuDriverSelectionText(selection, backendRenderer, isGameScoped)
    val title = text.titleArgument?.let { stringResource(text.titleRes, it) } ?: stringResource(text.titleRes)
    return title to (text.detailRes?.let { stringResource(it) } ?: "")
}

@Composable
private fun DriverStatusText(
    selection: GpuDriverSelectionResult,
    backendRenderer: String,
    isGameScoped: Boolean
) {
    val (title, detail) = gpuDriverSelectionSummary(selection, backendRenderer, isGameScoped)
    Text(title, style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold), color = MaterialTheme.colorScheme.onSurface)
    if (detail.isNotEmpty()) Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun InstalledDriverRow(
    driver: InstalledGpuDriver,
    selection: GpuDriverSelectionResult,
    selected: Boolean,
    onSelect: () -> Unit,
    onRemove: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = (Modifier.fillMaxWidth()).gamepadFocusableCard(enabled = selection.requestable, shape = neonShape(18.dp), interactionSource = interactionSource, addFocusTarget = false),
        shape = neonShape(18.dp),
        color = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f)),
        interactionSource = interactionSource,
        enabled = selection.requestable,
        onClick = onSelect
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = driver.name,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.onSecondaryContainer else MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = driver.mainLibrary,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                if (!selection.requestable || selection.compatibility?.state == GpuDriverCompatibilityState.UNVERIFIED ||
                    selection.decision == GpuDriverSelectionDecision.UNVERIFIED_NATIVE) {
                    Text(
                        text = gpuDriverSelectionSummary(selection, "Vulkan", false).second,
                        style = MaterialTheme.typography.bodySmall,
                        color = if (selection.compatibility?.state == GpuDriverCompatibilityState.INCOMPATIBLE)
                            MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            OutlinedButton(shape = neonButtonShape(), onClick = onRemove) {
                Icon(Icons.Rounded.Delete, contentDescription = null)
            }
        }
    }
}

@Composable
private fun RemoteDriverRow(
    driver: RemoteGpuDriver,
    match: GpuDriverMatch,
    canApply: Boolean,
    availability: String?,
    compatibilityTitleRes: Int,
    otherFamilyLabelRes: Int,
    expanded: Boolean,
    installedDriver: InstalledGpuDriver?,
    selected: Boolean,
    downloading: Boolean,
    progress: Float,
    onToggleExpanded: () -> Unit,
    onDownload: () -> Unit,
    onSelect: () -> Unit,
    onRemove: () -> Unit
) {
    val uriHandler = LocalUriHandler.current
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onToggleExpanded
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.Top) {
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = driver.name,
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    DriverBadgeRow(
                        recommended = driver.recommended,
                        downloaded = installedDriver != null,
                        match = match,
                        otherFamilyLabelRes = otherFamilyLabelRes
                    )
                    Text(
                        text = listOf(driver.gpu, driver.variant).filter { it.isNotBlank() }.joinToString(" / "),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Icon(
                    imageVector = if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = stringResource(compatibilityTitleRes),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (availability != null) {
                Text(
                    text = availability,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (installedDriver == null) {
                    Button(
                        shape = neonButtonShape(),
                        onClick = onDownload,
                        enabled = !downloading,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                        Text(
                            text = stringResource(if (canApply) R.string.settings_gpu_driver_download_apply else R.string.settings_updates_download),
                            modifier = Modifier.padding(start = 6.dp)
                        )
                    }
                } else {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        CompactOutlinedActionButton(
                            onClick = onSelect,
                            enabled = !selected && canApply,
                            modifier = Modifier.weight(1f),
                            text = if (selected) {
                                stringResource(R.string.settings_gpu_driver_applied)
                            } else {
                                stringResource(R.string.settings_gpu_driver_apply)
                            }
                        )
                        CompactOutlinedActionButton(
                            onClick = onRemove,
                            modifier = Modifier.weight(1f),
                            icon = { Icon(Icons.Rounded.Delete, contentDescription = null, modifier = Modifier.size(18.dp)) },
                            text = stringResource(R.string.settings_gpu_driver_remove_short)
                        )
                    }
                }
            }
            AnimatedVisibility(
                visible = expanded,
                enter = fadeIn(animationSpec = tween(160)) + expandVertically(animationSpec = tween(200)),
                exit = fadeOut(animationSpec = tween(100)) + shrinkVertically(animationSpec = tween(160))
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (driver.description.isNotBlank()) {
                        Text(
                            text = driver.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Text(
                        text = stringResource(compatibilityTitleRes),
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = if (driver.vendor == GpuDriverVendor.ADRENO) driver.snapdragonSummary() else driver.gpu,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (driver.credits.isNotBlank() || driver.sourceUrl.isNotBlank()) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = driver.credits,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.weight(1f)
                            )
                            if (driver.sourceUrl.isNotBlank()) {
                                CompactOutlinedActionButton(
                                    onClick = { uriHandler.openUri(driver.sourceUrl) },
                                    icon = { Icon(Icons.Rounded.Link, contentDescription = null, modifier = Modifier.size(18.dp)) },
                                    text = stringResource(R.string.settings_gpu_driver_source)
                                )
                            }
                        }
                    }
                }
            }
            if (downloading) {
                LinearProgressIndicator(
                    progress = { progress.coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

@Composable
private fun DeviceCompatibilityCard(
    socName: String,
    gpuName: String,
    @StringRes hintRes: Int
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.55f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = stringResource(
                    R.string.settings_gpu_driver_detected_device,
                    socName,
                    gpuName
                ),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onPrimaryContainer
            )
            Text(
                text = stringResource(hintRes),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.78f)
            )
        }
    }
}

@Composable
private fun CompactOutlinedActionButton(
    onClick: () -> Unit,
    text: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: (@Composable () -> Unit)? = null
) {
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.height(40.dp),
        shape = neonShape(14.dp),
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)),
        colors = ButtonDefaults.outlinedButtonColors(
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.45f)
        )
    ) {
        if (icon != null) {
            icon()
            Spacer(modifier = Modifier.width(6.dp))
        }
        Text(
            text = text,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun DriverBadgeRow(
    recommended: Boolean,
    downloaded: Boolean,
    match: GpuDriverMatch = GpuDriverMatch.UNKNOWN,
    otherFamilyLabelRes: Int = R.string.settings_gpu_driver_other_family
) {
    if (!recommended && !downloaded && match == GpuDriverMatch.UNKNOWN) return
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        if (match == GpuDriverMatch.COMPATIBLE) {
            DriverBadge(
                text = stringResource(
                    if (recommended) R.string.settings_gpu_driver_best_match
                    else R.string.settings_gpu_driver_compatible
                ),
                color = MaterialTheme.colorScheme.primary
            )
        } else if (match == GpuDriverMatch.OTHER_FAMILY) {
            DriverBadge(
                text = stringResource(otherFamilyLabelRes),
                color = MaterialTheme.colorScheme.error
            )
        } else if (recommended) {
            DriverBadge(
                text = stringResource(R.string.settings_gpu_driver_recommended),
                color = MaterialTheme.colorScheme.primary
            )
        }
        if (downloaded) {
            DriverBadge(
                text = stringResource(R.string.settings_gpu_driver_downloaded),
                color = MaterialTheme.colorScheme.tertiary
            )
        }
    }
}

private fun RemoteGpuDriver.recommendationRank(
    profile: SnapdragonGpuProfile?,
    deviceVendor: GpuDriverVendor? = null
): Int =
    when (GpuDriverRecommendations.match(this, profile, deviceVendor)) {
        GpuDriverMatch.COMPATIBLE -> if (recommended) 3 else 2
        GpuDriverMatch.UNKNOWN -> if (recommended) 1 else 0
        GpuDriverMatch.OTHER_FAMILY -> -1
    }

@Composable
private fun RemoteGpuDriver.snapdragonSummary(): String {
    val lines = GpuDriverRecommendations.supportedFamilies(gpu)
        .sortedBy(AdrenoFamily::ordinal)
        .map { family ->
            stringResource(
                when (family) {
                    AdrenoFamily.A6XX -> R.string.settings_gpu_driver_compatibility_a6xx
                    AdrenoFamily.A7XX -> R.string.settings_gpu_driver_compatibility_a7xx
                    AdrenoFamily.A8XX -> R.string.settings_gpu_driver_compatibility_a8xx
                }
            )
        }
    return lines.joinToString(separator = "\n").ifBlank { gpu }
}

@Composable
private fun DriverBadge(text: String, color: androidx.compose.ui.graphics.Color) {
    Surface(
        shape = neonShape(12.dp),
        color = color.copy(alpha = 0.12f)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
            color = color,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 4.dp)
        )
    }
}

private fun RemoteGpuDriver.installedDriverName(): String {
    val rawName = downloadUrl.substringBefore('?').substringAfterLast('/').ifBlank { "$id.zip" }
    val safeName = rawName.replace(Regex("[^A-Za-z0-9._-]"), "_")
    val archiveName = if (safeName.endsWith(".zip", ignoreCase = true)) safeName else "$safeName.zip"
    return archiveName.substringBeforeLast('.').ifBlank { id }
}

@Composable
private fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
        color = MaterialTheme.colorScheme.onBackground,
        modifier = modifier
    )
}

@Composable
private fun LoadingCard(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(text = text, color = MaterialTheme.colorScheme.onSurface)
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun ErrorCard(title: String, message: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.errorContainer
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onErrorContainer
            )
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

private const val GPU_DRIVER_FILTER_ALL = "all"

private fun RemoteGpuDriver.matchesSearch(query: String): Boolean {
    val normalized = query.trim()
    if (normalized.isBlank()) return true
    return listOf(id, name, variant, gpu, description, credits, sourceLabel())
        .any { value -> value.contains(normalized, ignoreCase = true) }
}

private fun RemoteGpuDriver.sourceLabel(): String {
    return when {
        credits.contains("nihui", ignoreCase = true) -> "nihui"
        credits.contains("K11MCH1", ignoreCase = true) -> "K11MCH1"
        else -> credits.substringBefore('/').trim().ifBlank { "Source" }
    }
}
