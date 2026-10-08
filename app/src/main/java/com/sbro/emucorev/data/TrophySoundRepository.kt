package com.sbro.emucorev.data

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import android.provider.OpenableColumns
import java.util.concurrent.atomic.AtomicLong
import java.util.WeakHashMap

enum class TrophySoundMode { SYSTEM, OFF, CUSTOM }
enum class TrophySoundSelectionResult { ACCEPTED, REJECTED, SUPERSEDED }

data class TrophySoundSettings(
    val mode: TrophySoundMode = TrophySoundMode.SYSTEM,
    val customUri: Uri? = null,
    val customName: String? = null,
    val customUnavailable: Boolean = false
)

sealed interface TrophySoundPlaybackRoute {
    data object Silent : TrophySoundPlaybackRoute
    data object SystemNotification : TrophySoundPlaybackRoute
    data class Custom(val uri: Uri) : TrophySoundPlaybackRoute
}

fun resolveTrophySoundPlayback(settings: TrophySoundSettings): TrophySoundPlaybackRoute = when (settings.mode) {
    TrophySoundMode.SYSTEM -> TrophySoundPlaybackRoute.SystemNotification
    TrophySoundMode.OFF -> TrophySoundPlaybackRoute.Silent
    TrophySoundMode.CUSTOM -> settings.customUri
        ?.takeUnless { settings.customUnavailable }
        ?.let(TrophySoundPlaybackRoute::Custom)
        ?: TrophySoundPlaybackRoute.SystemNotification
}

/** Small seam around document-provider permission and metadata calls. */
interface TrophySoundUriAccess {
    fun hasPersistedPermission(uri: Uri): Boolean
    fun persistReadablePermission(uri: Uri): Boolean
    fun isReadable(uri: Uri): Boolean
    fun displayName(uri: Uri): String?
    fun releasePermission(uri: Uri)
}

