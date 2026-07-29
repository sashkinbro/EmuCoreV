package com.sbro.emucorev.navigation

import android.annotation.SuppressLint
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.Games
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material.icons.rounded.QueryStats
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.SettingsEthernet
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.shouldUseExpandedShell
import kotlinx.coroutines.launch

enum class PrimaryDestination {
    Home, Setup, Library, GameManager, PlayTime, Achievements, SaveData, Search, Settings, Profile
}

private enum class MobileLeadingAction {
    Drawer,
    Back
}

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun AdaptiveShell(
    selected: PrimaryDestination,
    onNavigateSetup: () -> Unit,
    onNavigateLibrary: () -> Unit,
    onNavigateGameManager: () -> Unit,
    onNavigatePlayTime: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveData: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateProfile: () -> Unit = {},
    onBackClick: (() -> Unit)? = null,
    onInstallFirmware: (() -> Unit)? = null,
    onInstallContent: (() -> Unit)? = null,
    onRefreshLibrary: (() -> Unit)? = null,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val navContent: @Composable () -> Unit = {
        SideNavigation(
            selected = selected,
            onNavigateSetup = onNavigateSetup,
            onNavigateLibrary = onNavigateLibrary,
            onNavigateGameManager = onNavigateGameManager,
            onNavigatePlayTime = onNavigatePlayTime,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateSaveData = onNavigateSaveData,
            onNavigateSearch = onNavigateSearch,
            onNavigateSettings = onNavigateSettings,
            onNavigateProfile = onNavigateProfile,
            onInstallFirmware = onInstallFirmware,
            onInstallContent = onInstallContent,
            onRefreshLibrary = onRefreshLibrary,
            onCloseDrawer = { }
        )
    }
    val configuration = LocalConfiguration.current
    val isWide = configuration.shouldUseExpandedShell()

    if (isWide) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(320.dp)
                    .padding(start = 12.dp, top = 12.dp, bottom = 12.dp)
            ) {
                navContent()
            }
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
            ) {
                content(null)
            }
        }
    } else {
        CompactAdaptiveShell(
            selected = selected,
            onNavigateSetup = onNavigateSetup,
            onNavigateLibrary = onNavigateLibrary,
            onNavigateGameManager = onNavigateGameManager,
            onNavigatePlayTime = onNavigatePlayTime,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateSaveData = onNavigateSaveData,
            onNavigateSearch = onNavigateSearch,
            onNavigateSettings = onNavigateSettings,
            onNavigateProfile = onNavigateProfile,
            onBackClick = onBackClick,
            onInstallFirmware = onInstallFirmware,
            onInstallContent = onInstallContent,
            onRefreshLibrary = onRefreshLibrary,
            content = content
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun CompactAdaptiveShell(
    selected: PrimaryDestination,
    onNavigateSetup: () -> Unit,
    onNavigateLibrary: () -> Unit,
    onNavigateGameManager: () -> Unit,
    onNavigatePlayTime: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveData: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateProfile: () -> Unit,
    onBackClick: (() -> Unit)?,
    onInstallFirmware: (() -> Unit)?,
    onInstallContent: (() -> Unit)?,
    onRefreshLibrary: (() -> Unit)?,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val configuration = LocalConfiguration.current
    val statusPadding = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val isLandscapeCompact = configuration.screenWidthDp > configuration.screenHeightDp
    val drawerWidthFraction = if (isLandscapeCompact) 0.54f else 0.74f
    val drawerTopPadding = maxOf(statusPadding, 32.dp)
    val drawerState = rememberDrawerState(initialValue = DrawerValue.Closed)
    val scope = rememberCoroutineScope()

    val mobileLeadingAction = if (selected != PrimaryDestination.Home && onBackClick != null) {
        MobileLeadingAction.Back
    } else {
        MobileLeadingAction.Drawer
    }
    val leadingActionClick = when (mobileLeadingAction) {
        MobileLeadingAction.Drawer -> rememberDebouncedClick {
            scope.launch {
                if (drawerState.isClosed) drawerState.open() else drawerState.close()
            }
        }
        MobileLeadingAction.Back -> {
            { onBackClick?.invoke(); Unit }
        }
    }

    LaunchedEffect(selected, mobileLeadingAction) {
        if (drawerState.isOpen) drawerState.close()
    }

    BackHandler(enabled = drawerState.isOpen) {
        scope.launch { drawerState.close() }
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        gesturesEnabled = mobileLeadingAction == MobileLeadingAction.Drawer,
        scrimColor = MaterialTheme.colorScheme.scrim.copy(alpha = 0.42f),
        drawerContent = {
            ModalDrawerSheet(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(drawerWidthFraction)
                    .widthIn(min = 292.dp, max = 360.dp),
                drawerShape = RoundedCornerShape(topEnd = 30.dp, bottomEnd = 30.dp),
                drawerContainerColor = MaterialTheme.colorScheme.surface,
                drawerContentColor = MaterialTheme.colorScheme.onSurface,
                drawerTonalElevation = 6.dp,
                windowInsets = WindowInsets(0, 0, 0, 0)
            ) {
                SideNavigation(
                    selected = selected,
                    onNavigateSetup = onNavigateSetup,
                    onNavigateLibrary = onNavigateLibrary,
                    onNavigateGameManager = onNavigateGameManager,
                    onNavigatePlayTime = onNavigatePlayTime,
                    onNavigateAchievements = onNavigateAchievements,
                    onNavigateSaveData = onNavigateSaveData,
                    onNavigateSearch = onNavigateSearch,
                    onNavigateSettings = onNavigateSettings,
                    onNavigateProfile = onNavigateProfile,
                    onInstallFirmware = onInstallFirmware,
                    onInstallContent = onInstallContent,
                    onRefreshLibrary = onRefreshLibrary,
                    wrapInSurface = false,
                    topInset = drawerTopPadding,
                    onCloseDrawer = { scope.launch { drawerState.snapTo(DrawerValue.Closed) } }
                )
            }
        }
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            content(
                if (mobileLeadingAction == MobileLeadingAction.Drawer) leadingActionClick else null
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SideNavigation(
    selected: PrimaryDestination,
    onNavigateSetup: () -> Unit,
    onNavigateLibrary: () -> Unit,
    onNavigateGameManager: () -> Unit,
    onNavigatePlayTime: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveData: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateProfile: () -> Unit,
    onInstallFirmware: (() -> Unit)?,
    onInstallContent: (() -> Unit)?,
    onRefreshLibrary: (() -> Unit)?,
    wrapInSurface: Boolean = true,
    topInset: Dp = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
    onCloseDrawer: () -> Unit
) {
    val context = LocalContext.current
    val proManager = remember(context) { ProPurchaseManager.getInstance(context) }
    val proState by proManager.state.collectAsState()
    val drawerInset = 18.dp
    val drawerBottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val drawerBottomPadding = drawerInset + if (wrapInSurface) {
        drawerBottomInset
    } else {
        maxOf(drawerBottomInset, 18.dp)
    }

    val navigateLibrary = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateLibrary()
    }
    val navigateSetup = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSetup()
    }
    val navigateSearch = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSearch()
    }
    val navigateGameManager = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateGameManager()
    }
    val navigatePlayTime = rememberDebouncedClick {
        onCloseDrawer()
        onNavigatePlayTime()
    }
    val navigateAchievements = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateAchievements()
    }
    val navigateSaveData = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSaveData()
    }
    val navigateSettings = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSettings()
    }
    val navigateProfile = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateProfile()
    }
    val installFirmware = onInstallFirmware?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }
    val installContent = onInstallContent?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }
    val refreshLibrary = onRefreshLibrary?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }
    val hasSetupActions = installFirmware != null || installContent != null
    val hasLibraryActions = refreshLibrary != null
    val hasToolsActions = true

    val content: @Composable () -> Unit = {
        Column(
            modifier = Modifier
                .fillMaxHeight()
                .verticalScroll(rememberScrollState())
                .padding(
                    start = drawerInset,
                    end = drawerInset,
                    top = drawerInset + topInset + 4.dp,
                    bottom = drawerBottomPadding
                ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.padding(start = 6.dp, end = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Image(
                    painter = painterResource(
                        if (proState.isProUnlocked) {
                            R.drawable.ic_drawer_app_pro
                        } else {
                            R.drawable.ic_drawer_app
                        }
                    ),
                    contentDescription = null,
                    modifier = Modifier
                        .size(52.dp)
                        .clip(CircleShape)
                )
                Text(
                    text = stringResource(R.string.app_name_emucorev),
                    style = MaterialTheme.typography.headlineSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 14.dp)
                )
            }
            ShellItem(
                icon = Icons.Rounded.Games,
                label = stringResource(R.string.nav_library),
                selected = selected == PrimaryDestination.Library,
                onClick = navigateLibrary
            )
            ShellItem(
                icon = Icons.Rounded.SettingsEthernet,
                label = stringResource(R.string.nav_setup),
                selected = selected == PrimaryDestination.Setup,
                onClick = navigateSetup
            )
            ShellItem(
                icon = Icons.Rounded.Search,
                label = stringResource(R.string.nav_catalog),
                selected = selected == PrimaryDestination.Search,
                onClick = navigateSearch
            )
            ShellItem(
                icon = Icons.Rounded.Settings,
                label = stringResource(R.string.nav_settings),
                selected = selected == PrimaryDestination.Settings,
                onClick = navigateSettings
            )

            if (hasSetupActions) {
                HorizontalDivider(
                    thickness = 1.dp,
                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                )
                Text(
                    text = stringResource(R.string.shell_setup_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                if (installFirmware != null) {
                    ShellAction(
                        icon = Icons.Rounded.SystemUpdateAlt,
                        label = stringResource(R.string.shell_install_firmware),
                        onClick = installFirmware
                    )
                }
                if (installContent != null) {
                    ShellAction(
                        icon = Icons.Rounded.Inventory2,
                        label = stringResource(R.string.shell_install_content),
                        onClick = installContent
                    )
                }
            }

            if (hasLibraryActions) {
                HorizontalDivider(
                    thickness = 1.dp,
                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                )
                Text(
                    text = stringResource(R.string.shell_library_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                ShellAction(
                    icon = Icons.Rounded.Refresh,
                    label = stringResource(R.string.library_refresh),
                    onClick = refreshLibrary
                )
            }

            if (hasToolsActions) {
                HorizontalDivider(
                    thickness = 1.dp,
                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                )
                Text(
                    text = stringResource(R.string.shell_tools_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                ShellItem(
                    icon = Icons.Rounded.Tune,
                    label = stringResource(R.string.nav_game_manager),
                    selected = selected == PrimaryDestination.GameManager,
                    onClick = navigateGameManager
                )
                ShellItem(
                    icon = Icons.Rounded.QueryStats,
                    label = stringResource(R.string.nav_play_time),
                    selected = selected == PrimaryDestination.PlayTime,
                    onClick = navigatePlayTime
                )
                ShellItem(
                    icon = Icons.Rounded.EmojiEvents,
                    label = stringResource(R.string.nav_achievements),
                    selected = selected == PrimaryDestination.Achievements,
                    onClick = navigateAchievements
                )
                ShellItem(
                    icon = Icons.Rounded.Save,
                    label = stringResource(R.string.nav_save_manager),
                    selected = selected == PrimaryDestination.SaveData,
                    onClick = navigateSaveData
                )
            }

            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            ShellItem(
                icon = Icons.Rounded.AccountCircle,
                label = stringResource(R.string.nav_profile),
                selected = selected == PrimaryDestination.Profile,
                onClick = navigateProfile
            )
        }
    }

    if (wrapInSurface) {
        Surface(
            modifier = Modifier.fillMaxHeight(),
            shape = RoundedCornerShape(30.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 2.dp
        ) {
            content()
        }
    } else {
        content()
    }
}

@Composable
private fun ShellAction(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .focusable(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(18.dp)
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(start = 12.dp)
            )
        }
    }
}

@Composable
private fun ShellItem(
    icon: ImageVector,
    label: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .focusable(),
        shape = RoundedCornerShape(18.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.96f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f)
        },
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(20.dp)
            )
            Text(
                text = label,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(start = 12.dp)
            )
        }
    }
}
