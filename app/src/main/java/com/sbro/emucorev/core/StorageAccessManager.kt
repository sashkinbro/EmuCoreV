package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import com.sbro.emucorev.BuildConfig

object StorageAccessManager {
    fun needsAllFilesAccessForCustomStorage(): Boolean {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
            !Environment.isExternalStorageManager()
    }

    fun allFilesAccessIntent(context: Context): Intent {
        val packageIntent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
            .setData(Uri.parse("package:${BuildConfig.APPLICATION_ID}"))
        return if (packageIntent.resolveActivity(context.packageManager) != null) {
            packageIntent
        } else {
            Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
        }
    }
}
