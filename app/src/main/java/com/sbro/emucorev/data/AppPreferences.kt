package com.sbro.emucorev.data

import android.app.LocaleManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.LocaleList
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.content.edit
import androidx.core.os.LocaleListCompat
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorev.core.KeyboardMouseBindings
import com.sbro.emucorev.ui.theme.ThemeMode
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import org.json.JSONObject

enum class AppLanguage(val storageValue: Int, val languageTag: String) {
    SYSTEM(0, ""),
    ENGLISH(1, "en"),
    RUSSIAN(2, "ru"),
    UKRAINIAN(3, "uk"),
    SPANISH(4, "es"),
    FRENCH(5, "fr"),
    GERMAN(6, "de"),
    PORTUGUESE(7, "pt"),
    CHINESE(8, "zh"),
    HINDI(9, "hi"),
    ITALIAN(10, "it"),
    TURKISH(11, "tr"),
    ARABIC(12, "ar"),
    CZECH(13, "cs"),
    POLISH(14, "pl"),
    JAPANESE(15, "ja"),
    KOREAN(16, "ko"),
    INDONESIAN(17, "in"),
    PERSIAN(18, "fa");

    companion object {
        fun fromStorageValue(value: Int): AppLanguage = entries.firstOrNull { it.storageValue == value } ?: SYSTEM
    }
}

enum class LibraryLayoutMode { LIST, GRID }