class TrophySoundRepository(
    context: Context,
    private val uriAccess: TrophySoundUriAccess = AndroidTrophySoundUriAccess(context),
    preferencesName: String = PREFERENCES_NAME
) {
    private val preferences = context.applicationContext.getSharedPreferences(preferencesName, Context.MODE_PRIVATE)
    private val coordinator = coordinatorFor(preferences)
    private val writeLock = coordinator.writeLock
    private val selectionRevision = coordinator.selectionRevision

    @Synchronized
    fun current(): TrophySoundSettings {
        val mode = runCatching {
            TrophySoundMode.valueOf(preferences.getString(KEY_MODE, TrophySoundMode.SYSTEM.name) ?: TrophySoundMode.SYSTEM.name)
        }.getOrDefault(TrophySoundMode.SYSTEM)
        val uri = preferences.getString(KEY_URI, null)?.let(Uri::parse)
        return TrophySoundSettings(
            mode = if (mode == TrophySoundMode.CUSTOM && uri == null) TrophySoundMode.SYSTEM else mode,
            customUri = uri,
            customName = preferences.getString(KEY_NAME, null),
            customUnavailable = preferences.getBoolean(KEY_UNAVAILABLE, false)
        )
    }

    fun selectMode(mode: TrophySoundMode): Boolean {
        val revision = selectionRevision.incrementAndGet()
        synchronized(writeLock) {
            if (selectionRevision.get() != revision) return true
            val before = current()
            if (mode == TrophySoundMode.CUSTOM && before.customUri == null) return false
            preferences.edit()
                .putString(KEY_MODE, mode.name)
                .apply()
            return true
        }
    }

    /**
     * Validates the picked document and persists its grant before changing the active choice.
     * A failed/cancelled picker therefore leaves the previous setting intact.
     */
    fun selectCustomSound(uri: Uri): TrophySoundSelectionResult {
        val revision = selectionRevision.incrementAndGet()
        val before = current()
        val old = before.customUri
        val permissionWasAlreadyHeld = runCatching { uriAccess.hasPersistedPermission(uri) }.getOrDefault(false)
        val permissionPersisted = permissionWasAlreadyHeld ||
            runCatching { uriAccess.persistReadablePermission(uri) }.getOrDefault(false)
        if (!permissionPersisted) return if (selectionRevision.get() == revision) TrophySoundSelectionResult.REJECTED else TrophySoundSelectionResult.SUPERSEDED
        if (selectionRevision.get() != revision) {
            releaseNewPermission(uri, old, permissionWasAlreadyHeld)
            return TrophySoundSelectionResult.SUPERSEDED
        }
        val accepted = runCatching { uriAccess.isReadable(uri) }.getOrDefault(false)
        if (!accepted) {
            releaseNewPermission(uri, old, permissionWasAlreadyHeld)
            return if (selectionRevision.get() == revision) TrophySoundSelectionResult.REJECTED else TrophySoundSelectionResult.SUPERSEDED
        }
        val name = runCatching { uriAccess.displayName(uri) }.getOrNull()?.takeIf(String::isNotBlank)
        synchronized(writeLock) {
            if (selectionRevision.get() != revision) {
                releaseNewPermission(uri, old, permissionWasAlreadyHeld)
                return TrophySoundSelectionResult.SUPERSEDED
            }
            val saved = preferences.edit()
                .putString(KEY_MODE, TrophySoundMode.CUSTOM.name)
                .putString(KEY_URI, uri.toString())
                .putNullable(KEY_NAME, name)
                .putBoolean(KEY_UNAVAILABLE, false)
                .commit()
            if (!saved) {
                restorePreferences(before)
                releaseNewPermission(uri, old, permissionWasAlreadyHeld)
                return TrophySoundSelectionResult.REJECTED
            }
        }
        if (old != null && old != uri) runCatching { uriAccess.releasePermission(old) }
        return TrophySoundSelectionResult.ACCEPTED
    }

    fun refreshCustomAvailability(): Boolean {
        val uri = current().customUri ?: return false
        val available = runCatching { uriAccess.isReadable(uri) }.getOrDefault(false)
        synchronized(writeLock) {
            if (current().customUri != uri) return false
            preferences.edit().putBoolean(KEY_UNAVAILABLE, !available).apply()
            return available
        }
    }

    fun markCustomUnavailable(uri: Uri) {
        synchronized(writeLock) {
            if (current().customUri == uri) preferences.edit().putBoolean(KEY_UNAVAILABLE, true).apply()
        }
    }

    /** Restores portable values only; URI access is revalidated because SAF grants are device-local. */
    fun restoreBackup(modeName: String?, uriText: String?, name: String?): Boolean {
        val revision = selectionRevision.incrementAndGet()
        val oldSettings = current()
        val mode = runCatching { TrophySoundMode.valueOf(modeName ?: TrophySoundMode.SYSTEM.name) }
            .getOrDefault(TrophySoundMode.SYSTEM)
        val uri = uriText?.takeIf(String::isNotBlank)?.let(Uri::parse)
        val safeMode = if (mode == TrophySoundMode.CUSTOM && uri == null) TrophySoundMode.SYSTEM else mode
        synchronized(writeLock) {
            if (selectionRevision.get() != revision) return false
            val saved = preferences.edit()
                .putString(KEY_MODE, safeMode.name)
                .putNullable(KEY_URI, uri?.toString())
                .putNullable(KEY_NAME, name)
                .putBoolean(KEY_UNAVAILABLE, uri != null)
                .commit()
            if (!saved) {
                restorePreferences(oldSettings)
                return false
            }
        }
        if (oldSettings.customUri != null && oldSettings.customUri != uri) {
            runCatching { uriAccess.releasePermission(oldSettings.customUri) }
        }
        return true
    }

    private fun restorePreferences(oldSettings: TrophySoundSettings) {
        preferences.edit()
            .putString(KEY_MODE, oldSettings.mode.name)
            .putNullable(KEY_URI, oldSettings.customUri?.toString())
            .putNullable(KEY_NAME, oldSettings.customName)
            .putBoolean(KEY_UNAVAILABLE, oldSettings.customUnavailable)
            .apply()
    }

    private fun releaseNewPermission(uri: Uri, oldUri: Uri?, wasAlreadyHeld: Boolean) {
        if (uri != oldUri && !wasAlreadyHeld) runCatching { uriAccess.releasePermission(uri) }
    }

    companion object {
        const val PREFERENCES_NAME = "emucorev_prefs"
        private const val KEY_MODE = "trophy_sound_mode"
        private const val KEY_URI = "trophy_sound_custom_uri"
        private const val KEY_NAME = "trophy_sound_custom_name"
        private const val KEY_UNAVAILABLE = "trophy_sound_custom_unavailable"

        private val coordinators = WeakHashMap<SharedPreferences, TrophySoundCoordinator>()

        private fun coordinatorFor(preferences: SharedPreferences): TrophySoundCoordinator =
            synchronized(coordinators) {
                coordinators.getOrPut(preferences) { TrophySoundCoordinator() }
            }
    }
}

private class TrophySoundCoordinator {
    val writeLock = Any()
    val selectionRevision = AtomicLong()
}

private fun android.content.SharedPreferences.Editor.putNullable(key: String, value: String?): android.content.SharedPreferences.Editor =
    if (value == null) remove(key) else putString(key, value)

private class AndroidTrophySoundUriAccess(private val context: Context) : TrophySoundUriAccess {
    override fun persistReadablePermission(uri: Uri): Boolean {
        return try {
            context.contentResolver.takePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            true
        } catch (_: SecurityException) {
            // Some providers do not expose persistable grants; test readability separately.
            false
        } catch (_: IllegalArgumentException) {
            false
        }
    }

    override fun hasPersistedPermission(uri: Uri): Boolean = context.contentResolver.persistedUriPermissions.any {
        it.uri == uri && it.isReadPermission
    }

    override fun isReadable(uri: Uri): Boolean = runCatching {
        val stream = context.contentResolver.openInputStream(uri) ?: return false
        stream.use { true }
    }.getOrDefault(false)

    override fun displayName(uri: Uri): String? = context.contentResolver.query(
        uri,
        arrayOf(OpenableColumns.DISPLAY_NAME),
        null,
        null,
        null
    )?.use { cursor ->
        if (!cursor.moveToFirst()) null else cursor.getString(cursor.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
    }

    override fun releasePermission(uri: Uri) {
        context.contentResolver.releasePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
}
