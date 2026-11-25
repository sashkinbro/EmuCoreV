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
            5 -> when {
                prefs.getString(KEY_THEME_PACKAGE_ID, null).isNullOrBlank() -> ThemeMode.SYSTEM
                prefs.getBoolean(KEY_THEME_PACKAGE_REQUIRES_PRO, false) && !proUnlocked -> ThemeMode.SYSTEM
                else -> ThemeMode.PACKAGE
            }
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
                        // Packages are applied through applyThemePackage(); keep the stored id.
                        ThemeMode.PACKAGE ->
                            if (prefs.getString(KEY_THEME_PACKAGE_ID, null).isNullOrBlank()) 0 else 5
                    }
                )
            }
        }

    val themeModeFlow: Flow<ThemeMode>
        get() = callbackFlow {
            trySend(themeMode)
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_THEME_MODE || key == KEY_PRO_UNLOCKED ||
                    key == KEY_THEME_PACKAGE_ID || key == KEY_THEME_PACKAGE_REQUIRES_PRO
                ) {
                    trySend(themeMode)
                }
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    val themePackageId: String?
        get() = prefs.getString(KEY_THEME_PACKAGE_ID, null)

    /** Id of the applied Store controls skin pack (see data/store/StoreControlSkins), or null. */
    val touchControlSkinPack: Flow<String?>
        get() = callbackFlow {
            trySend(prefs.getString(KEY_TOUCH_CONTROL_SKIN_PACK, null)?.takeIf { it.isNotBlank() })
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_TOUCH_CONTROL_SKIN_PACK) {
                    trySend(prefs.getString(KEY_TOUCH_CONTROL_SKIN_PACK, null)?.takeIf { it.isNotBlank() })
                }
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    fun setTouchControlSkinPack(id: String?) {
        prefs.edit {
            if (id.isNullOrBlank()) remove(KEY_TOUCH_CONTROL_SKIN_PACK) else putString(KEY_TOUCH_CONTROL_SKIN_PACK, id)
        }
    }

    /**
     * Remembers which Store font was copied into the custom-font slot and where, so the Store can tell
     * whether that font is still the selected one. Null clears it.
     */
    fun setStoreFont(id: String?, path: String?) {
        prefs.edit {
            if (id.isNullOrBlank() || path.isNullOrBlank()) {
                remove(KEY_STORE_FONT_ID)
                remove(KEY_STORE_FONT_PATH)
            } else {
                putString(KEY_STORE_FONT_ID, id)
                putString(KEY_STORE_FONT_PATH, path)
            }
        }
    }

    /** (Store font id, custom-font path it was installed to), or null. */
    val storeFont: Flow<Pair<String, String>?>
        get() = callbackFlow {
            fun read(): Pair<String, String>? {
                val id = prefs.getString(KEY_STORE_FONT_ID, null)?.takeIf { it.isNotBlank() } ?: return null
                val path = prefs.getString(KEY_STORE_FONT_PATH, null)?.takeIf { it.isNotBlank() } ?: return null
                return id to path
            }
            trySend(read())
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_STORE_FONT_ID || key == KEY_STORE_FONT_PATH) trySend(read())
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    /** Id of the applied theme package; meaningful only while [themeMode] is PACKAGE. */
    val themePackageIdFlow: Flow<String?>
        get() = callbackFlow {
            trySend(prefs.getString(KEY_THEME_PACKAGE_ID, null))
            val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEY_THEME_PACKAGE_ID) trySend(prefs.getString(KEY_THEME_PACKAGE_ID, null))
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
        }.distinctUntilChanged()

    /**
     * Applies an installed theme package. Pro-only packages are refused without Pro, like the
     * built-in Pro theme. Returns false when refused.
     */
    fun applyThemePackage(id: String, requiresPro: Boolean): Boolean {
        if (requiresPro && !proUnlocked) return false
        prefs.edit {
            putString(KEY_THEME_PACKAGE_ID, id)
            putBoolean(KEY_THEME_PACKAGE_REQUIRES_PRO, requiresPro)
            putInt(KEY_THEME_MODE, 5)
        }
        return true
    }

    /** Called when an installed package is deleted: falls back to SYSTEM if it was applied. */
    fun clearThemePackage(id: String) {
        if (prefs.getString(KEY_THEME_PACKAGE_ID, null) != id) return
        prefs.edit {
            remove(KEY_THEME_PACKAGE_ID)
            remove(KEY_THEME_PACKAGE_REQUIRES_PRO)
            if (prefs.getInt(KEY_THEME_MODE, 0) == 5) putInt(KEY_THEME_MODE, 0)
        }
    }

    var proUnlocked: Boolean
        get() = prefs.getBoolean(KEY_PRO_UNLOCKED, false)
        set(value) {
            val resetProTheme = !value && (
                prefs.getInt(KEY_THEME_MODE, 0) == 3 ||
                    (prefs.getInt(KEY_THEME_MODE, 0) == 5 && prefs.getBoolean(KEY_THEME_PACKAGE_REQUIRES_PRO, false))
                )
            prefs.edit {
                putBoolean(KEY_PRO_UNLOCKED, value)
                if (resetProTheme) {
                    putInt(KEY_THEME_MODE, 0)
                }
            }
        }

    // Separate file so it can be excluded from backup / device transfer (see backup_rules.xml).
    private val agePrefs = context.getSharedPreferences(AGE_PREFS_NAME, Context.MODE_PRIVATE)

    /** Last Play Age Signals result. Local only: excluded from backup and device transfer. */
    suspend fun getAgeSignals(): com.sbro.emucorev.core.AgeSignalsSnapshot {
        fun intOrNull(key: String): Int? = if (agePrefs.contains(key)) agePrefs.getInt(key, 0) else null
        return com.sbro.emucorev.core.AgeSignalsSnapshot(
            ageLower = intOrNull(KEY_AGE_LOWER),
            ageUpper = intOrNull(KEY_AGE_UPPER),
            significantChangeStatus = intOrNull(KEY_AGE_CHANGE_STATUS),
            installId = agePrefs.getString(KEY_AGE_INSTALL_ID, null),
            ageRangeSource = intOrNull(KEY_AGE_SOURCE),
            timestampMs = agePrefs.getLong(KEY_AGE_TIMESTAMP, 0L)
        )
    }

    suspend fun setAgeSignals(snapshot: com.sbro.emucorev.core.AgeSignalsSnapshot) {
        agePrefs.edit {
            fun putOrRemove(key: String, value: Int?) {
                if (value != null) putInt(key, value) else remove(key)
            }
            putOrRemove(KEY_AGE_LOWER, snapshot.ageLower)
            putOrRemove(KEY_AGE_UPPER, snapshot.ageUpper)
            putOrRemove(KEY_AGE_CHANGE_STATUS, snapshot.significantChangeStatus)
            putOrRemove(KEY_AGE_SOURCE, snapshot.ageRangeSource)
            val installId = snapshot.installId
            if (installId != null) putString(KEY_AGE_INSTALL_ID, installId) else remove(KEY_AGE_INSTALL_ID)
            putLong(KEY_AGE_TIMESTAMP, snapshot.timestampMs)
        }
    }

    var welcomeDialogShown: Boolean
        get() = prefs.getBoolean(KEY_WELCOME_DIALOG_SHOWN, false)
        set(value) = prefs.edit { putBoolean(KEY_WELCOME_DIALOG_SHOWN, value) }

    // Recurring Pro promotion. Separate from welcomeDialogShown, which also marks existing installs.
    var proReminderLastShownAtMs: Long
        get() = prefs.getLong(KEY_PRO_REMINDER_LAST_SHOWN_AT_MS, 0L)
        set(value) = prefs.edit { putLong(KEY_PRO_REMINDER_LAST_SHOWN_AT_MS, value) }

    var proReminderDismissStreak: Int
        get() = prefs.getInt(KEY_PRO_REMINDER_DISMISS_STREAK, 0)
        set(value) = prefs.edit { putInt(KEY_PRO_REMINDER_DISMISS_STREAK, value) }

    fun proPromoHiddenUntilFlow(id: String): Flow<Long> = callbackFlow {
        val key = proPromoHiddenUntilKey(id)
        trySend(prefs.getLong(key, 0L))
        val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, changed ->
            if (changed == key) trySend(prefs.getLong(key, 0L))
        }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }.distinctUntilChanged()

    fun proPromoDismissStreak(id: String): Int = prefs.getInt(proPromoDismissStreakKey(id), 0)

    fun hideProPromo(id: String, streak: Int, untilMs: Long) {
        prefs.edit {
            putInt(proPromoDismissStreakKey(id), streak)
            putLong(proPromoHiddenUntilKey(id), untilMs)
        }
    }

    fun resetProPromoDismissStreak(id: String) {
        prefs.edit { remove(proPromoDismissStreakKey(id)) }
    }

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
            ?: LibraryLayoutMode.GRID
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
        private const val KEY_THEME_PACKAGE_ID = "theme_package_id"
        private const val KEY_TOUCH_CONTROL_SKIN_PACK = "touch_control_skin_pack"
        private const val KEY_STORE_FONT_ID = "store_font_id"
        private const val KEY_STORE_FONT_PATH = "store_font_path"
        private const val KEY_THEME_PACKAGE_REQUIRES_PRO = "theme_package_requires_pro"
        private const val KEY_PRO_UNLOCKED = "pro_unlocked"
        private const val KEY_WELCOME_DIALOG_SHOWN = "welcome_dialog_shown"
        private const val KEY_PRO_REMINDER_LAST_SHOWN_AT_MS = "pro_reminder_last_shown_at_ms"
        private const val KEY_PRO_REMINDER_DISMISS_STREAK = "pro_reminder_dismiss_streak"
        private fun proPromoHiddenUntilKey(id: String) = "pro_promo_hidden_until_$id"
        private fun proPromoDismissStreakKey(id: String) = "pro_promo_dismiss_streak_$id"
        private const val AGE_PREFS_NAME = "age_signals"
        private const val KEY_AGE_LOWER = "age_signals_lower"
        private const val KEY_AGE_UPPER = "age_signals_upper"
        private const val KEY_AGE_CHANGE_STATUS = "age_signals_change_status"
        private const val KEY_AGE_SOURCE = "age_signals_source"
        private const val KEY_AGE_INSTALL_ID = "age_signals_install_id"
        private const val KEY_AGE_TIMESTAMP = "age_signals_timestamp"
        private const val KEY_LIBRARY_LAYOUT_MODE = "library_layout_mode"
        private const val KEY_CONFIRM_SAVE_STATE_ACTIONS = "confirm_save_state_actions"
        private const val KEY_SHOW_EMULATION_QUICK_BAR = "show_emulation_quick_bar"
        private const val KEY_LAST_CORE_BINARY_FINGERPRINT = "last_core_binary_fingerprint"
        private const val KEY_APP_LANGUAGE = "app_language"
        private const val KEY_SKIPPED_UPDATE_TAG = "skipped_update_tag"
    }
}
