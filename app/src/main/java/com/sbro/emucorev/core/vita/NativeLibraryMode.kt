package com.sbro.emucorev.core.vita

import android.content.Context
import android.content.Intent
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.SKIP_AUTOMATIC_STATE_LOAD_EXTRA
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.data.InstalledGameRepository
import java.io.File

/** Library navigation is separate from game launch and automatic save-state preferences. */
object NativeLibraryMode {
    const val TITLE_ID = "__VITA_NATIVE_LIBRARY__"
    const val RETURN_TO_NATIVE_LIBRARY = "return_to_native_library"
    const val EXIT_NATIVE_LIBRARY = "exit_native_library"
    val available: Boolean get() = BuildConfig.DEBUG

    fun needsCatalogBootstrap(context: Context): Boolean {
        val database = File(context.cacheDir, "native-shell-runtime/vita/ur0/shell/db/app.db")
        if (!database.isFile) return true
        return runCatching {
            android.database.sqlite.SQLiteDatabase.openDatabase(database.path, null,
                android.database.sqlite.SQLiteDatabase.OPEN_READONLY).use { db ->
                db.rawQuery("SELECT 1 FROM tbl_appinfo WHERE titleId='NPXS10008' LIMIT 1", null).use { !it.moveToFirst() }
            }
        }.getOrDefault(true)
    }

    /** Runs on SDL's worker before starting the guest; never edits a live shell database. */
    fun prepare(context: Context): File {
        val root = File(context.cacheDir, "native-shell-runtime/vita")
        check(root.isDirectory || root.mkdirs())
        val versions = File(EmulatorStorage.vitaRoot(context), "system-packages")
        versions.listFiles().orEmpty().filter { it.isFile && it.name.matches(Regex("[0-9]+\\.version")) }
            .forEach { source ->
                val target = File(root, "system-packages/${source.name}")
                check(target.parentFile!!.isDirectory || target.parentFile!!.mkdirs())
                if (!target.isFile || !source.readBytes().contentEquals(target.readBytes())) source.copyTo(target, overwrite = true)
            }
        // Older installers discarded SPKG versions. Recover only missing
        // metadata from the actual cached packages; never invent a version.
        val downloads = File(EmulatorStorage.runtimeRoot(context), "Download/firmware-downloads")
        listOf("PSVUPDAT.PUP", "PSP2UPDAT.PUP").forEach { name ->
            val source = File(downloads, name)
            if (source.isFile && (name == "PSVUPDAT.PUP" && !File(root, "system-packages/1.version").isFile ||
                    name == "PSP2UPDAT.PUP" && !File(root, "system-packages/23.version").isFile)) {
                runCatching { FirmwarePackageVersions.read(source) }.getOrDefault(emptyMap()).forEach { (type, version) ->
                    val target = File(root, "system-packages/$type.version")
                    if (!target.isFile) {
                        check(target.parentFile!!.isDirectory || target.parentFile!!.mkdirs())
                        target.writeText(version.toString(16) + "\n")
                    }
                }
            }
        }
        if (!needsCatalogBootstrap(context)) {
            NativeLibraryCatalog.synchronize(context, root, InstalledGameRepository().loadInstalledGames(context))
        }
        return root
    }

    fun intent(context: Context): Intent = Intent(context, NativeLibraryActivity::class.java).apply {
        action = "LAUNCH_NATIVE_LIBRARY"
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
        putExtra("AppStartParameters", arrayOf("-r", TITLE_ID))
        putExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, true)
    }

    fun setSelected(context: Context, selected: Boolean) {
        context.getSharedPreferences("emucorev_library_ui", Context.MODE_PRIVATE)
            .edit().putBoolean("shelf_mode", selected).commit()
    }
}