class AppPreferences(context: Context) {
    private val appContext = context.applicationContext
    private val prefs = context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)

    var keyboardMouseSettings: KeyboardMouseSettings
        get() = runCatching {
            KeyboardMouseSettingsCodec.decode(
                prefs.getString(KEY_KEYBOARD_MOUSE_SETTINGS, null)?.let { raw ->
                    runCatching { JSONObject(raw) }.getOrNull()
                }
            )
        }.getOrNull() ?: KeyboardMouseSettings()
        set(value) = prefs.edit {
            putString(KEY_KEYBOARD_MOUSE_SETTINGS, KeyboardMouseSettingsCodec.encode(value).toString())
        }

    val keyboardMouseSettingsFlow: Flow<KeyboardMouseSettings>
        get() = callbackFlow {
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_KEYBOARD_MOUSE_SETTINGS) trySend(keyboardMouseSettings)
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            trySend(keyboardMouseSettings)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    var packagesFolderUri: String?
        get() = prefs.getString(KEY_PACKAGES_FOLDER_URI, null)
        set(value) = prefs.edit { putString(KEY_PACKAGES_FOLDER_URI, value) }

    var vitaStorageRootPath: String?
        get() = prefs.getString(KEY_VITA_STORAGE_ROOT_PATH, null)
        set(value) = prefs.edit {
            if (value == null) {
                remove(KEY_VITA_STORAGE_ROOT_PATH)
            } else {
                putString(KEY_VITA_STORAGE_ROOT_PATH, value)
            }
        }

    var onboardingCompleted: Boolean
        get() = prefs.getBoolean(KEY_ONBOARDING_COMPLETED, false)
        set(value) = prefs.edit { putBoolean(KEY_ONBOARDING_COMPLETED, value) }

    var themeMode: ThemeMode
        get() = when (prefs.getInt(KEY_THEME_MODE, 0)) {
            1 -> ThemeMode.LIGHT
            2 -> ThemeMode.DARK
            3 -> if (proUnlocked) ThemeMode.PRO else ThemeMode.SYSTEM
            4 -> ThemeMode.NEON
            else -> ThemeMode.SYSTEM
        }
        set(value) {
            if (value == ThemeMode.PRO && !proUnlocked) return
            prefs.edit {
                putInt(
                    KEY_THEME_MODE,
                    when (value) {
                        ThemeMode.SYSTEM -> 0
                        ThemeMode.LIGHT -> 1
                        ThemeMode.DARK -> 2
                        ThemeMode.PRO -> 3
                        ThemeMode.NEON -> 4
                    }
                )
            }
        }

    val themeModeFlow: Flow<ThemeMode>
        get() = callbackFlow {
            trySend(themeMode)
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_THEME_MODE || key == KEY_PRO_UNLOCKED) {
                    trySend(themeMode)
                }
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    var proUnlocked: Boolean
        get() = prefs.getBoolean(KEY_PRO_UNLOCKED, false)
        set(value) {
            val resetProTheme = !value && prefs.getInt(KEY_THEME_MODE, 0) == 3
            prefs.edit {
                putBoolean(KEY_PRO_UNLOCKED, value)
                if (resetProTheme) {
                    putInt(KEY_THEME_MODE, 0)
                }
            }
        }

    var welcomeDialogShown: Boolean
        get() = prefs.getBoolean(KEY_WELCOME_DIALOG_SHOWN, false)
        set(value) = prefs.edit { putBoolean(KEY_WELCOME_DIALOG_SHOWN, value) }

    var showEmulationQuickBar: Boolean
        get() = prefs.getBoolean(KEY_SHOW_EMULATION_QUICK_BAR, false)
        set(value) = prefs.edit { putBoolean(KEY_SHOW_EMULATION_QUICK_BAR, value) }

    val showEmulationQuickBarFlow: Flow<Boolean>
        get() = callbackFlow {
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_SHOW_EMULATION_QUICK_BAR) trySend(showEmulationQuickBar)
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            trySend(showEmulationQuickBar)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    var confirmSaveStateActions: Boolean
        get() = prefs.getBoolean(KEY_CONFIRM_SAVE_STATE_ACTIONS, true)
        set(value) = prefs.edit { putBoolean(KEY_CONFIRM_SAVE_STATE_ACTIONS, value) }

    val confirmSaveStateActionsFlow: Flow<Boolean>
        get() = callbackFlow {
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_CONFIRM_SAVE_STATE_ACTIONS) {
                    trySend(confirmSaveStateActions)
                }
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            trySend(confirmSaveStateActions)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    var libraryLayoutMode: LibraryLayoutMode
        get() = LibraryLayoutMode.entries.firstOrNull { it.name == prefs.all[KEY_LIBRARY_LAYOUT_MODE] }
            ?: LibraryLayoutMode.LIST
        set(value) = prefs.edit { putString(KEY_LIBRARY_LAYOUT_MODE, value.name) }

    val libraryLayoutModeFlow: Flow<LibraryLayoutMode>
        get() = callbackFlow {
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_LIBRARY_LAYOUT_MODE) trySend(libraryLayoutMode)
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            trySend(libraryLayoutMode)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    var lastCoreBinaryFingerprint: String?
        get() = prefs.getString(KEY_LAST_CORE_BINARY_FINGERPRINT, null)
        set(value) = prefs.edit { putString(KEY_LAST_CORE_BINARY_FINGERPRINT, value) }

    var appLanguage: AppLanguage
        get() = AppLanguage.fromStorageValue(prefs.getInt(KEY_APP_LANGUAGE, AppLanguage.SYSTEM.storageValue))
        set(value) = prefs.edit { putInt(KEY_APP_LANGUAGE, value.storageValue) }

    var skippedUpdateTag: String?
        get() = prefs.getString(KEY_SKIPPED_UPDATE_TAG, null)
        set(value) = prefs.edit { putString(KEY_SKIPPED_UPDATE_TAG, value) }

    fun packagesFolderUriAsUri(): Uri? = packagesFolderUri?.let(Uri::parse)

    fun applyAppLanguage() {
        val tag = if (appLanguage == AppLanguage.SYSTEM) "" else appLanguage.languageTag
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val manager = appContext.getSystemService(LocaleManager::class.java)
            manager?.applicationLocales = if (tag.isEmpty()) {
                LocaleList.getEmptyLocaleList()
            } else {
                LocaleList.forLanguageTags(tag)
            }
        } else {
            val locales = if (tag.isEmpty()) {
                LocaleListCompat.getEmptyLocaleList()
            } else {
                LocaleListCompat.forLanguageTags(tag)
            }
            AppCompatDelegate.setApplicationLocales(locales)
        }
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
        return runCatching { DocumentFile.fromTreeUri(context, uri)?.name }.getOrNull()
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
        const val DEFAULT_MOUSE_SENSITIVITY = KeyboardMouseBindings.DEFAULT_MOUSE_SENSITIVITY
        const val MOUSE_SENSITIVITY_MIN = KeyboardMouseBindings.MOUSE_SENSITIVITY_MIN
        const val MOUSE_SENSITIVITY_MAX = KeyboardMouseBindings.MOUSE_SENSITIVITY_MAX
        const val DEFAULT_MOUSE_VERTICAL_SENSITIVITY =
            KeyboardMouseBindings.DEFAULT_MOUSE_VERTICAL_SENSITIVITY
        const val MOUSE_VERTICAL_SENSITIVITY_MIN =
            KeyboardMouseBindings.MOUSE_VERTICAL_SENSITIVITY_MIN
        const val MOUSE_VERTICAL_SENSITIVITY_MAX =
            KeyboardMouseBindings.MOUSE_VERTICAL_SENSITIVITY_MAX
        const val DEFAULT_MOUSE_SMOOTHING = 0
        const val MOUSE_SMOOTHING_MIN = 0
        const val MOUSE_SMOOTHING_MAX = 90
        const val DEFAULT_MOUSE_DEADZONE = KeyboardMouseBindings.DEFAULT_MOUSE_DEADZONE
        const val MOUSE_DEADZONE_MIN = KeyboardMouseBindings.MOUSE_DEADZONE_MIN
        const val MOUSE_DEADZONE_MAX = KeyboardMouseBindings.MOUSE_DEADZONE_MAX
        const val DEFAULT_MOUSE_WHEEL_PULSE_MS =
            KeyboardMouseBindings.DEFAULT_MOUSE_WHEEL_PULSE_MS
        const val MOUSE_WHEEL_PULSE_MIN_MS = KeyboardMouseBindings.MOUSE_WHEEL_PULSE_MIN_MS
        const val MOUSE_WHEEL_PULSE_MAX_MS = KeyboardMouseBindings.MOUSE_WHEEL_PULSE_MAX_MS
        const val MOUSE_AIM_OFF = KeyboardMouseBindings.MOUSE_AIM_OFF
        const val MOUSE_AIM_RIGHT_STICK = KeyboardMouseBindings.MOUSE_AIM_RIGHT_STICK
        const val MOUSE_AIM_LEFT_STICK = KeyboardMouseBindings.MOUSE_AIM_LEFT_STICK
        const val MOUSE_WHEEL_OFF = KeyboardMouseBindings.MOUSE_WHEEL_OFF
        const val MOUSE_WHEEL_DPAD = KeyboardMouseBindings.MOUSE_WHEEL_DPAD
        const val MOUSE_WHEEL_TRIGGERS = KeyboardMouseBindings.MOUSE_WHEEL_TRIGGERS
        const val MOUSE_WHEEL_LEFT_STICK = KeyboardMouseBindings.MOUSE_WHEEL_LEFT_STICK
        const val MOUSE_WHEEL_RIGHT_STICK = KeyboardMouseBindings.MOUSE_WHEEL_RIGHT_STICK
        const val MOUSE_WHEEL_SHOULDERS = KeyboardMouseBindings.MOUSE_WHEEL_SHOULDERS

        private const val KEY_KEYBOARD_MOUSE_SETTINGS = "keyboard_mouse_settings"
        private const val KEY_PACKAGES_FOLDER_URI = "packages_folder_uri"
        private const val KEY_VITA_STORAGE_ROOT_PATH = "vita_storage_root_path"
        private const val KEY_ONBOARDING_COMPLETED = "onboarding_completed"
        private const val KEY_THEME_MODE = "theme_mode"
        private const val KEY_PRO_UNLOCKED = "pro_unlocked"
        private const val KEY_WELCOME_DIALOG_SHOWN = "welcome_dialog_shown"
        private const val KEY_LIBRARY_LAYOUT_MODE = "library_layout_mode"
        private const val KEY_CONFIRM_SAVE_STATE_ACTIONS = "confirm_save_state_actions"
        private const val KEY_SHOW_EMULATION_QUICK_BAR = "show_emulation_quick_bar"
        private const val KEY_LAST_CORE_BINARY_FINGERPRINT = "last_core_binary_fingerprint"
        private const val KEY_APP_LANGUAGE = "app_language"
        private const val KEY_SKIPPED_UPDATE_TAG = "skipped_update_tag"
    }
}
