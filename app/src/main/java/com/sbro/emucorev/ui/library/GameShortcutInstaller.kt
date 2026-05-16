package com.sbro.emucorev.ui.library

import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import com.sbro.emucorev.R
import com.sbro.emucorev.core.vita.Emulator
import java.io.File

internal object GameShortcutInstaller {
    private const val ACTION_EXTERNAL_LAUNCH = "com.sbro.emucorev.action.LAUNCH"
    private const val APP_RESTART_PARAMETERS = "AppStartParameters"
    private const val EXTRA_TITLE_ID = "titleId"

    enum class Result {
        Requested,
        Unsupported,
        Failed
    }

    fun requestPinnedShortcut(
        context: Context,
        titleId: String,
        title: String,
        iconPath: String?
    ): Result {
        val appContext = context.applicationContext
        if (!ShortcutManagerCompat.isRequestPinShortcutSupported(appContext)) {
            return Result.Unsupported
        }

        val shortcut = runCatching {
            val icon = iconPath
                ?.let(::File)
                ?.takeIf(File::isFile)
                ?.let { BitmapFactory.decodeFile(it.absolutePath) }
                ?.let(IconCompat::createWithBitmap)
                ?: IconCompat.createWithResource(appContext, R.mipmap.ic_launcher)

            val launchIntent = Intent(appContext, Emulator::class.java).apply {
                action = ACTION_EXTERNAL_LAUNCH
                putExtra(EXTRA_TITLE_ID, titleId)
                putExtra(APP_RESTART_PARAMETERS, arrayOf("-r", titleId))
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }

            ShortcutInfoCompat.Builder(appContext, "game_$titleId")
                .setShortLabel(title.take(24).ifBlank { titleId })
                .setLongLabel(title.ifBlank { titleId })
                .setIcon(icon)
                .setIntent(launchIntent)
                .build()
        }.getOrElse {
            return Result.Failed
        }

        return if (ShortcutManagerCompat.requestPinShortcut(appContext, shortcut, null)) {
            Result.Requested
        } else {
            Result.Failed
        }
    }
}
