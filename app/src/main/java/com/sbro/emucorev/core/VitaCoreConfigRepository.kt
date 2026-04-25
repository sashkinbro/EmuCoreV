package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.BuildConfig
import java.io.File

data class VitaCoreConfig(
    val validationLayer: Boolean = false,
    val logActiveShaders: Boolean = false,
    val logUniforms: Boolean = false,
    val logCompatWarn: Boolean = false,
    val pstvMode: Boolean = false,
    val showInfoBar: Boolean = false,
    val showLiveAreaScreen: Boolean = false,
    val backendRenderer: String = "Vulkan",
    val customDriverName: String = "",
    val turboMode: Boolean = false,
    val highAccuracy: Boolean = false,
    val resolutionMultiplier: Float = 1.0f,
    val disableSurfaceSync: Boolean = true,
    val screenFilter: String = "Bilinear",
    val anisotropicFiltering: Int = 1,
    val textureCache: Boolean = true,
    val asyncPipelineCompilation: Boolean = true,
    val showCompileShaders: Boolean = false,
    val hashlessTextureCache: Boolean = false,
    val importTextures: Boolean = false,
    val exportTextures: Boolean = false,
    val exportAsPng: Boolean = true,
    val memoryMapping: String = "double-buffer",
    val fullscreenHdResPixelPerfect: Boolean = false,
    val performanceOverlay: Boolean = false,
    val performanceOverlayDetail: Int = 0,
    val performanceOverlayPosition: Int = 0,
    val enableGamepadOverlay: Boolean = true,
    val overlayShowTouchSwitch: Boolean = false,
    val overlayScale: Float = 0.9f,
    val overlayOpacity: Int = 100,
    val disableMotion: Boolean = false,
    val analogMultiplier: Float = 1.0f,
    val stretchDisplayArea: Boolean = false,
    val fpsHack: Boolean = false,
    val vSync: Boolean = true,
    val bootAppsFullScreen: Boolean = false,
    val audioBackend: String = "SDL",
    val audioVolume: Int = 100,
    val bgmVolume: Int = 100,
    val ngsEnable: Boolean = true,
    val showTouchpadCursor: Boolean = true,
    val sysButton: Int = 1,
    val sysLang: Int = 1,
    val cpuPoolSize: Int = 8,
    val modulesMode: Int = 0,
    val archiveLog: Boolean = false,
    val logLevel: Int = 4,
    val discordRichPresence: Boolean = true,
    val checkForUpdates: Boolean = true,
    val fileLoadingDelay: Int = 0,
    val shaderCache: Boolean = true,
    val spirvShader: Boolean = false,
    val psnSignedIn: Boolean = false,
    val httpEnable: Boolean = true,
    val colorSurfaceDebug: Boolean = false,
    val showShaderCacheWarn: Boolean = true
)

class VitaCoreConfigRepository(private val context: Context) {

    private val persistedKeys = setOf(
        "anisotropic-filtering",
        "archive-log",
        "async-pipeline-compilation",
        "audio-backend",
        "audio-volume",
        "backend-renderer",
        "bgm-volume",
        "boot-apps-full-screen",
        "check-for-updates",
        "color-surface-debug",
        "resolution-multiplier",
        "controller-analog-multiplier",
        "cpu-pool-size",
        "custom-driver-name",
        "disable-motion",
        "disable-surface-sync",
        "discord-rich-presence",
        "enable-gamepad-overlay",
        "export-as-png",
        "export-textures",
        "file-loading-delay",
        "fps-hack",
        "fullscreen_hd_res_pixel_perfect",
        "hashless-texture-cache",
        "high-accuracy",
        "http-enable",
        "import-textures",
        "log-active-shaders",
        "log-compat-warn",
        "log-level",
        "log-uniforms",
        "memory-mapping",
        "modules-mode",
        "ngs-enable",
        "overlay-opacity",
        "overlay-scale",
        "overlay-show-touch-switch",
        "performance-overlay",
        "performance-overlay-detail",
        "performance-overlay-position",
        "psn-signed-in",
        "pstv-mode",
        "screen-filter",
        "shader-cache",
        "show-compile-shaders",
        "show-info-bar",
        "show-live-area-screen",
        "show-shader-cache-warn",
        "show-touchpad-cursor",
        "spirv-shader",
        "stretch_the_display_area",
        "sys-button",
        "sys-lang",
        "texture-cache",
        "turbo-mode",
        "v-sync",
        "validation-layer"
    )

