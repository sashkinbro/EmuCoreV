package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.BuildConfig
import java.io.File

// Defaults follow upstream Vita3K unless EmuCoreV needs a mobile-safe runtime
// override in its own layer. The vanilla core stays untouched.
data class VitaCoreConfig(
    val validationLayer: Boolean = false,
    val logActiveShaders: Boolean = false,
    val logUniforms: Boolean = false,
    val logCompatWarn: Boolean = false,
    val pstvMode: Boolean = false,
    val showInfoBar: Boolean = false,
    val showLiveAreaScreen: Boolean = false,
    val useAngle: Boolean = false,
    val backendRenderer: String = "Vulkan",
    val customDriverName: String = "",
    val turboMode: Boolean = false,
    val highAccuracy: Boolean = false,
    val resolutionMultiplier: Float = 1.0f,
    val disableSurfaceSync: Boolean = false,
    val screenFilter: String = "Bilinear",
    val anisotropicFiltering: Int = 1,
    val textureCache: Boolean = true,
    val asyncPipelineCompilation: Boolean = true,
    val showCompileShaders: Boolean = true,
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
    val touchHaptics: Boolean = true,
    val touchHapticsPreset: Int = TOUCH_HAPTICS_PRESET_BALANCED,
    val touchHapticsStrength: Int = 60,
    val gyroMode: Int = GYRO_MODE_OFF,
    val gyroSensitivity: Int = 100,
    val gyroSmoothing: Int = 45,
    val gyroInvertX: Boolean = false,
    val gyroInvertY: Boolean = false,
    val analogMultiplier: Float = 1.0f,
    val gamepadDeadzone: Float = 0.15f,
    val gamepadTriggerThreshold: Float = 0.12f,
    val gamepadButtonProfile: String = GAMEPAD_PROFILE_STANDARD,
    val gamepadVibration: Boolean = true,
    val gamepadVibrationStrength: Int = 100,
    val deviceVibrationFallback: Boolean = true,
    val gamepadSwapSticks: Boolean = false,
    val gamepadInvertLeftX: Boolean = false,
    val gamepadInvertLeftY: Boolean = false,
    val gamepadInvertRightX: Boolean = false,
    val gamepadInvertRightY: Boolean = false,
    val stretchDisplayArea: Boolean = false,
    val fpsHack: Boolean = false,
    val frameLimit: Int = FrameLimit.UNLIMITED,
    val vSync: Boolean = true,
    val bootAppsFullScreen: Boolean = false,
    val audioBackend: String = "SDL",
    val audioVolume: Int = 100,
    val bgmVolume: Int = 100,
    val ngsEnable: Boolean = true,
    val showTouchpadCursor: Boolean = true,
    val sysButton: Int = 1,
    val sysLang: Int = 1,
    val cpuPoolSize: Int = 10,
    val modulesMode: Int = 0,
    val archiveLog: Boolean = false,
    val logLevel: Int = if (BuildConfig.DEBUG) 3 else 6,
    val discordRichPresence: Boolean = false,
    val checkForUpdates: Boolean = false,
    val fileLoadingDelay: Int = 0,
    val shaderCache: Boolean = true,
    val spirvShader: Boolean = false,
    val psnSignedIn: Boolean = false,
    val httpEnable: Boolean = true,
    val colorSurfaceDebug: Boolean = false,
    val showShaderCacheWarn: Boolean = true,
    // Camera — defaults match upstream Vita3K (type 2 = "real camera").
    val frontCameraType: Int = 2,
    val frontCameraId: String = "",
    val frontCameraImage: String = "",
    val frontCameraColor: Long = 0L,
    val backCameraType: Int = 2,
    val backCameraId: String = "",
    val backCameraImage: String = "",
    val backCameraColor: Long = 0L,
    // Misc upstream-aligned settings.
    val screenshotFormat: Int = SCREENSHOT_FORMAT_JPEG,
    val showWelcome: Boolean = true,
    val warnMissingFirmware: Boolean = true
) {
    companion object {
        const val CAMERA_SOURCE_SOLID: Int = 0
        const val CAMERA_SOURCE_IMAGE: Int = 1
        const val CAMERA_SOURCE_REAL: Int = 2

        const val SCREENSHOT_FORMAT_NONE: Int = 0
        const val SCREENSHOT_FORMAT_JPEG: Int = 1
        const val SCREENSHOT_FORMAT_PNG: Int = 2

        const val GAMEPAD_PROFILE_STANDARD: String = "standard"
        const val GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE: String = "swap-cross-circle"
        const val GAMEPAD_PROFILE_NINTENDO_FACE: String = "nintendo-face"

        const val TOUCH_HAPTICS_PRESET_SOFT: Int = 0
        const val TOUCH_HAPTICS_PRESET_BALANCED: Int = 1
        const val TOUCH_HAPTICS_PRESET_CRISP: Int = 2
        const val TOUCH_HAPTICS_PRESET_STRONG: Int = 3

        const val GYRO_MODE_OFF: Int = 0
        const val GYRO_MODE_AIM: Int = 1
        const val GYRO_MODE_STEERING: Int = 2
    }
}

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
        "touch-haptics",
        "touch-haptics-preset",
        "touch-haptics-strength",
        "gyro-mode",
        "gyro-sensitivity",
        "gyro-smoothing",
        "gyro-invert-x",
        "gyro-invert-y",
        "disable-surface-sync",
        "discord-rich-presence",
        "enable-gamepad-overlay",
        "export-as-png",
        "export-textures",
        "file-loading-delay",
        "fps-hack",
        "frame-limit",
        "gamepad-button-profile",
        "gamepad-deadzone",
        "gamepad-invert-left-x",
        "gamepad-invert-left-y",
        "gamepad-invert-right-x",
        "gamepad-invert-right-y",
        "gamepad-swap-sticks",
        "gamepad-trigger-threshold",
        "gamepad-vibration",
        "gamepad-vibration-strength",
        "device-vibration-fallback",
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
        "use-angle",
        "v-sync",
        "validation-layer",
        "front-camera-type",
        "front-camera-id",
        "front-camera-image",
        "front-camera-color",
        "back-camera-type",
        "back-camera-id",
        "back-camera-image",
        "back-camera-color",
        "screenshot-format",
        "show-welcome",
        "warn-missing-firmware"
    )

    private val configFile: File
        get() {
            val base = EmulatorStorage.runtimeRoot(context)
            return File(base, "config.yml")
        }

    fun load(): VitaCoreConfig {
        val defaults = defaultConfig()
        val values = readKeyValues()
        return normalizeForBuild(
            VitaCoreConfig(
                validationLayer = values["validation-layer"]?.toBooleanStrictOrNull() ?: defaults.validationLayer,
                logActiveShaders = values["log-active-shaders"]?.toBooleanStrictOrNull() ?: defaults.logActiveShaders,
                logUniforms = values["log-uniforms"]?.toBooleanStrictOrNull() ?: defaults.logUniforms,
                logCompatWarn = values["log-compat-warn"]?.toBooleanStrictOrNull() ?: defaults.logCompatWarn,
                pstvMode = values["pstv-mode"]?.toBooleanStrictOrNull() ?: defaults.pstvMode,
                showInfoBar = values["show-info-bar"]?.toBooleanStrictOrNull() ?: defaults.showInfoBar,
                showLiveAreaScreen = false,
                useAngle = values["use-angle"]?.toBooleanStrictOrNull() ?: defaults.useAngle,
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
                touchHaptics = values["touch-haptics"]?.toBooleanStrictOrNull() ?: defaults.touchHaptics,
                touchHapticsPreset = values["touch-haptics-preset"]?.toIntOrNull() ?: defaults.touchHapticsPreset,
                touchHapticsStrength = values["touch-haptics-strength"]?.toIntOrNull() ?: defaults.touchHapticsStrength,
                gyroMode = values["gyro-mode"]?.toIntOrNull() ?: defaults.gyroMode,
                gyroSensitivity = values["gyro-sensitivity"]?.toIntOrNull() ?: defaults.gyroSensitivity,
                gyroSmoothing = values["gyro-smoothing"]?.toIntOrNull() ?: defaults.gyroSmoothing,
                gyroInvertX = values["gyro-invert-x"]?.toBooleanStrictOrNull() ?: defaults.gyroInvertX,
                gyroInvertY = values["gyro-invert-y"]?.toBooleanStrictOrNull() ?: defaults.gyroInvertY,
                analogMultiplier = values["controller-analog-multiplier"]?.toFloatOrNull() ?: defaults.analogMultiplier,
                gamepadDeadzone = values["gamepad-deadzone"]?.toFloatOrNull() ?: defaults.gamepadDeadzone,
                gamepadTriggerThreshold = values["gamepad-trigger-threshold"]?.toFloatOrNull() ?: defaults.gamepadTriggerThreshold,
                gamepadButtonProfile = normalizeGamepadProfile(values["gamepad-button-profile"] ?: defaults.gamepadButtonProfile),
                gamepadVibration = values["gamepad-vibration"]?.toBooleanStrictOrNull() ?: defaults.gamepadVibration,
                gamepadVibrationStrength = values["gamepad-vibration-strength"]?.toIntOrNull() ?: defaults.gamepadVibrationStrength,
                deviceVibrationFallback = values["device-vibration-fallback"]?.toBooleanStrictOrNull() ?: defaults.deviceVibrationFallback,
                gamepadSwapSticks = values["gamepad-swap-sticks"]?.toBooleanStrictOrNull() ?: defaults.gamepadSwapSticks,
                gamepadInvertLeftX = values["gamepad-invert-left-x"]?.toBooleanStrictOrNull() ?: defaults.gamepadInvertLeftX,
                gamepadInvertLeftY = values["gamepad-invert-left-y"]?.toBooleanStrictOrNull() ?: defaults.gamepadInvertLeftY,
                gamepadInvertRightX = values["gamepad-invert-right-x"]?.toBooleanStrictOrNull() ?: defaults.gamepadInvertRightX,
                gamepadInvertRightY = values["gamepad-invert-right-y"]?.toBooleanStrictOrNull() ?: defaults.gamepadInvertRightY,
                stretchDisplayArea = values["stretch_the_display_area"]?.toBooleanStrictOrNull() ?: defaults.stretchDisplayArea,
                fpsHack = values["fps-hack"]?.toBooleanStrictOrNull() ?: defaults.fpsHack,
                frameLimit = FrameLimit.normalize(values["frame-limit"]?.toIntOrNull() ?: defaults.frameLimit),
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
                checkForUpdates = false,
                fileLoadingDelay = values["file-loading-delay"]?.toIntOrNull() ?: defaults.fileLoadingDelay,
                shaderCache = values["shader-cache"]?.toBooleanStrictOrNull() ?: defaults.shaderCache,
                spirvShader = values["spirv-shader"]?.toBooleanStrictOrNull() ?: defaults.spirvShader,
                psnSignedIn = values["psn-signed-in"].toBooleanLikeOrNull() ?: defaults.psnSignedIn,
                httpEnable = values["http-enable"]?.toBooleanStrictOrNull() ?: defaults.httpEnable,
                colorSurfaceDebug = values["color-surface-debug"]?.toBooleanStrictOrNull() ?: defaults.colorSurfaceDebug,
                showShaderCacheWarn = values["show-shader-cache-warn"]?.toBooleanStrictOrNull() ?: defaults.showShaderCacheWarn,
                frontCameraType = values["front-camera-type"]?.toIntOrNull() ?: defaults.frontCameraType,
                frontCameraId = values["front-camera-id"].sanitizeNullableString().orEmpty(),
                frontCameraImage = values["front-camera-image"].sanitizeNullableString().orEmpty(),
                frontCameraColor = values["front-camera-color"]?.toLongOrNull() ?: defaults.frontCameraColor,
                backCameraType = values["back-camera-type"]?.toIntOrNull() ?: defaults.backCameraType,
                backCameraId = values["back-camera-id"].sanitizeNullableString().orEmpty(),
                backCameraImage = values["back-camera-image"].sanitizeNullableString().orEmpty(),
                backCameraColor = values["back-camera-color"]?.toLongOrNull() ?: defaults.backCameraColor,
                screenshotFormat = values["screenshot-format"]?.toIntOrNull() ?: defaults.screenshotFormat,
                showWelcome = values["show-welcome"]?.toBooleanStrictOrNull() ?: defaults.showWelcome,
                warnMissingFirmware = values["warn-missing-firmware"]?.toBooleanStrictOrNull() ?: defaults.warnMissingFirmware
            )
        )
    }

    fun ensureDefaultsPersisted(): VitaCoreConfig {
        migrateLegacyConfigIfNeeded()
        val existingValues = readKeyValues()
        val storedSchemaVersion = existingValues[SCHEMA_VERSION_KEY]?.toIntOrNull() ?: 0
        val schemaOutdated = configFile.exists() && storedSchemaVersion < CONFIG_SCHEMA_VERSION
        val config = applyMigrations(load(), schemaOutdated)
        val shouldPersist = !configFile.exists() ||
            persistedKeys.any { it !in existingValues } ||
            schemaOutdated ||
            storedSchemaVersion != CONFIG_SCHEMA_VERSION ||
            releaseValuesNeedNormalization(existingValues)
        if (shouldPersist) {
            save(config)
        }
        return config
    }

    private fun applyMigrations(loaded: VitaCoreConfig, schemaOutdated: Boolean): VitaCoreConfig {
        if (!schemaOutdated) return loaded
        // Old EmuCoreV builds shipped non-upstream defaults that caused frozen
        // launches and weaker CPU scheduling. Snap these back to vanilla Vita3K
        // values whenever a stale config.yml is detected.
        // NOTE: User-facing preferences (touchHaptics, touchHapticsPreset,
        // touchHapticsStrength, gamepadVibration, etc.) are intentionally NOT
        // reset here — the user's choices must survive schema upgrades.
        return loaded.copy(
            showCompileShaders = VitaCoreConfig().showCompileShaders,
            cpuPoolSize = VitaCoreConfig().cpuPoolSize,
            disableSurfaceSync = false,
            validationLayer = false,
            discordRichPresence = false,
            psnSignedIn = false,
            logLevel = defaultConfig().logLevel
        )
    }

    fun save(inputConfig: VitaCoreConfig) = synchronized(CONFIG_IO_LOCK) {
        val config = normalizeForBuild(inputConfig)
        val values = readKeyValues().toMutableMap()
        values["validation-layer"] = config.validationLayer.toString()
        values["log-active-shaders"] = config.logActiveShaders.toString()
        values["log-uniforms"] = config.logUniforms.toString()
        values["log-compat-warn"] = config.logCompatWarn.toString()
        values["pstv-mode"] = config.pstvMode.toString()
        values["show-info-bar"] = config.showInfoBar.toString()
        values["show-live-area-screen"] = false.toString()
        values["use-angle"] = config.useAngle.toString()
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
        values["touch-haptics"] = config.touchHaptics.toString()
        values["touch-haptics-preset"] = config.touchHapticsPreset.toString()
        values["touch-haptics-strength"] = config.touchHapticsStrength.toString()
        values["gyro-mode"] = config.gyroMode.toString()
        values["gyro-sensitivity"] = config.gyroSensitivity.toString()
        values["gyro-smoothing"] = config.gyroSmoothing.toString()
        values["gyro-invert-x"] = config.gyroInvertX.toString()
        values["gyro-invert-y"] = config.gyroInvertY.toString()
        values["controller-analog-multiplier"] = formatFloat(config.analogMultiplier)
        values["gamepad-deadzone"] = formatFloat(config.gamepadDeadzone)
        values["gamepad-trigger-threshold"] = formatFloat(config.gamepadTriggerThreshold)
        values["gamepad-button-profile"] = normalizeGamepadProfile(config.gamepadButtonProfile)
        values["gamepad-vibration"] = config.gamepadVibration.toString()
        values["gamepad-vibration-strength"] = config.gamepadVibrationStrength.toString()
        values["device-vibration-fallback"] = config.deviceVibrationFallback.toString()
        values["gamepad-swap-sticks"] = config.gamepadSwapSticks.toString()
        values["gamepad-invert-left-x"] = config.gamepadInvertLeftX.toString()
        values["gamepad-invert-left-y"] = config.gamepadInvertLeftY.toString()
        values["gamepad-invert-right-x"] = config.gamepadInvertRightX.toString()
        values["gamepad-invert-right-y"] = config.gamepadInvertRightY.toString()
        values["stretch_the_display_area"] = config.stretchDisplayArea.toString()
        values["fps-hack"] = config.fpsHack.toString()
        values["frame-limit"] = FrameLimit.normalize(config.frameLimit).toString()
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
        values["check-for-updates"] = false.toString()
        values["file-loading-delay"] = config.fileLoadingDelay.toString()
        values["shader-cache"] = config.shaderCache.toString()
        values["spirv-shader"] = config.spirvShader.toString()
        values["psn-signed-in"] = if (config.psnSignedIn) "1" else "0"
        values["http-enable"] = config.httpEnable.toString()
        values["color-surface-debug"] = config.colorSurfaceDebug.toString()
        values["show-shader-cache-warn"] = config.showShaderCacheWarn.toString()

        values["front-camera-type"] = config.frontCameraType.toString()
        values["front-camera-id"] = config.frontCameraId
        values["front-camera-image"] = config.frontCameraImage
        values["front-camera-color"] = config.frontCameraColor.toString()
        values["back-camera-type"] = config.backCameraType.toString()
        values["back-camera-id"] = config.backCameraId
        values["back-camera-image"] = config.backCameraImage
        values["back-camera-color"] = config.backCameraColor.toString()
        values["screenshot-format"] = config.screenshotFormat.toString()
        values["show-welcome"] = config.showWelcome.toString()
        values["warn-missing-firmware"] = config.warnMissingFirmware.toString()

        values[SCHEMA_VERSION_KEY] = CONFIG_SCHEMA_VERSION.toString()
        dropEmptyUpstreamSequenceValues(values)

        val content = buildString {
            appendLine("# EmuCoreV overrides for the Vita3K core")
            values.toSortedMap().forEach { (key, value) ->
                appendLine("$key: ${formatYamlScalar(value)}")
            }
        }
        AtomicTextFile.write(configFile, content)
    }

    fun resetToDefaults(): VitaCoreConfig {
        val config = normalizeForBuild(defaultConfig())
        save(config)
        return config
    }

    private fun readKeyValues(): Map<String, String> = synchronized(CONFIG_IO_LOCK) {
        if (!configFile.exists()) return emptyMap()
        configFile.readLines()
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

    private fun String?.toBooleanLikeOrNull(): Boolean? {
        return when (this?.trim()?.lowercase()) {
            "true", "1" -> true
            "false", "0" -> false
            else -> null
        }
    }

    private fun formatFloat(value: Float): String {
        val normalized = if (value % 1f == 0f) value.toInt().toString() else value.toString()
        return normalized
    }

    private fun formatYamlScalar(value: String): String {
        val needsQuotes = value.isEmpty() ||
            value != value.trim() ||
            value.contains(':') ||
            value.contains('#') ||
            value.equals("null", ignoreCase = true)
        if (!needsQuotes) return value

        val escaped = value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
        return "\"$escaped\""
    }

    private fun normalizeLogLevel(level: Int): Int {
        return level.coerceIn(0, 6)
    }

    private fun normalizeForBuild(config: VitaCoreConfig): VitaCoreConfig {
        val diagnosticsEnabled = config.logActiveShaders ||
            config.logUniforms ||
            config.logCompatWarn ||
            config.archiveLog ||
            config.colorSurfaceDebug
        val normalizedLogLevel = when {
            !BuildConfig.DEBUG -> RELEASE_LOG_LEVEL
            diagnosticsEnabled -> normalizeLogLevel(config.logLevel).coerceAtMost(DIAGNOSTIC_LOG_LEVEL)
            else -> maxOf(normalizeLogLevel(config.logLevel), DEFAULT_DEBUG_LOG_LEVEL)
        }
        return config.copy(
            validationLayer = false,
            discordRichPresence = false,
            gamepadDeadzone = config.gamepadDeadzone.coerceIn(0f, 0.45f),
            gamepadTriggerThreshold = config.gamepadTriggerThreshold.coerceIn(0f, 0.9f),
            gamepadButtonProfile = normalizeGamepadProfile(config.gamepadButtonProfile),
            gamepadVibrationStrength = config.gamepadVibrationStrength.coerceIn(0, 100),
            touchHapticsPreset = config.touchHapticsPreset.coerceIn(
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT,
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG
            ),
            touchHapticsStrength = config.touchHapticsStrength.coerceIn(10, 100),
            gyroMode = config.gyroMode.coerceIn(
                VitaCoreConfig.GYRO_MODE_OFF,
                VitaCoreConfig.GYRO_MODE_STEERING
            ),
            gyroSensitivity = config.gyroSensitivity.coerceIn(25, 300),
            gyroSmoothing = config.gyroSmoothing.coerceIn(0, 90),
            frameLimit = FrameLimit.normalize(config.frameLimit),
            logLevel = normalizedLogLevel
        )
    }

    private fun normalizeGamepadProfile(profile: String): String {
        return when (profile) {
            VitaCoreConfig.GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE,
            VitaCoreConfig.GAMEPAD_PROFILE_NINTENDO_FACE -> profile
            else -> VitaCoreConfig.GAMEPAD_PROFILE_STANDARD
        }
    }

    private fun releaseValuesNeedNormalization(values: Map<String, String>): Boolean {
        return values["custom-driver-name"]?.equals("null", ignoreCase = true) == true ||
            values["disable-surface-sync"]?.toBooleanStrictOrNull() == true ||
            values["discord-rich-presence"]?.toBooleanStrictOrNull() == true ||
            values["log-level"]?.toIntOrNull()?.let { it < defaultRuntimeLogLevel() } == true ||
            values["psn-signed-in"]?.let { it != "0" && it != "1" && it.toBooleanStrictOrNull() != null } == true ||
            values["validation-layer"]?.toBooleanStrictOrNull() == true ||
            upstreamSequenceKeys.any { values[it].isNullOrEmpty() }
    }

    private fun dropEmptyUpstreamSequenceValues(values: MutableMap<String, String>) {
        upstreamSequenceKeys.forEach { key ->
            if (values[key].isNullOrEmpty()) {
                values.remove(key)
            }
        }
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
        return VitaCoreConfig(
            logLevel = defaultRuntimeLogLevel()
        )
    }

    private fun defaultRuntimeLogLevel(): Int {
        return if (BuildConfig.DEBUG) DEFAULT_DEBUG_LOG_LEVEL else RELEASE_LOG_LEVEL
    }

    private companion object {
        private val CONFIG_IO_LOCK = Any()
        // 0 = TRACE (upstream Vita3K default). EmuCoreV keeps debug gameplay at
        // WARN and release at OFF; explicit diagnostics can opt into DEBUG.
        private const val DIAGNOSTIC_LOG_LEVEL = 1
        private const val DEFAULT_DEBUG_LOG_LEVEL = 3
        private const val RELEASE_LOG_LEVEL = 6
        // Bump whenever an old non-upstream default needs to be snapped to vanilla
        // for users who already wrote a stale config.yml. applyMigrations() rewrites
        // the affected keys on the next launch.
        private const val CONFIG_SCHEMA_VERSION = 8
        private const val SCHEMA_VERSION_KEY = "config-schema-version"
        private val upstreamSequenceKeys = setOf(
            "controller-axis-binds",
            "controller-binds",
            "controller-led-color",
            "ime-langs",
            "lle-modules",
            "tracy-advanced-profiling-modules"
        )
    }
}
