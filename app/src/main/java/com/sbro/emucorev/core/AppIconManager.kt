package com.sbro.emucorev.core

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.util.Log

object AppIconManager {
    private const val TAG = "AppIconManager"
    private const val DEFAULT_ALIAS_SUFFIX = ".MainActivityDefault"
    private const val PRO_ALIAS_SUFFIX = ".MainActivityPro"

    fun applyProIcon(context: Context, enabled: Boolean) {
        val appContext = context.applicationContext
        val packageName = appContext.packageName
        val defaultAlias = ComponentName(packageName, packageName + DEFAULT_ALIAS_SUFFIX)
        val proAlias = ComponentName(packageName, packageName + PRO_ALIAS_SUFFIX)
        val packageManager = appContext.packageManager
        val targetAlias = if (enabled) proAlias else defaultAlias
        val otherAlias = if (enabled) defaultAlias else proAlias

        if (isLauncherAliasActive(packageManager, targetAlias, defaultAlias)) return

        try {
            packageManager.setComponentEnabledSetting(
                targetAlias,
                PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
                PackageManager.DONT_KILL_APP
            )
            packageManager.setComponentEnabledSetting(
                otherAlias,
                PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                PackageManager.DONT_KILL_APP
            )
        } catch (error: RuntimeException) {
            Log.w(TAG, "Unable to switch launcher icon", error)
        }
    }

    private fun isLauncherAliasActive(
        packageManager: PackageManager,
        alias: ComponentName,
        defaultAlias: ComponentName
    ): Boolean {
        val state = packageManager.getComponentEnabledSetting(alias)
        return state == PackageManager.COMPONENT_ENABLED_STATE_ENABLED ||
            (alias == defaultAlias && state == PackageManager.COMPONENT_ENABLED_STATE_DEFAULT)
    }
}
