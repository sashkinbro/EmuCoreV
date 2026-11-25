package com.sbro.emucorev.ui.store

import android.app.Activity
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.data.store.StoreSoundPacks
import kotlinx.coroutines.Job
import java.io.File
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.hub.HubLocaleResolver
import com.sbro.emucorev.data.store.InstalledStoreItem
import com.sbro.emucorev.data.store.StoreApplier
import com.sbro.emucorev.data.store.StoreClient
import com.sbro.emucorev.data.store.StoreApplyResult
import com.sbro.emucorev.data.store.StoreInstalls
import com.sbro.emucorev.data.store.StoreItem
import com.sbro.emucorev.data.store.StoreKind
import com.sbro.emucorev.data.store.StorePackageInstaller
import com.sbro.emucorev.data.store.StoreRepository
import com.sbro.emucorev.data.store.StoreSnapshot
import com.sbro.emucorev.data.store.storeScriptForLanguage
import com.sbro.emucorev.ui.theme.ThemeMode
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.text.Normalizer
import java.util.Locale

enum class StoreTab(val key: String, val kind: StoreKind) {
    THEMES("themes", StoreKind.THEME),
    WALLPAPERS("wallpapers", StoreKind.WALLPAPER),
    SOUNDS("sounds", StoreKind.SOUNDS),
    CONTROLS("controls", StoreKind.CONTROLS),
    FONTS("fonts", StoreKind.FONT);

    companion object {
        fun fromKey(key: String?): StoreTab = entries.firstOrNull { it.key == key } ?: THEMES
    }
}

/** LANGUAGE ("Supports my language") applies to the Fonts tab only. */
enum class StoreAccessFilter { ALL, FREE, PRO, INSTALLED, LANGUAGE }

enum class StoreSort { FEATURED, NEWEST, NAME }

enum class StoreItemStatus { NONE, INSTALLED, APPLIED, UPDATE, NEEDS_NEWER_APP }

enum class StoreActionPhase { DOWNLOADING, INSTALLING, APPLYING, REMOVING }

/** A running action on one item. [progress] is 0..1 while downloading, otherwise unused. */
data class StoreAction(val phase: StoreActionPhase, val progress: Float = 0f)

/** Sound-pack preview state: [part] is music, click, menu or achievement; [loading] covers the download. */
data class SoundPreview(
    val itemId: String? = null,
    val part: String? = null,
    val loading: Boolean = false,
    val playing: Boolean = false,
    val progress: Float = 0f
)

private const val MAX_PREVIEW_PACKS = 5
private val PREVIEW_KEYS = mapOf(
    "music" to listOf("music"),
    "click" to listOf("click"),
    "menu" to listOf("menu_open", "menu_select", "menu_close"),
    "achievement" to listOf("achievement")
)

internal fun StoreItem.statusFor(installed: InstalledStoreItem?, applied: Boolean): StoreItemStatus = when {
    minAppVersionCode > StoreClient.ENGINE_VERSION_CODE -> StoreItemStatus.NEEDS_NEWER_APP
    installed == null -> StoreItemStatus.NONE
    installed.version < version -> StoreItemStatus.UPDATE
    applied -> StoreItemStatus.APPLIED
    else -> StoreItemStatus.INSTALLED
}

private fun String.normalizedStoreText(): String = Normalizer.normalize(this, Normalizer.Form.NFKC)
    .lowercase(Locale.ROOT)
    .replace(Regex("[\\p{Punct}\\s]+"), " ")
    .trim()

/** Applies tab kind, access filter, category, search and sort. */
internal fun filterStoreItems(
    items: List<StoreItem>,
    kind: StoreKind,
    access: StoreAccessFilter,
    categoryId: String?,
    query: String,
    sort: StoreSort,
    installedIds: Set<String>,
    locale: String,
    languageScript: String? = null
): List<StoreItem> {
    val normalizedQuery = query.normalizedStoreText()
    val filtered = items.asSequence()
        .filter { it.kind == kind }
        .filter {
            when (access) {
                StoreAccessFilter.ALL -> true
                StoreAccessFilter.FREE -> !it.requiresPro
                StoreAccessFilter.PRO -> it.requiresPro
                StoreAccessFilter.INSTALLED -> it.id in installedIds
                StoreAccessFilter.LANGUAGE -> languageScript != null && languageScript in it.scripts()
            }
        }
        .filter { categoryId == null || categoryId in it.categories }
        .filter {
            normalizedQuery.isBlank() || buildString {
                append(it.displayName(locale)); append(' ')
                append(it.displayDescription(locale)); append(' ')
                append(it.tags.joinToString(" "))
            }.normalizedStoreText().contains(normalizedQuery)
        }
        .toList()
    return when (sort) {
        StoreSort.FEATURED -> filtered.sortedWith(
            compareByDescending<StoreItem> { it.featured }
                .thenByDescending { it.publishedAt }
                .thenBy { it.id }
        )
        StoreSort.NEWEST -> filtered.sortedWith(
            compareByDescending<StoreItem> { it.publishedAt }.thenBy { it.id }
        )
        StoreSort.NAME -> filtered.sortedWith(
            compareBy<StoreItem, String>(String.CASE_INSENSITIVE_ORDER) { it.displayName(locale) }.thenBy { it.id }
        )
    }
}

class StoreViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = StoreRepository.get(application)
    private val installs = StoreInstalls.getInstance(application)
    private val installer = StorePackageInstaller.getInstance(application)
    private val applier = StoreApplier(application)
    private val preferences = AppPreferences(application)
    private val proPurchaseManager = ProPurchaseManager.getInstance(application)

    /** The Hub/Store catalog language: the device language resolved to a supported catalog locale. */
    val locale: String = HubLocaleResolver.current() // the app language (AppLocaleManager sets the default), not the device one

    val catalog: StateFlow<StoreSnapshot?> = repository.catalog
    val installed: StateFlow<Map<String, InstalledStoreItem>> = installs.installed

    /** The font script the current app language needs, matched by the Fonts "Supports my language" filter. */
    val languageScript: String = storeScriptForLanguage(locale)

    /** The Store font sitting in the custom-font slot, while that font is the selected one. */
    private val customizationPreferences = CustomizationPreferences(application)
    private val appliedFontId = combine(customizationPreferences.settings, preferences.storeFont) { settings, font ->
        font?.takeIf { (_, path) -> settings.appFont == AppFont.CUSTOM && settings.customFontPath == path }?.first
    }

    /** Ids of items that are currently in use: the applied theme, controls skin, sound pack(s) and font. */
    val appliedIds: StateFlow<Set<String>> = combine(
        preferences.themeModeFlow,
        preferences.themePackageIdFlow,
        preferences.touchControlSkinPack,
        appliedFontId
    ) { mode, themeId, skinId, fontId ->
        buildSet {
            if (mode == ThemeMode.PACKAGE && themeId != null) add(themeId)
            if (skinId != null) add(skinId)
            if (fontId != null) add(fontId)
        }
    }.stateIn(viewModelScope, SharingStarted.Eagerly, emptySet())

    val isPro: StateFlow<Boolean> = proPurchaseManager.state
        .map { it.isProUnlocked }
        .stateIn(viewModelScope, SharingStarted.Eagerly, proPurchaseManager.state.value.isProUnlocked)

    /** Price of the base Pro tier when the billing client has it; shown on the Pro sheet. */
    val proPrice: StateFlow<String?> = proPurchaseManager.state
        .map { it.productPrice }
        .stateIn(viewModelScope, SharingStarted.Eagerly, proPurchaseManager.state.value.productPrice)

    private val _refreshing = MutableStateFlow(false)
    val refreshing: StateFlow<Boolean> = _refreshing.asStateFlow()
    private val _offline = MutableStateFlow(false)
    val offline: StateFlow<Boolean> = _offline.asStateFlow()

    private val _actions = MutableStateFlow<Map<String, StoreAction>>(emptyMap())
    val actions: StateFlow<Map<String, StoreAction>> = _actions.asStateFlow()

    private val _messages = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val messages: SharedFlow<String> = _messages.asSharedFlow()

    init {
        refresh(force = false)
    }

    fun refresh(force: Boolean) {
        if (_refreshing.value) return
        _refreshing.value = true
        viewModelScope.launch {
            try {
                _offline.value = repository.refresh(locale, force).isFailure
            } finally {
                _refreshing.value = false
            }
        }
    }

    fun purchasePro(activity: Activity) {
        proPurchaseManager.purchase(activity, source = "store")
    }

    fun status(item: StoreItem, installed: Map<String, InstalledStoreItem>, applied: Set<String>): StoreItemStatus =
        item.statusFor(installed[item.id]?.takeIf { it.kind == item.kind }, item.id in applied)

    /** Downloads, verifies and installs [item]; re-applies it afterwards when it was in use (an update). */
    fun install(item: StoreItem) {
        runAction(item.id, StoreAction(StoreActionPhase.DOWNLOADING)) {
            val wasApplied = item.id in appliedIds.value
            var lastReported = -1f
            val file = try {
                repository.download(item) { progress ->
                    if (progress - lastReported >= 0.01f || progress >= 1f) {
                        lastReported = progress
                        setAction(item.id, StoreAction(StoreActionPhase.DOWNLOADING, progress.coerceIn(0f, 1f)))
                    }
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                post(R.string.store_msg_download_failed)
                return@runAction
            }
            setAction(item.id, StoreAction(StoreActionPhase.INSTALLING))
            val result = installer.install(item, file)
            if (result.isFailure) {
                post(R.string.store_msg_install_failed)
                return@runAction
            }
            if (wasApplied) {
                setAction(item.id, StoreAction(StoreActionPhase.APPLYING))
                applier.apply(item)
            }
            post(R.string.store_msg_installed)
        }
    }

    /** Applies an installed item. Callers show the Pro sheet themselves before calling for locked items. */
    fun apply(item: StoreItem) {
        runAction(item.id, StoreAction(StoreActionPhase.APPLYING)) {
            when (applier.apply(item)) {
                StoreApplyResult.APPLIED -> post(R.string.store_msg_applied)
                StoreApplyResult.NEEDS_PRO -> post(R.string.store_pro_sheet_title)
                StoreApplyResult.NOT_INSTALLED -> post(R.string.store_msg_not_installed)
                StoreApplyResult.UNSUPPORTED -> post(R.string.hub_theme_requires_newer_app)
                StoreApplyResult.FAILED -> post(R.string.store_msg_apply_failed)
            }
            installs.refresh()
        }
    }

    fun remove(item: StoreItem) = remove(item.id)

    /** Removes an installed item by id, also for items that have left the catalog. */
    fun remove(id: String) {
        runAction(id, StoreAction(StoreActionPhase.REMOVING)) {
            if (applier.remove(id)) post(R.string.store_msg_removed)
            else post(R.string.store_msg_apply_failed)
        }
    }

    // region Sound-pack preview

    private val _preview = MutableStateFlow(SoundPreview())
    val preview: StateFlow<SoundPreview> = _preview.asStateFlow()
    private var previewJob: Job? = null
    private val previewPlayer = StoreSoundPreviewPlayer { finishPreview() }

    /** Starts the [part] preview of a sound pack, or stops it when it is already the one playing. */
    fun togglePreview(item: StoreItem, installedItem: InstalledStoreItem?, part: String) {
        if (item.kind != StoreKind.SOUNDS || part !in PREVIEW_KEYS) return
        val current = _preview.value
        if (current.itemId == item.id && current.part == part) {
            stopPreview()
            return
        }
        if (item.id in _actions.value) return
        stopPreview()
        _preview.value = SoundPreview(item.id, part, loading = false)
        previewJob = viewModelScope.launch {
            val app = getApplication<Application>()
            val directory = try {
                if (installedItem != null) {
                    File(StoreInstalls.kindDirectory(app, StoreKind.SOUNDS), item.id)
                } else {
                    preparePreviewDirectory(item)
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                _preview.value = SoundPreview()
                post(R.string.store_msg_preview_failed)
                return@launch
            }
            val files = PREVIEW_KEYS.getValue(part).mapNotNull { StoreSoundPacks.fileIn(directory, it) }
            if (files.isEmpty()) {
                _preview.value = SoundPreview()
                post(R.string.store_msg_preview_failed)
                return@launch
            }
            _preview.value = SoundPreview(item.id, part, loading = false, playing = true)
            previewPlayer.play(files, loop = part == "music", volume = if (part == "music") 0.6f else 0.9f)
        }
    }

    fun stopPreview() {
        previewJob?.cancel()
        previewJob = null
        previewPlayer.stop()
        finishPreview()
    }

    private fun finishPreview() {
        if (_preview.value != SoundPreview()) _preview.value = SoundPreview()
    }

    /** Unpacked preview files of [item] under cacheDir/store-preview; downloads and unpacks them on first use. */
    private suspend fun preparePreviewDirectory(item: StoreItem): File {
        val root = File(getApplication<Application>().cacheDir, "store-preview")
        val directory = File(root, "${item.id}-${item.version}")
        if (StoreSoundPacks.KEYS.any { StoreSoundPacks.fileIn(directory, it) != null }) {
            directory.setLastModified(System.currentTimeMillis())
            return directory
        }
        var lastReported = -1f
        _preview.update { it.copy(loading = true, progress = 0f) }
        val file = repository.download(item) { progress ->
            if (progress - lastReported >= 0.01f || progress >= 1f) {
                lastReported = progress
                _preview.update { it.copy(progress = progress.coerceIn(0f, 1f)) }
            }
        }
        try {
            installer.extractSoundPreview(item, file, directory).getOrThrow()
        } finally {
            file.delete()
        }
        _preview.update { it.copy(loading = false) }
        // Keep only the most recently previewed packs.
        root.listFiles()?.filter { it.isDirectory && it != directory }
            ?.sortedByDescending { it.lastModified() }
            ?.drop(MAX_PREVIEW_PACKS - 1)
            ?.forEach { it.deleteRecursively() }
        return directory
    }

    override fun onCleared() {
        previewPlayer.stop()
        customizationPreferences.close()
        super.onCleared()
    }

    // endregion

    private fun setAction(id: String, action: StoreAction) {
        _actions.update { it + (id to action) }
    }

    private fun post(resId: Int) {
        _messages.tryEmit(getApplication<Application>().getString(resId))
    }

    private fun runAction(id: String, first: StoreAction, block: suspend () -> Unit) {
        if (id in _actions.value) return
        setAction(id, first)
        viewModelScope.launch {
            try {
                block()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Throwable) {
                post(R.string.store_msg_install_failed)
            } finally {
                _actions.update { it - id }
            }
        }
    }
}