    private val configFile: File
        get() {
            val base = context.getExternalFilesDir(null) ?: context.filesDir
            return File(base, "config.yml")
        }

    fun load(): VitaCoreConfig {
        val defaults = defaultConfig()
        val values = readKeyValues()
        return VitaCoreConfig(
            validationLayer = values["validation-layer"]?.toBooleanStrictOrNull() ?: defaults.validationLayer,
            logActiveShaders = values["log-active-shaders"]?.toBooleanStrictOrNull() ?: defaults.logActiveShaders,
            logUniforms = values["log-uniforms"]?.toBooleanStrictOrNull() ?: defaults.logUniforms,
            logCompatWarn = values["log-compat-warn"]?.toBooleanStrictOrNull() ?: defaults.logCompatWarn,
            pstvMode = values["pstv-mode"]?.toBooleanStrictOrNull() ?: defaults.pstvMode,
            showInfoBar = values["show-info-bar"]?.toBooleanStrictOrNull() ?: defaults.showInfoBar,
            showLiveAreaScreen = false,
            backendRenderer = values["backend-renderer"] ?: defaults.backendRenderer,
            customDriverName = values["custom-driver-name"].sanitizeNullableString() ?: defaults.customDriverName,
            turboMode = values["turbo-mode"]?.toBooleanStrictOrNull() ?: defaults.turboMode,
            highAccuracy = values["high-accuracy"]?.toBooleanStrictOrNull() ?: defaults.highAccuracy,
            resolutionMultiplier = values["resolution-multiplier"]?.toFloatOrNull() ?: defaults.resolutionMultiplier,
            disableSurfaceSync = values["disable-surface-sync"]?.toBooleanStrictOrNull() ?: defaults.disableSurfaceSync,
            screenFilter = values["screen-filter"] ?: defaults.screenFilter,
            anisotropicFiltering = values["anisotropic-filtering"]?.toIntOrNull() ?: defaults.anisotropicFiltering,
            textureCache = values["texture-cache"]?.toBooleanStrictOrNull() ?: defaults.textureCache,
            asyncPipelineCompilation = values["async-pipeline-compilation"]?.toBooleanStrictOrNull() ?: defaults.asyncPipelineCompilation,
            showCompileShaders = values["show-compile-shaders"]?.toBooleanStrictOrNull() ?: defaults.showCompileShaders,
            hashlessTextureCache = values["hashless-texture-cache"]?.toBooleanStrictOrNull() ?: defaults.hashlessTextureCache,
            importTextures = values["import-textures"]?.toBooleanStrictOrNull() ?: defaults.importTextures,
            exportTextures = values["export-textures"]?.toBooleanStrictOrNull() ?: defaults.exportTextures,
            exportAsPng = values["export-as-png"]?.toBooleanStrictOrNull() ?: defaults.exportAsPng,
            memoryMapping = values["memory-mapping"] ?: defaults.memoryMapping,
            fullscreenHdResPixelPerfect = values["fullscreen_hd_res_pixel_perfect"]?.toBooleanStrictOrNull() ?: defaults.fullscreenHdResPixelPerfect,
            performanceOverlay = values["performance-overlay"]?.toBooleanStrictOrNull() ?: defaults.performanceOverlay,
            performanceOverlayDetail = values["performance-overlay-detail"]?.toIntOrNull() ?: defaults.performanceOverlayDetail,
            performanceOverlayPosition = values["performance-overlay-position"]?.toIntOrNull() ?: defaults.performanceOverlayPosition,
            enableGamepadOverlay = values["enable-gamepad-overlay"]?.toBooleanStrictOrNull() ?: defaults.enableGamepadOverlay,
            overlayShowTouchSwitch = values["overlay-show-touch-switch"]?.toBooleanStrictOrNull() ?: defaults.overlayShowTouchSwitch,
            overlayScale = values["overlay-scale"]?.toFloatOrNull() ?: defaults.overlayScale,
            overlayOpacity = values["overlay-opacity"]?.toIntOrNull() ?: defaults.overlayOpacity,
            disableMotion = values["disable-motion"]?.toBooleanStrictOrNull() ?: defaults.disableMotion,
            analogMultiplier = values["controller-analog-multiplier"]?.toFloatOrNull() ?: defaults.analogMultiplier,
            stretchDisplayArea = values["stretch_the_display_area"]?.toBooleanStrictOrNull() ?: defaults.stretchDisplayArea,
            fpsHack = values["fps-hack"]?.toBooleanStrictOrNull() ?: defaults.fpsHack,
            vSync = values["v-sync"]?.toBooleanStrictOrNull() ?: defaults.vSync,
            bootAppsFullScreen = values["boot-apps-full-screen"]?.toBooleanStrictOrNull() ?: defaults.bootAppsFullScreen,
            audioBackend = values["audio-backend"] ?: defaults.audioBackend,
            audioVolume = values["audio-volume"]?.toIntOrNull() ?: defaults.audioVolume,
            bgmVolume = values["bgm-volume"]?.toIntOrNull() ?: defaults.bgmVolume,
            ngsEnable = values["ngs-enable"]?.toBooleanStrictOrNull() ?: defaults.ngsEnable,
            showTouchpadCursor = values["show-touchpad-cursor"]?.toBooleanStrictOrNull() ?: defaults.showTouchpadCursor,
            sysButton = values["sys-button"]?.toIntOrNull() ?: defaults.sysButton,
            sysLang = values["sys-lang"]?.toIntOrNull() ?: defaults.sysLang,
            cpuPoolSize = values["cpu-pool-size"]?.toIntOrNull() ?: defaults.cpuPoolSize,
            modulesMode = values["modules-mode"]?.toIntOrNull() ?: defaults.modulesMode,
            archiveLog = values["archive-log"]?.toBooleanStrictOrNull() ?: defaults.archiveLog,
            logLevel = normalizeLogLevel(values["log-level"]?.toIntOrNull() ?: defaults.logLevel),
            discordRichPresence = values["discord-rich-presence"]?.toBooleanStrictOrNull() ?: defaults.discordRichPresence,
            checkForUpdates = values["check-for-updates"]?.toBooleanStrictOrNull() ?: defaults.checkForUpdates,
            fileLoadingDelay = values["file-loading-delay"]?.toIntOrNull() ?: defaults.fileLoadingDelay,
            shaderCache = values["shader-cache"]?.toBooleanStrictOrNull() ?: defaults.shaderCache,
            spirvShader = values["spirv-shader"]?.toBooleanStrictOrNull() ?: defaults.spirvShader,
            psnSignedIn = values["psn-signed-in"]?.toBooleanStrictOrNull() ?: defaults.psnSignedIn,
            httpEnable = values["http-enable"]?.toBooleanStrictOrNull() ?: defaults.httpEnable,
            colorSurfaceDebug = values["color-surface-debug"]?.toBooleanStrictOrNull() ?: defaults.colorSurfaceDebug,
            showShaderCacheWarn = values["show-shader-cache-warn"]?.toBooleanStrictOrNull() ?: defaults.showShaderCacheWarn
        )
    }

