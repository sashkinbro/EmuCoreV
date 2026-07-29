package com.sbro.emucorev.navigation

import android.net.Uri
import android.widget.Toast
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.EaseIn
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
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
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.sbro.emucorev.R
import com.sbro.emucorev.core.DocumentPathResolver
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.ui.achievements.AchievementsScreen
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.ui.catalog.CatalogScreen
import com.sbro.emucorev.ui.detail.GameDetailScreen
import com.sbro.emucorev.ui.gamemanager.GameManagerScreen
import com.sbro.emucorev.ui.home.HomeScreen
import com.sbro.emucorev.ui.library.LibraryScreen
import com.sbro.emucorev.ui.onboarding.OnboardingScreen
import com.sbro.emucorev.ui.playtime.PlayTimeScreen
import com.sbro.emucorev.ui.profile.ProfileScreen
import com.sbro.emucorev.ui.pro.ProWelcomeDialog
import com.sbro.emucorev.ui.saves.SaveDataScreen
import com.sbro.emucorev.ui.settings.AppLanguageScreen
import com.sbro.emucorev.ui.settings.GpuDriverScreen
import com.sbro.emucorev.ui.settings.SettingsScreen
import com.sbro.emucorev.ui.settings.SettingsTab
import com.sbro.emucorev.ui.settings.SettingsViewModel
import com.sbro.emucorev.ui.settings.VitaLanguageScreen
import com.sbro.emucorev.ui.settings.settingsTabFromRoute
import com.sbro.emucorev.ui.setup.InstallGameChoiceDialog
import com.sbro.emucorev.ui.setup.SetupInstallDialog
import com.sbro.emucorev.ui.setup.SetupInstallViewModel
import com.sbro.emucorev.ui.setup.SetupScreen

private const val ROUTE_ONBOARDING = "onboarding"
private const val ROUTE_HOME = "home"
private const val ROUTE_SETUP = "setup"
private const val ROUTE_LIBRARY = "library"
private const val ROUTE_CATALOG = "catalog"
private const val ROUTE_GAME_MANAGER = "game-manager"
private const val ROUTE_GAME_MANAGER_WITH_TITLE = "game-manager/{titleId}"
private const val ROUTE_PLAY_TIME = "play-time"
private const val ROUTE_PLAY_TIME_WITH_TITLE = "play-time/{titleId}"
private const val ROUTE_ACHIEVEMENTS = "achievements"
private const val ROUTE_ACHIEVEMENTS_WITH_TITLE = "achievements/{titleId}"
private const val ROUTE_SAVE_MANAGER = "save-manager"
private const val ROUTE_SAVE_MANAGER_WITH_TITLE = "save-manager/{titleId}"
private const val ROUTE_SETTINGS = "settings"
private const val ROUTE_PROFILE = "profile"
private const val ROUTE_SETTINGS_WITH_TAB = "settings/{tab}"
private const val ROUTE_APP_LANGUAGE = "app-language"
private const val ROUTE_VITA_LANGUAGE = "vita-language"
private const val ROUTE_GPU_DRIVER = "gpu-driver"
private const val ROUTE_GPU_DRIVER_WITH_TITLE = "gpu-driver/{titleId}"
private const val ROUTE_DETAIL_PREFIX = "detail"
private const val ROUTE_CATALOG_DETAIL_PREFIX = "catalog-detail"

private fun settingsRoute(tab: SettingsTab = SettingsTab.General): String = "$ROUTE_SETTINGS/${tab.name.lowercase()}"
private fun saveManagerRoute(titleId: String? = null): String =
    titleId?.takeIf(String::isNotBlank)?.let { "$ROUTE_SAVE_MANAGER/$it" } ?: ROUTE_SAVE_MANAGER
private fun gameManagerRoute(titleId: String? = null): String =
    titleId?.takeIf(String::isNotBlank)?.let { "$ROUTE_GAME_MANAGER/$it" } ?: ROUTE_GAME_MANAGER
private fun playTimeRoute(titleId: String? = null): String =
    titleId?.takeIf(String::isNotBlank)?.let { "$ROUTE_PLAY_TIME/$it" } ?: ROUTE_PLAY_TIME
private fun achievementsRoute(titleId: String? = null): String =
    titleId?.takeIf(String::isNotBlank)?.let { "$ROUTE_ACHIEVEMENTS/${Uri.encode(it)}" } ?: ROUTE_ACHIEVEMENTS
private fun gpuDriverRoute(titleId: String? = null): String =
    titleId?.takeIf(String::isNotBlank)?.let { "$ROUTE_GPU_DRIVER/${Uri.encode(it)}" } ?: ROUTE_GPU_DRIVER

