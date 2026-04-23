package com.sbro.emucorev.navigation

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.sbro.emucorev.R
import com.sbro.emucorev.core.DocumentPathResolver
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.ui.catalog.CatalogScreen
import com.sbro.emucorev.ui.detail.GameDetailScreen
import com.sbro.emucorev.ui.home.HomeScreen
import com.sbro.emucorev.ui.library.LibraryScreen
import com.sbro.emucorev.ui.onboarding.OnboardingScreen
import com.sbro.emucorev.ui.settings.SettingsScreen
import com.sbro.emucorev.ui.settings.SettingsTab
import com.sbro.emucorev.ui.settings.settingsTabFromRoute
import com.sbro.emucorev.ui.setup.SetupInstallDialog
import com.sbro.emucorev.ui.setup.SetupInstallViewModel
import com.sbro.emucorev.ui.setup.SetupScreen

private const val ROUTE_ONBOARDING = "onboarding"
private const val ROUTE_HOME = "home"
private const val ROUTE_SETUP = "setup"
private const val ROUTE_LIBRARY = "library"
private const val ROUTE_CATALOG = "catalog"
private const val ROUTE_SETTINGS = "settings"
private const val ROUTE_SETTINGS_WITH_TAB = "settings/{tab}"
private const val ROUTE_DETAIL_PREFIX = "detail"
private const val ROUTE_CATALOG_DETAIL_PREFIX = "catalog-detail"

private fun settingsRoute(tab: SettingsTab = SettingsTab.General): String = "$ROUTE_SETTINGS/${tab.name.lowercase()}"

