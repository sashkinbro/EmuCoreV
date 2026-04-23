package com.sbro.emucorev.data

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.content.edit
import androidx.core.os.LocaleListCompat
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorev.ui.theme.ThemeMode

enum class AppLanguage(val storageValue: Int, val languageTag: String) {
    SYSTEM(0, ""),
    ENGLISH(1, "en"),
    RUSSIAN(2, "ru"),
    UKRAINIAN(3, "uk");

    companion object {
        fun fromStorageValue(value: Int): AppLanguage = entries.firstOrNull { it.storageValue == value } ?: SYSTEM
    }
}

class AppPreferences(context: Context) {
    private val prefs = context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)

    var packagesFolderUri: String?
        get() = prefs.getString(KEY_PACKAGES_FOLDER_URI, null)
        set(value) = prefs.edit { putString(KEY_PACKAGES_FOLDER_URI, value) }

    var onboardingCompleted: Boolean
        get() = prefs.getBoolean(KEY_ONBOARDING_COMPLETED, false)
        set(value) = prefs.edit { putBoolean(KEY_ONBOARDING_COMPLETED, value) }

    var themeMode: ThemeMode
        get() = when (prefs.getInt(KEY_THEME_MODE, 0)) {
            1 -> ThemeMode.LIGHT
            2 -> ThemeMode.DARK
            else -> ThemeMode.SYSTEM
        }
        set(value) = prefs.edit {
            putInt(
                KEY_THEME_MODE,
                when (value) {
                    ThemeMode.SYSTEM -> 0
                    ThemeMode.LIGHT -> 1
                    ThemeMode.DARK -> 2
                }
            )
        }

    var appLanguage: AppLanguage
        get() = AppLanguage.fromStorageValue(prefs.getInt(KEY_APP_LANGUAGE, AppLanguage.SYSTEM.storageValue))
        set(value) = prefs.edit { putInt(KEY_APP_LANGUAGE, value.storageValue) }

    fun packagesFolderUriAsUri(): Uri? = packagesFolderUri?.let(Uri::parse)

    fun applyAppLanguage() {
        val locales = if (appLanguage == AppLanguage.SYSTEM) {
            LocaleListCompat.getEmptyLocaleList()
        } else {
            LocaleListCompat.forLanguageTags(appLanguage.languageTag)
        }
        AppCompatDelegate.setApplicationLocales(locales)
    }

    fun setPackagesFolder(context: Context, uri: Uri) {
        val resolver = context.contentResolver
        val previous = packagesFolderUriAsUri()
        if (previous != null && previous != uri) {
            releasePersistedPermission(resolver, previous)
        }
        runCatching {
            resolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        }
        packagesFolderUri = uri.toString()
    }

    fun clearPackagesFolder(context: Context) {
        packagesFolderUriAsUri()?.let { releasePersistedPermission(context.contentResolver, it) }
        packagesFolderUri = null
    }

    fun packagesFolderDisplayName(context: Context): String? {
        val uri = packagesFolderUriAsUri() ?: return null
        return DocumentFile.fromTreeUri(context, uri)?.name
            ?: uri.lastPathSegment?.substringAfterLast(':')
            ?: uri.toString()
    }

    private fun releasePersistedPermission(
        resolver: android.content.ContentResolver,
        uri: Uri
    ) {
        runCatching {
            resolver.releasePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        }
    }

    companion object {
        private const val KEY_PACKAGES_FOLDER_URI = "packages_folder_uri"
        private const val KEY_ONBOARDING_COMPLETED = "onboarding_completed"
        private const val KEY_THEME_MODE = "theme_mode"
        private const val KEY_APP_LANGUAGE = "app_language"
    }
}
