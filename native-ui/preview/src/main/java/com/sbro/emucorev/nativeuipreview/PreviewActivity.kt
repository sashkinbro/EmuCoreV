package com.sbro.emucorev.nativeuipreview

import android.content.res.AssetManager
import com.sbro.emucorev.nativeui.BundledImages
import com.sbro.emucorev.nativeui.shell.NativeShellActivity
import java.io.File

/** Development host: the native shell over the local fixture catalog. */
class PreviewActivity : NativeShellActivity() {
    /**
     * The preview has no installed emulator: push a firmware tree (vs0, sa0, ...) to
     * Android/data/com.sbro.emucorev.nativeuipreview/files/vita and the pack is built from it.
     */
    override fun firmwareRoot(): File = File(getExternalFilesDir(null), "vita")

    override fun loadCatalog(): List<BundledImages.HomeCatalogSource> {
        val root = File(noBackupFilesDir, "home-fixture")
        if (!File(root, "catalog.json").isFile) {
            root.deleteRecursively()
            copyAssets(assets, "home-fixture", root)
        }
        return BundledImages.Fixture.fixtureCatalog(root)
    }

    private fun copyAssets(assets: AssetManager, path: String, target: File) {
        val children = assets.list(path).orEmpty()
        if (children.isEmpty()) {
            target.parentFile?.mkdirs()
            assets.open(path).use { input -> target.outputStream().use { input.copyTo(it) } }
            return
        }
        target.mkdirs()
        children.forEach { copyAssets(assets, "$path/$it", File(target, it)) }
    }
}