@Composable
fun AppNavigation(navController: NavHostController = rememberNavController()) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val installViewModel: SetupInstallViewModel = viewModel()
    val settingsViewModel: SettingsViewModel = viewModel()
    val installUiState by installViewModel.uiState.collectAsState()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = currentBackStackEntry?.destination?.route
    var firmwareInstalled by remember(context) { mutableStateOf(EmulatorStorage.hasInstalledFirmware(context)) }
    var firmwareUpdateInstalled by remember(context) { mutableStateOf(EmulatorStorage.hasInstalledFirmwareUpdate(context)) }
    val startDestination = if (preferences.onboardingCompleted) ROUTE_LIBRARY else ROUTE_ONBOARDING
    stringResource(R.string.core_install_firmware_failed)
    val unsupportedFirmware = stringResource(R.string.core_install_firmware_unsupported)
    stringResource(R.string.core_install_content_failed)
    val unsupportedContent = stringResource(R.string.core_install_content_unsupported)
    stringResource(R.string.core_install_pkg_failed)
    val unsupportedPkg = stringResource(R.string.core_install_pkg_unsupported)
    val gameLaunchFailed = stringResource(R.string.game_launch_failed)
    val launchRequiresFirmwareMessage = stringResource(R.string.game_launch_requires_firmware)
    val launchRequiresFirmwareUpdateMessage = stringResource(R.string.game_launch_requires_firmware_update)
    var pendingPkgZrif by rememberSaveable { mutableStateOf("") }
    var pendingContentRepair by rememberSaveable { mutableStateOf(false) }
    var installChoiceZrif by rememberSaveable { mutableStateOf("") }
    var showInstallChoiceDialog by rememberSaveable { mutableStateOf(false) }
    var showWelcomeDialog by rememberSaveable {
        mutableStateOf(preferences.onboardingCompleted && !preferences.welcomeDialogShown)
    }

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
        val repairArchive = pendingContentRepair
        pendingContentRepair = false
        uri ?: return@rememberLauncherForActivityResult
        val fileName = DocumentPathResolver.getDisplayName(context, uri.toString())
        val extension = fileName.substringAfterLast('.', "").lowercase()
        val supported = extension in setOf("vpk", "zip")
        if (!supported) {
            Toast.makeText(context, unsupportedContent, Toast.LENGTH_SHORT).show()
        } else {
            installViewModel.installContent(uri.toString(), repairArchive = repairArchive)
        }
    }

    val licensePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        installViewModel.installLicense(uri.toString())
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
    val openContentInstall = {
        pendingContentRepair = false
        contentPicker.launch(arrayOf("*/*"))
    }
    val openRepairContentInstall = {
        pendingContentRepair = true
        contentPicker.launch(arrayOf("*/*"))
    }
    val openLicenseInstall = { licensePicker.launch(arrayOf("*/*")) }
    val openPkgInstall: (String) -> Unit = { zrif ->
        pendingPkgZrif = zrif
        pkgPicker.launch(arrayOf("*/*"))
    }
    val openInstallChoiceDialog = {
        showInstallChoiceDialog = true
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
    val navigateGameManager: (String?) -> Unit = { titleId ->
        navController.navigate(gameManagerRoute(titleId)) { launchSingleTop = true }
    }
    val navigatePlayTime: (String?) -> Unit = { titleId ->
        navController.navigate(playTimeRoute(titleId)) { launchSingleTop = true }
    }
    val navigateAchievements: (String?) -> Unit = { titleId ->
        navController.navigate(achievementsRoute(titleId)) { launchSingleTop = true }
    }
    val navigateAchievementsRoot = { navigateAchievements(null) }
    val navigateProfile = {
        navController.navigate(ROUTE_PROFILE) { launchSingleTop = true }
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
                .background(MaterialTheme.colorScheme.background),
            enterTransition = { appScreenEnterTransition() },
            exitTransition = { appScreenExitTransition() },
            popEnterTransition = { appScreenPopEnterTransition() },
            popExitTransition = { appScreenPopExitTransition() },
            sizeTransform = { SizeTransform(clip = false) }
        ) {
            composable(
                ROUTE_ONBOARDING
            ) {
                OnboardingScreen(
                    firmwareInstalled = firmwareInstalled,
                    firmwareUpdateInstalled = firmwareUpdateInstalled,
                    onInstallFirmware = openFirmwareInstall,
                    onInstallFirmwareUpdate = openFirmwareInstall,
                    onInstallDownloadedFirmware = installViewModel::installFirmware,
                    onComplete = {
                        showWelcomeDialog = !preferences.welcomeDialogShown
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
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
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
                        onOpenGameManager = { titleId -> navigateGameManager(titleId) },
                        onOpenPlayTime = { titleId -> navigatePlayTime(titleId) },
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
                    onNavigateSetup = { },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) {
                    SetupScreen(
                        vitaRootPath = EmulatorStorage.vitaRoot(context).absolutePath,
                        onBackClick = navigateHome,
                        onInstallContent = openContentInstall,
                        onRepairContent = openRepairContentInstall,
                        onInstallLicense = openLicenseInstall,
                        onInstallPkg = openPkgInstall
                    )
                }
            }
            composable(
                ROUTE_LIBRARY
            ) {
                AdaptiveShell(
                    selected = PrimaryDestination.Library,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = { },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) { openDrawer ->
                    LibraryScreen(
                        onLaunchGame = launchInstalledGame,
                        onOpenSaveManager = { titleId ->
                            navController.navigate(saveManagerRoute(titleId)) { launchSingleTop = true }
                        },
                        onOpenGameManager = { titleId -> navigateGameManager(titleId) },
                        onOpenPlayTime = { titleId -> navigatePlayTime(titleId) },
                        onOpenAchievements = { titleId -> navigateAchievements(titleId) },
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
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = { },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
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
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = { },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) {
                    SettingsScreen(
                        initialTab = SettingsTab.General,
                        onBackClick = navigateHome,
                        onOpenLanguageSettings = {
                            navController.navigate(ROUTE_APP_LANGUAGE) { launchSingleTop = true }
                        },
                        onOpenVitaLanguageSettings = {
                            navController.navigate(ROUTE_VITA_LANGUAGE) { launchSingleTop = true }
                        },
                        onOpenGpuDriverSettings = {
                            navController.navigate(ROUTE_GPU_DRIVER) { launchSingleTop = true }
                        },
                        viewModel = settingsViewModel
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
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = { },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) {
                    SettingsScreen(
                        initialTab = settingsTabFromRoute(entry.arguments?.getString("tab")),
                        onBackClick = navigateHome,
                        onOpenLanguageSettings = {
                            navController.navigate(ROUTE_APP_LANGUAGE) { launchSingleTop = true }
                        },
                        onOpenVitaLanguageSettings = {
                            navController.navigate(ROUTE_VITA_LANGUAGE) { launchSingleTop = true }
                        },
                        onOpenGpuDriverSettings = {
                            navController.navigate(ROUTE_GPU_DRIVER) { launchSingleTop = true }
                        },
                        viewModel = settingsViewModel
                    )
                }
            }
            composable(ROUTE_APP_LANGUAGE) {
                AppLanguageScreen(
                    onBackClick = { navController.popBackStack() },
                    viewModel = settingsViewModel
                )
            }
            composable(ROUTE_VITA_LANGUAGE) {
                VitaLanguageScreen(
                    onBackClick = { navController.popBackStack() },
                    viewModel = settingsViewModel
                )
            }
            composable(ROUTE_GPU_DRIVER) {
                GpuDriverScreen(
                    onBackClick = { navController.popBackStack() },
                    viewModel = settingsViewModel
                )
            }
            composable(ROUTE_GPU_DRIVER_WITH_TITLE) { entry ->
                GpuDriverScreen(
                    onBackClick = { navController.popBackStack() },
                    targetTitleId = entry.arguments?.getString("titleId"),
                    viewModel = settingsViewModel
                )
            }
            composable(ROUTE_SAVE_MANAGER) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.SaveData,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = { },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) {
                    SaveDataScreen(
                        focusTitleId = null,
                        onBackClick = navigateHome
                    )
                }
            }
            composable(ROUTE_SAVE_MANAGER_WITH_TITLE) { entry ->
                SaveDataScreen(
                    focusTitleId = entry.arguments?.getString("titleId"),
                    onBackClick = { navController.popBackStack() }
                )
            }
            composable(ROUTE_PLAY_TIME) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.PlayTime,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) { openDrawer ->
                    PlayTimeScreen(
                        onMenuClick = openDrawer,
                        onBackClick = navigateHome
                    )
                }
            }
            composable(ROUTE_ACHIEVEMENTS) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Achievements,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = { },
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) { openDrawer ->
                    AchievementsScreen(
                        onMenuClick = openDrawer,
                        onBackClick = navigateHome
                    )
                }
            }
            composable(ROUTE_ACHIEVEMENTS_WITH_TITLE) { entry ->
                AchievementsScreen(
                    focusTitleId = entry.arguments?.getString("titleId"),
                    onMenuClick = null,
                    onBackClick = { navController.popBackStack() }
                )
            }
            composable(ROUTE_PLAY_TIME_WITH_TITLE) { entry ->
                PlayTimeScreen(
                    focusTitleId = entry.arguments?.getString("titleId"),
                    onMenuClick = null,
                    onBackClick = { navController.popBackStack() }
                )
            }
            composable(ROUTE_PROFILE) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.Profile,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { navigateGameManager(null) },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = { },
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) { openDrawer ->
                    ProfileScreen(
                        onBackClick = navigateHome,
                        onMenuClick = openDrawer,
                        onGameClick = { igdbId -> navController.navigate("$ROUTE_CATALOG_DETAIL_PREFIX/$igdbId") }
                    )
                }
            }
            composable(ROUTE_GAME_MANAGER) {
                val navigateHome = {
                    navController.navigate(ROUTE_LIBRARY) {
                        launchSingleTop = true
                        popUpTo(ROUTE_LIBRARY) { inclusive = false }
                    }
                }
                AdaptiveShell(
                    selected = PrimaryDestination.GameManager,
                    onNavigateSetup = {
                        navController.navigate(ROUTE_SETUP) { launchSingleTop = true }
                    },
                    onNavigateLibrary = {
                        navController.navigate(ROUTE_LIBRARY) { launchSingleTop = true }
                    },
                    onNavigateGameManager = { },
                    onNavigatePlayTime = { navigatePlayTime(null) },
                    onNavigateAchievements = navigateAchievementsRoot,
                    onNavigateSaveData = {
                        navController.navigate(saveManagerRoute()) { launchSingleTop = true }
                    },
                    onNavigateSearch = {
                        navController.navigate(ROUTE_CATALOG) { launchSingleTop = true }
                    },
                    onNavigateSettings = {
                        navController.navigate(settingsRoute()) { launchSingleTop = true }
                    },
                    onNavigateProfile = navigateProfile,
                    onBackClick = navigateHome,
                    onInstallFirmware = null,
                    onInstallContent = openInstallChoiceDialog
                ) { openDrawer ->
                    GameManagerScreen(
                        onMenuClick = openDrawer,
                        onBackClick = navigateHome,
                        onOpenGpuDriverManager = {
                            navController.navigate(gpuDriverRoute(it)) { launchSingleTop = true }
                        }
                    )
                }
            }
            composable(ROUTE_GAME_MANAGER_WITH_TITLE) { entry ->
                GameManagerScreen(
                    initialTitleId = entry.arguments?.getString("titleId"),
                    onMenuClick = null,
                    onBackClick = { navController.popBackStack() },
                    onOpenGpuDriverManager = {
                        navController.navigate(gpuDriverRoute(it)) { launchSingleTop = true }
                    }
                )
            }
            composable(
                "$ROUTE_DETAIL_PREFIX/{titleId}"
            ) { entry ->
                GameDetailScreen(
                    titleId = entry.arguments?.getString("titleId"),
                    igdbId = null,
                    onBack = { navController.popBackStack() },
                    onOpenSaveManager = { titleId ->
                        navController.navigate(saveManagerRoute(titleId)) { launchSingleTop = true }
                    }
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

    if (showInstallChoiceDialog) {
        InstallGameChoiceDialog(
            zrif = installChoiceZrif,
            onZrifChange = { installChoiceZrif = it },
            onDismiss = {
                showInstallChoiceDialog = false
                installChoiceZrif = ""
            },
            onInstallArchive = {
                showInstallChoiceDialog = false
                installChoiceZrif = ""
                openContentInstall()
            },
            onRepairArchive = {
                showInstallChoiceDialog = false
                installChoiceZrif = ""
                openRepairContentInstall()
            },
            onInstallLicense = openLicenseInstall,
            onInstallPkg = { zrif ->
                showInstallChoiceDialog = false
                installChoiceZrif = ""
                openPkgInstall(zrif)
            }
        )
    }

    SetupInstallDialog(
        uiState = installUiState,
        onDismiss = installViewModel::dismissDialog
    )

    if (
        showWelcomeDialog &&
        preferences.onboardingCompleted &&
        (currentRoute == ROUTE_LIBRARY || currentRoute == ROUTE_HOME)
    ) {
        ProWelcomeDialog(
            onContinue = {
                preferences.welcomeDialogShown = true
                showWelcomeDialog = false
            }
        )
    }

}

private fun appScreenEnterTransition(): EnterTransition {
    return appScreenPopEnterTransition()
}

private fun appScreenExitTransition(): ExitTransition {
    return ExitTransition.None
}

private fun appScreenPopEnterTransition(): EnterTransition {
    return fadeIn(animationSpec = tween(durationMillis = 260, delayMillis = 70, easing = EaseOut)) +
        scaleIn(initialScale = 0.96f, animationSpec = tween(260, delayMillis = 70, easing = EaseOut))
}

private fun appScreenPopExitTransition(): ExitTransition {
    return fadeOut(animationSpec = tween(durationMillis = 110, easing = EaseIn)) +
        scaleOut(targetScale = 1.0f, animationSpec = tween(110, easing = EaseIn))
}