    fun ensureDefaultsPersisted(): VitaCoreConfig {
        migrateLegacyConfigIfNeeded()
        val config = load()
        val existingValues = readKeyValues()
        val shouldPersist = !configFile.exists() || persistedKeys.any { it !in existingValues }
        if (shouldPersist) {
            save(config)
        }
        return config
    }

    fun save(config: VitaCoreConfig) {
        val values = readKeyValues().toMutableMap()
        values["validation-layer"] = config.validationLayer.toString()
        values["log-active-shaders"] = config.logActiveShaders.toString()
        values["log-uniforms"] = config.logUniforms.toString()
        values["log-compat-warn"] = config.logCompatWarn.toString()
        values["pstv-mode"] = config.pstvMode.toString()
        values["show-info-bar"] = config.showInfoBar.toString()
        values["show-live-area-screen"] = false.toString()
        values["backend-renderer"] = config.backendRenderer
        values["custom-driver-name"] = config.customDriverName.sanitizeNullableString().orEmpty()
        values["turbo-mode"] = config.turboMode.toString()
        values["high-accuracy"] = config.highAccuracy.toString()
        values["resolution-multiplier"] = formatFloat(config.resolutionMultiplier)
        values["disable-surface-sync"] = config.disableSurfaceSync.toString()
        values["screen-filter"] = config.screenFilter
        values["anisotropic-filtering"] = config.anisotropicFiltering.toString()
        values["texture-cache"] = config.textureCache.toString()
        values["async-pipeline-compilation"] = config.asyncPipelineCompilation.toString()
        values["show-compile-shaders"] = config.showCompileShaders.toString()
        values["hashless-texture-cache"] = config.hashlessTextureCache.toString()
        values["import-textures"] = config.importTextures.toString()
        values["export-textures"] = config.exportTextures.toString()
        values["export-as-png"] = config.exportAsPng.toString()
        values["memory-mapping"] = config.memoryMapping
        values["fullscreen_hd_res_pixel_perfect"] = config.fullscreenHdResPixelPerfect.toString()
        values["performance-overlay"] = config.performanceOverlay.toString()
        values["performance-overlay-detail"] = config.performanceOverlayDetail.toString()
        values["performance-overlay-position"] = config.performanceOverlayPosition.toString()
        values["enable-gamepad-overlay"] = config.enableGamepadOverlay.toString()
        values["overlay-show-touch-switch"] = config.overlayShowTouchSwitch.toString()
        values["overlay-scale"] = formatFloat(config.overlayScale)
        values["overlay-opacity"] = config.overlayOpacity.toString()
        values["disable-motion"] = config.disableMotion.toString()
        values["controller-analog-multiplier"] = formatFloat(config.analogMultiplier)
        values["stretch_the_display_area"] = config.stretchDisplayArea.toString()
        values["fps-hack"] = config.fpsHack.toString()
        values["v-sync"] = config.vSync.toString()
        values["boot-apps-full-screen"] = config.bootAppsFullScreen.toString()
        values["audio-backend"] = config.audioBackend
        values["audio-volume"] = config.audioVolume.toString()
        values["bgm-volume"] = config.bgmVolume.toString()
        values["ngs-enable"] = config.ngsEnable.toString()
        values["show-touchpad-cursor"] = config.showTouchpadCursor.toString()
        values["sys-button"] = config.sysButton.toString()
        values["sys-lang"] = config.sysLang.toString()
        values["cpu-pool-size"] = config.cpuPoolSize.toString()
        values["modules-mode"] = config.modulesMode.toString()
        values["archive-log"] = config.archiveLog.toString()
        values["log-level"] = normalizeLogLevel(config.logLevel).toString()
        values["discord-rich-presence"] = config.discordRichPresence.toString()
        values["check-for-updates"] = config.checkForUpdates.toString()
        values["file-loading-delay"] = config.fileLoadingDelay.toString()
        values["shader-cache"] = config.shaderCache.toString()
        values["spirv-shader"] = config.spirvShader.toString()
        values["psn-signed-in"] = config.psnSignedIn.toString()
        values["http-enable"] = config.httpEnable.toString()
        values["color-surface-debug"] = config.colorSurfaceDebug.toString()
        values["show-shader-cache-warn"] = config.showShaderCacheWarn.toString()

        configFile.parentFile?.mkdirs()
        configFile.writeText(
            buildString {
                appendLine("# EmuCoreV overrides for the Vita3K core")
                values.toSortedMap().forEach { (key, value) ->
                    appendLine("$key: $value")
                }
            }
        )
    }

