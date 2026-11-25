package com.sbro.emucorev.ui.theme

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.theme.InstalledThemePackage
import com.sbro.emucorev.data.theme.ThemePackageRepository

/** The installed theme package that is currently applied, or null when none is. */
@Composable
fun rememberAppliedThemePackage(preferences: AppPreferences): InstalledThemePackage? {
    val context = LocalContext.current
    val repository = remember(context) { ThemePackageRepository.getInstance(context.applicationContext) }
    val packageId by preferences.themePackageIdFlow.collectAsState(initial = preferences.themePackageId)
    val installed by repository.installed.collectAsState(initial = repository.installedSync())
    return remember(packageId, installed) { installed.firstOrNull { it.id == packageId } }
}
