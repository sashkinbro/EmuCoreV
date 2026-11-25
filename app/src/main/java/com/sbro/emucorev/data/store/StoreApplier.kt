package com.sbro.emucorev.data.store

import android.content.Context
import android.graphics.Color
import android.net.Uri
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationFileStore
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TrophySoundRepository
import com.sbro.emucorev.data.theme.ThemePackageRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withContext
import java.io.File

enum class StoreApplyResult { APPLIED, NEEDS_PRO, NOT_INSTALLED, UNSUPPORTED, FAILED }

/** An installed Store controls skin, resolved to files. Rendering is up to the touch-overlay UI. */
data class ControlSkin(
    val dir: File,
    /** control id -> image. */
    val images: Map<String, File>,
    /** control id -> image shown while pressed (from the "<id>_pressed" entries). */
    val pressed: Map<String, File>,
    /** ARGB tint, or null for none. */
    val tint: Int?,
    /** 0..1. */
    val opacity: Float,
    /** Null = keep the user's press-effect preference. */
    val pressEffect: TouchControlPressEffect?
)

object StoreControlSkins {
    /** Loads the skin of installed controls package [packId]; null if missing, layout-only or unreadable. */
    fun load(context: Context, packId: String?): ControlSkin? {
        if (packId.isNullOrBlank() || !StoreInstalls.ID_PATTERN.matches(packId)) return null
        val dir = File(StoreInstalls.kindDirectory(context, StoreKind.CONTROLS), packId)
        return runCatching {
            val manifest = StoreInstalls.JSON.decodeFromString<ControlsManifest>(
                File(dir, StoreInstalls.CONTROLS_JSON).readText()
            )
            val skin = manifest.skin?.takeIf { it.images.isNotEmpty() } ?: return null
            val images = LinkedHashMap<String, File>()
            val pressed = LinkedHashMap<String, File>()
            skin.images.forEach { (controlId, name) ->
                val file = File(dir, name).takeIf { it.isFile && it.parentFile == dir } ?: return@forEach
                if (controlId.endsWith(PRESSED_SUFFIX)) {
                    pressed[controlId.removeSuffix(PRESSED_SUFFIX)] = file
                } else {
                    images[controlId] = file
                }
            }
            if (images.isEmpty()) return null
            ControlSkin(
                dir = dir,
                images = images,
                pressed = pressed,
                tint = skin.tint?.let { runCatching { Color.parseColor(it) }.getOrNull() },
                opacity = skin.opacity.coerceIn(0f, 1f),
                pressEffect = skin.pressEffect?.let { name ->
                    TouchControlPressEffect.entries.firstOrNull { it.name.equals(name, ignoreCase = true) }
                }
            )
        }.getOrNull()
    }

    private const val PRESSED_SUFFIX = "_pressed"
}

/**
 * Applies installed Store items through the existing preference/repository paths, and removes them.
 * No UI. Pro items are refused without Pro, like the built-in Pro theme.
 */
class StoreApplier(context: Context) {
    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private val installs = StoreInstalls.getInstance(appContext)