@Composable
fun AppNavigation(navController: NavHostController = rememberNavController()) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val installViewModel: SetupInstallViewModel = viewModel()
    val installUiState by installViewModel.uiState.collectAsState()
    var firmwareInstalled by remember(context) { mutableStateOf(EmulatorStorage.hasInstalledFirmware(context)) }
    var firmwareUpdateInstalled by remember(context) { mutableStateOf(EmulatorStorage.hasInstalledFirmwareUpdate(context)) }
    val startDestination = if (preferences.onboardingCompleted) ROUTE_LIBRARY else ROUTE_ONBOARDING
    val firmwareLaunchFailed = stringResource(R.string.core_install_firmware_failed)
    val unsupportedFirmware = stringResource(R.string.core_install_firmware_unsupported)
    val contentLaunchFailed = stringResource(R.string.core_install_content_failed)
    val unsupportedContent = stringResource(R.string.core_install_content_unsupported)
    val pkgLaunchFailed = stringResource(R.string.core_install_pkg_failed)
    val unsupportedPkg = stringResource(R.string.core_install_pkg_unsupported)
    val gameLaunchFailed = stringResource(R.string.game_launch_failed)
    val launchRequiresFirmwareMessage = stringResource(R.string.game_launch_requires_firmware)
    val launchRequiresFirmwareUpdateMessage = stringResource(R.string.game_launch_requires_firmware_update)
    var pendingPkgZrif by rememberSaveable { mutableStateOf("") }

    val firmwarePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        val fileName = DocumentPathResolver.getDisplayName(context, uri.toString())
        val extension = fileName.substringAfterLast('.', "").lowercase()
        if (extension != "pup") {
            Toast.makeText(context, unsupportedFirmware, Toast.LENGTH_SHORT).show()
        } else {
            installViewModel.installFirmware(uri.toString())
        }
    }

    val contentPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        val fileName = DocumentPathResolver.getDisplayName(context, uri.toString())
        val extension = fileName.substringAfterLast('.', "").lowercase()
        val supported = extension in setOf("vpk", "zip", "rif", "bin")
        if (!supported) {
            Toast.makeText(context, unsupportedContent, Toast.LENGTH_SHORT).show()
        } else {
            installViewModel.installContent(uri.toString())
        }
    }

    val pkgPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        val fileName = DocumentPathResolver.getDisplayName(context, uri.toString())
        val extension = fileName.substringAfterLast('.', "").lowercase()
        val zrif = pendingPkgZrif.trim()
        pendingPkgZrif = ""
        if (extension != "pkg") {
            Toast.makeText(context, unsupportedPkg, Toast.LENGTH_SHORT).show()
        } else {
            installViewModel.installPkg(uri.toString(), zrif)
        }
    }

    val openFirmwareInstall = { firmwarePicker.launch(arrayOf("*/*")) }
    val openContentInstall = { contentPicker.launch(arrayOf("*/*")) }
    val openPkgInstall: (String) -> Unit = { zrif ->
        pendingPkgZrif = zrif
        pkgPicker.launch(arrayOf("*/*"))
    }

    LaunchedEffect(context) {
        InstallStateBus.events.collect {
            firmwareInstalled = EmulatorStorage.hasInstalledFirmware(context)
            firmwareUpdateInstalled = EmulatorStorage.hasInstalledFirmwareUpdate(context)
        }
    }

    val launchInstalledGame: (String) -> Unit = { titleId ->
        when (VitaLaunchBridge.launchInstalledTitle(context, titleId)) {
            VitaLaunchBridge.LaunchResult.Success -> Unit
            VitaLaunchBridge.LaunchResult.MissingFirmware -> Toast.makeText(context, launchRequiresFirmwareMessage, Toast.LENGTH_SHORT).show()
            VitaLaunchBridge.LaunchResult.MissingFirmwareUpdate -> Toast.makeText(context, launchRequiresFirmwareUpdateMessage, Toast.LENGTH_SHORT).show()
            VitaLaunchBridge.LaunchResult.Failure -> Toast.makeText(context, gameLaunchFailed, Toast.LENGTH_SHORT).show()
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        NavHost(
            navController = navController,
            startDestination = startDestination,
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            composable(
                ROUTE_ONBOARDING
            ) {
                OnboardingScreen(
                    firmwareInstalled = firmwareInstalled,
                    firmwareUpdateInstalled = firmwareUpdateInstalled,
                    onInstallFirmware = openFirmwareInstall,
                    onInstallFirmwareUpdate = openFirmwareInstall,
                    onComplete = {
                        navController.navigate(ROUTE_LIBRARY) {
                            popUpTo(ROUTE_ONBOARDING) { inclusive = true }
                            launchSingleTop = true
                        }
                    }
                )
            }
            composable(
                ROUTE_HOME
            ) {
                AdaptiveShell(
                    selected = PrimaryDestination.Home,
                    onNavigateHome = { },
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onOpenManageFolders = {
                        navController.navigate(settingsRoute(SettingsTab.Storage)) { launchSingleTop = true }
                    },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) { openDrawer ->
                    HomeScreen(
                        onOpenSetup = {
                            navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                        },
                        onOpenLibrary = {
                            navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                        },
                        onOpenCatalog = {
                            navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                        },
                        onLaunchGame = launchInstalledGame,
                        onMenuClick = openDrawer
                    )
                }
            }
            composable(
                ROUTE_SETUP
            ) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Setup,
                    onNavigateHome = navigateHome,
                    onNavigateSetup = { },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onBackClick = navigateHome,
                    onOpenManageFolders = {
                        navController.navigate(settingsRoute(SettingsTab.Storage)) { launchSingleTop = true }
                    },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) {
                    SetupScreen(
                        packagesFolderLabel = preferences.packagesFolderDisplayName(context),
                        vitaRootPath = EmulatorStorage.vitaRoot(context).absolutePath,
                        onBackClick = navigateHome,
                        onInstallFirmware = openFirmwareInstall,
                        onInstallContent = openContentInstall,
                        onInstallPkg = openPkgInstall
                    )
                }
            }
            composable(
                ROUTE_LIBRARY
            ) {
                AdaptiveShell(
                    selected = PrimaryDestination.Library,
                    onNavigateHome = {
                        navController.navigate(ROUTE_LIBRARY) {
                            launchSingleTop = true
                            popUpTo(ROUTE_LIBRARY) { inclusive = false }
                        }
                    },
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = { },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onOpenManageFolders = {
                        navController.navigate(settingsRoute(SettingsTab.Storage)) { launchSingleTop = true }
                    },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) { openDrawer ->
                    LibraryScreen(
                        onLaunchGame = launchInstalledGame,
                        onMenuClick = openDrawer
                    )
                }
            }
            composable(
                ROUTE_CATALOG
            ) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Search,
                    onNavigateHome = navigateHome,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateSearch = { },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onBackClick = navigateHome,
                    onOpenManageFolders = {
                        navController.navigate(settingsRoute(SettingsTab.Storage)) { launchSingleTop = true }
                    },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) {
                    CatalogScreen(
                        onGameClick = { igdbId -> navController.navigate("$ROUTE_CATALOG_DETAIL_PREFIX/$igdbId") },
                        onBackClick = navigateHome
                    )
                }
            }
            composable(ROUTE_SETTINGS) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Settings,
                    onNavigateHome = navigateHome,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = { },
                    onBackClick = navigateHome,
                    onOpenManageFolders = { },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) {
                    SettingsScreen(
                        initialTab = SettingsTab.General,
                        onBackClick = navigateHome
                    )
                }
            }
            composable(ROUTE_SETTINGS_WITH_TAB) { entry ->
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Settings,
                    onNavigateHome = navigateHome,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = { },
                    onBackClick = navigateHome,
                    onOpenManageFolders = { },
                    onInstallFirmware = null,
                    onInstallContent = openContentInstall
                ) {
                    SettingsScreen(
                        initialTab = settingsTabFromRoute(entry.arguments?.getString("tab")),
                        onBackClick = navigateHome
                    )
                }
            }
            composable(
                "$ROUTE_DETAIL_PREFIX/{titleId}"
            ) { entry ->
                GameDetailScreen(
                    titleId = entry.arguments?.getString("titleId"),
                    igdbId = null,
                    onBack = { navController.popBackStack() }
                )
            }
            composable(
                "$ROUTE_CATALOG_DETAIL_PREFIX/{igdbId}"
            ) { entry ->
                GameDetailScreen(
                    titleId = null,
                    igdbId = entry.arguments?.getString("igdbId")?.toLongOrNull(),
                    onBack = { navController.popBackStack() }
                )
            }
        }
    }

    SetupInstallDialog(
        uiState = installUiState,
        onDismiss = installViewModel::dismissDialog
    )
}
