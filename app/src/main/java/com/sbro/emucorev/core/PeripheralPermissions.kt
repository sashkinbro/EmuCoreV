package com.sbro.emucorev.core

import android.content.Context

/** What the Settings UI shows for a runtime permission of a peripheral (camera / microphone). */
enum class PermissionStatus {
    GRANTED,

    /** Not granted, but the system prompt can still be shown. */
    CAN_REQUEST,

    /** Denied with "don't ask again": only the app settings page can grant it now. */
    BLOCKED
}

/**
 * Pure decision used by the permission rows. Android gives no direct "permanently denied" signal: a
 * permission that was asked before and has no rationale to show can only be changed in app settings.
 */
fun permissionStatus(granted: Boolean, askedBefore: Boolean, shouldShowRationale: Boolean): PermissionStatus = when {
    granted -> PermissionStatus.GRANTED
    !askedBefore || shouldShowRationale -> PermissionStatus.CAN_REQUEST
    else -> PermissionStatus.BLOCKED
}

/** Remembers which permissions the app already asked for (the input of [permissionStatus]). */
class PermissionAskHistory(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences("emucorev_permission_history", Context.MODE_PRIVATE)

    fun wasAsked(permission: String): Boolean = prefs.getBoolean(permission, false)

    fun markAsked(permission: String) {
        prefs.edit().putBoolean(permission, true).apply()
    }
}