    suspend fun apply(item: StoreItem): StoreApplyResult = withContext(Dispatchers.IO) {
        try {
            val installed = installs.find(item.id)?.takeIf { it.kind == item.kind }
                ?: return@withContext StoreApplyResult.NOT_INSTALLED
            if (item.minAppVersionCode > StoreClient.ENGINE_VERSION_CODE) return@withContext StoreApplyResult.UNSUPPORTED
            if (item.requiresPro && !preferences.proUnlocked) return@withContext StoreApplyResult.NEEDS_PRO
            when (item.kind) {
                StoreKind.THEME -> applyTheme(item, installed)
                StoreKind.WALLPAPER -> applyWallpaper(installed)
                StoreKind.SOUNDS -> applySounds(item, installed)
                StoreKind.CONTROLS -> applyControls(installed)
                StoreKind.FONT -> applyFont(installed)
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            StoreApplyResult.FAILED
        }
    }

    /** Deletes the installed files and resets anything that pointed at them (applied theme, controls skin). */
    suspend fun remove(id: String): Boolean {
        val installed = installs.find(id) ?: return false
        when (installed.kind) {
            StoreKind.THEME -> preferences.clearThemePackage(id)
            StoreKind.CONTROLS -> if (preferences.touchControlSkinPack.first() == id) preferences.setTouchControlSkinPack(null)
            // The wallpaper, an applied font and an applied achievement sound are independent copies.
            StoreKind.SOUNDS, StoreKind.WALLPAPER, StoreKind.FONT -> Unit
        }
        return installs.remove(id)
    }

    private fun applyTheme(item: StoreItem, installed: InstalledStoreItem): StoreApplyResult {
        val pack = ThemePackageRepository.getInstance(appContext).find(installed.id)
            ?: return StoreApplyResult.NOT_INSTALLED
        if (!pack.isSupported) return StoreApplyResult.UNSUPPORTED
        val requiresPro = item.requiresPro || pack.spec.requiresPro
        return if (preferences.applyThemePackage(pack.id, requiresPro)) StoreApplyResult.APPLIED else StoreApplyResult.NEEDS_PRO
    }

    /** Imports the wallpaper image as the Home background (this app has no separate dim setting). */
    private fun applyWallpaper(installed: InstalledStoreItem): StoreApplyResult {
        val image = StorePackageInstaller.WALLPAPER_IMAGES.map { File(installed.directory, it) }.firstOrNull(File::isFile)
            ?: return StoreApplyResult.NOT_INSTALLED
        val imported = runCatching { CustomizationFileStore(appContext).importBackground(Uri.fromFile(image)) }
            .getOrElse { return StoreApplyResult.FAILED }
        CustomizationPreferences(appContext).use { it.setBackground(imported.path, imported.mimeType) }
        return StoreApplyResult.APPLIED
    }

    /**
     * This app has no UI click/menu/music sounds; the only part of a sound pack it can use is the achievement
     * (trophy) sound.
     */
    private fun applySounds(item: StoreItem, installed: InstalledStoreItem): StoreApplyResult {
        val source = StoreSoundPacks.file(appContext, installed.id, "achievement")
            ?: return StoreApplyResult.UNSUPPORTED
        // A private copy, so the choice survives removing the pack.
        val directory = File(appContext.filesDir, "achievements").apply { mkdirs() }
        val target = File(directory, "unlock_sound.${source.extension}")
        File(directory, "unlock_sound.wav").takeIf { it != target }?.delete()
        File(directory, "unlock_sound.ogg").takeIf { it != target }?.delete()
        source.copyTo(target, overwrite = true)
        return if (TrophySoundRepository(appContext).selectLocalFile(target, item.displayName("en"))) {
            StoreApplyResult.APPLIED
        } else {
            StoreApplyResult.FAILED
        }
    }

    private fun applyFont(installed: InstalledStoreItem): StoreApplyResult {
        val font = StoreFontPacks.file(appContext, installed.id) ?: return StoreApplyResult.NOT_INSTALLED
        val path = CustomizationFileStore(appContext).importFont(Uri.fromFile(font))
        CustomizationPreferences(appContext).use { it.setAppFont(AppFont.CUSTOM, path) }
        preferences.setStoreFont(installed.id, path)
        return StoreApplyResult.APPLIED
    }

    private fun applyControls(installed: InstalledStoreItem): StoreApplyResult {
        val manifest = StoreInstalls.JSON.decodeFromString<ControlsManifest>(
            File(installed.directory, StoreInstalls.CONTROLS_JSON).readText()
        )
        // Only the skin art is used here; button layouts are specific to the sibling apps' overlays.
        if (manifest.skin?.images?.isNotEmpty() != true) return StoreApplyResult.UNSUPPORTED
        preferences.setTouchControlSkinPack(installed.id)
        return StoreApplyResult.APPLIED
    }
}