    private fun readKeyValues(): Map<String, String> {
        if (!configFile.exists()) return emptyMap()
        return configFile.readLines()
            .map(String::trim)
            .filter { it.isNotEmpty() && !it.startsWith("#") && ":" in it }
            .associate { line ->
                val index = line.indexOf(':')
                val key = line.substring(0, index).trim()
                val value = line.substring(index + 1).trim().trim('"')
                key to value
            }
    }

    private fun String?.sanitizeNullableString(): String? {
        val normalized = this?.trim()?.trim('"')?.takeIf(String::isNotBlank)
        return normalized?.takeUnless { it.equals("null", ignoreCase = true) }
    }

    private fun formatFloat(value: Float): String {
        val normalized = if (value % 1f == 0f) value.toInt().toString() else value.toString()
        return normalized
    }

    private fun normalizeLogLevel(level: Int): Int {
        val bounded = level.coerceIn(0, 6)
        return if (BuildConfig.DEBUG) bounded else bounded.coerceAtLeast(RELEASE_LOG_LEVEL)
    }

    private fun migrateLegacyConfigIfNeeded() {
        val legacy = File(EmulatorStorage.vitaRoot(context), "config.yml")
        val target = configFile
        if (legacy.exists() && !target.exists()) {
            runCatching {
                target.parentFile?.mkdirs()
                legacy.copyTo(target, overwrite = false)
                legacy.delete()
            }
        }
    }

    private fun defaultConfig(): VitaCoreConfig {
        val recommendedCpuPool = Runtime.getRuntime().availableProcessors().coerceIn(4, 10)
        return VitaCoreConfig(
            cpuPoolSize = recommendedCpuPool,
            logLevel = if (BuildConfig.DEBUG) DEBUG_LOG_LEVEL else RELEASE_LOG_LEVEL
        )
    }

    private companion object {
        private const val DEBUG_LOG_LEVEL = 2
        private const val RELEASE_LOG_LEVEL = 4
    }
}
