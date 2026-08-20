package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomizationPreferences
import org.json.JSONObject

class SettingsBackupRepository(
    private val context: Context,
    private val preferences: AppPreferences,
    private val coreConfigRepository: VitaCoreConfigRepository,
    private val customizationPreferences: CustomizationPreferences
) {
    fun exportTo(uri: Uri) {
        val config = coreConfigRepository.ensureDefaultsPersisted()
        val root = JSONObject()
            .put("format", BACKUP_FORMAT_VERSION)
            .put(
                "app",
                JSONObject()
                    .putNullable("packagesFolderUri", preferences.packagesFolderUri)
                    .putNullable("vitaStorageRootPath", preferences.vitaStorageRootPath)
                    .put("onboardingCompleted", preferences.onboardingCompleted)
                    .put("themeMode", preferences.themeMode.name)
                    .put("appLanguage", preferences.appLanguage.name)
                    .putNullable("skippedUpdateTag", preferences.skippedUpdateTag)
            )
            .put("core", config.toJson())
            .put(
                "customization",
                JSONObject()
                    .put("coverSizePercent", customizationPreferences.current.coverSizePercent)
                    .put(
                        "appFont",
                        customizationPreferences.current.appFont
                            .takeUnless { it == AppFont.CUSTOM }
                            ?.name ?: AppFont.SYSTEM.name
                    )
                    .put("textSizePercent", customizationPreferences.current.textSizePercent)
                    .put("touchControlVisualStyle", customizationPreferences.current.touchControlVisualStyle.name)
                    .put("touchControlPressEffect", customizationPreferences.current.touchControlPressEffect.name)
                    .put("gameMenuLayoutStyle", customizationPreferences.current.gameMenuLayoutStyle.name)
                    .put("drawerVisualStyle", customizationPreferences.current.drawerVisualStyle.name)
            )

        context.contentResolver.openOutputStream(uri)?.use { output ->
            output.write(root.toString(2).toByteArray(Charsets.UTF_8))
        } ?: error("Could not open backup destination")
    }

    fun restoreFrom(uri: Uri): VitaCoreConfig {
        val text = context.contentResolver.openInputStream(uri)?.use { input ->
            input.bufferedReader(Charsets.UTF_8).readText()
        } ?: error("Could not open backup file")

        val root = JSONObject(text)
        require(root.optInt("format", -1) == BACKUP_FORMAT_VERSION) {
            "Unsupported settings backup format"
        }

        root.optJSONObject("app")?.let { app ->
            preferences.packagesFolderUri = app.optNullableString("packagesFolderUri")
            preferences.vitaStorageRootPath = app.optNullableString("vitaStorageRootPath")
            preferences.onboardingCompleted = app.optBoolean(
                "onboardingCompleted",
                preferences.onboardingCompleted
            )
            preferences.themeMode = app.optEnum("themeMode", preferences.themeMode)
            preferences.appLanguage = app.optEnum("appLanguage", preferences.appLanguage)
            preferences.skippedUpdateTag = app.optNullableString("skippedUpdateTag")
            preferences.applyAppLanguage()
        }
        root.optJSONObject("customization")?.let { customization ->
            customizationPreferences.setCoverSizePercent(
                customization.optInt(
                    "coverSizePercent",
                    customizationPreferences.current.coverSizePercent
                )
            )
            customizationPreferences.setAppFont(
                customization.optEnum("appFont", AppFont.SYSTEM)
            )
            customizationPreferences.setTextSizePercent(
                customization.optInt(
                    "textSizePercent",
                    customizationPreferences.current.textSizePercent
                )
            )
            customizationPreferences.setTouchControlVisualStyle(
                customization.optEnum(
                    "touchControlVisualStyle",
                    customizationPreferences.current.touchControlVisualStyle
                )
            )
            customizationPreferences.setTouchControlPressEffect(
                customization.optEnum(
                    "touchControlPressEffect",
                    customizationPreferences.current.touchControlPressEffect
                )
            )
            customizationPreferences.setGameMenuLayoutStyle(
                customization.optEnum(
                    "gameMenuLayoutStyle",
                    customizationPreferences.current.gameMenuLayoutStyle
                )
            )
            customizationPreferences.setDrawerVisualStyle(
                customization.optEnum(
                    "drawerVisualStyle",
                    customizationPreferences.current.drawerVisualStyle
                )
            )
        }

        val restoredConfig = root.optJSONObject("core")
            ?.toVitaCoreConfig(coreConfigRepository.ensureDefaultsPersisted())
            ?: coreConfigRepository.ensureDefaultsPersisted()
        coreConfigRepository.save(restoredConfig)
        return coreConfigRepository.ensureDefaultsPersisted()
    }

    private fun VitaCoreConfig.toJson(): JSONObject = JSONObject()
        .put("validationLayer", validationLayer)
        .put("logActiveShaders", logActiveShaders)
        .put("logUniforms", logUniforms)
        .put("logCompatWarn", logCompatWarn)
        .put("pstvMode", pstvMode)
        .put("showInfoBar", showInfoBar)
        .put("showLiveAreaScreen", showLiveAreaScreen)
        .put("useAngle", useAngle)
        .put("backendRenderer", backendRenderer)
        .put("customDriverName", customDriverName)
        .put("turboMode", turboMode)
        .put("highAccuracy", highAccuracy)
        .put("resolutionMultiplier", resolutionMultiplier.toDouble())
        .put("disableSurfaceSync", disableSurfaceSync)
        .put("screenFilter", screenFilter)
        .put("anisotropicFiltering", anisotropicFiltering)
        .put("textureCache", textureCache)
        .put("asyncPipelineCompilation", asyncPipelineCompilation)
        .put("accurateThreadScheduling", accurateThreadScheduling)
        .put("guestCores", guestCores)
        .put("showCompileShaders", showCompileShaders)
        .put("hashlessTextureCache", hashlessTextureCache)
        .put("importTextures", importTextures)
        .put("exportTextures", exportTextures)
        .put("exportAsPng", exportAsPng)
        .put("memoryMapping", memoryMapping)
        .put("fullscreenHdResPixelPerfect", fullscreenHdResPixelPerfect)
        .put("performanceOverlay", performanceOverlay)
        .put("performanceOverlayDetail", performanceOverlayDetail)
        .put("performanceOverlayPosition", performanceOverlayPosition)
        .put("enableGamepadOverlay", enableGamepadOverlay)
        .put("overlayShowTouchSwitch", overlayShowTouchSwitch)
        .put("overlayScale", overlayScale.toDouble())
        .put("overlayOpacity", overlayOpacity)
        .put("disableMotion", disableMotion)
        .put("touchHaptics", touchHaptics)
        .put("touchHapticsPreset", touchHapticsPreset)
        .put("touchHapticsStrength", touchHapticsStrength)
        .put("gyroMode", gyroMode)
        .put("gyroSensitivity", gyroSensitivity)
        .put("gyroSmoothing", gyroSmoothing)
        .put("gyroInvertX", gyroInvertX)
        .put("gyroInvertY", gyroInvertY)
        .put("analogMultiplier", analogMultiplier.toDouble())
        .put("gamepadDeadzone", gamepadDeadzone.toDouble())
        .put("gamepadTriggerThreshold", gamepadTriggerThreshold.toDouble())
        .put("gamepadButtonProfile", gamepadButtonProfile)
        .put("gamepadVibration", gamepadVibration)
        .put("gamepadVibrationStrength", gamepadVibrationStrength)
        .put("deviceVibrationFallback", deviceVibrationFallback)
        .put("gamepadSwapSticks", gamepadSwapSticks)
        .put("gamepadInvertLeftX", gamepadInvertLeftX)
        .put("gamepadInvertLeftY", gamepadInvertLeftY)
        .put("gamepadInvertRightX", gamepadInvertRightX)
        .put("gamepadInvertRightY", gamepadInvertRightY)
        .put("stretchDisplayArea", stretchDisplayArea)
        .put("fpsHack", fpsHack)
        .put("frameLimit", frameLimit)
        .put("vSync", vSync)
        .put("bootAppsFullScreen", bootAppsFullScreen)
        .put("audioBackend", audioBackend)
        .put("audioVolume", audioVolume)
        .put("bgmVolume", bgmVolume)
        .put("ngsEnable", ngsEnable)
        .put("showTouchpadCursor", showTouchpadCursor)
        .put("sysButton", sysButton)
        .put("sysLang", sysLang)
        .put("cpuPoolSize", cpuPoolSize)
        .put("modulesMode", modulesMode)
        .put("archiveLog", archiveLog)
        .put("logLevel", logLevel)
        .put("discordRichPresence", discordRichPresence)
        .put("checkForUpdates", checkForUpdates)
        .put("fileLoadingDelay", fileLoadingDelay)
        .put("shaderCache", shaderCache)
        .put("spirvShader", spirvShader)
        .put("psnSignedIn", psnSignedIn)
        .put("httpEnable", httpEnable)
        .put("colorSurfaceDebug", colorSurfaceDebug)
        .put("showShaderCacheWarn", showShaderCacheWarn)
        .put("frontCameraType", frontCameraType)
        .put("frontCameraId", frontCameraId)
        .put("frontCameraImage", frontCameraImage)
        .put("frontCameraColor", frontCameraColor)
        .put("backCameraType", backCameraType)
        .put("backCameraId", backCameraId)
        .put("backCameraImage", backCameraImage)
        .put("backCameraColor", backCameraColor)
        .put("screenshotFormat", screenshotFormat)
        .put("showWelcome", showWelcome)
        .put("warnMissingFirmware", warnMissingFirmware)

    private fun JSONObject.toVitaCoreConfig(defaults: VitaCoreConfig): VitaCoreConfig = defaults.copy(
        validationLayer = optBoolean("validationLayer", defaults.validationLayer),
        logActiveShaders = optBoolean("logActiveShaders", defaults.logActiveShaders),
        logUniforms = optBoolean("logUniforms", defaults.logUniforms),
        logCompatWarn = optBoolean("logCompatWarn", defaults.logCompatWarn),
        pstvMode = optBoolean("pstvMode", defaults.pstvMode),
        showInfoBar = optBoolean("showInfoBar", defaults.showInfoBar),
        showLiveAreaScreen = optBoolean("showLiveAreaScreen", defaults.showLiveAreaScreen),
        useAngle = optBoolean("useAngle", defaults.useAngle),
        backendRenderer = optString("backendRenderer", defaults.backendRenderer),
        customDriverName = optString("customDriverName", defaults.customDriverName),
        turboMode = optBoolean("turboMode", defaults.turboMode),
        highAccuracy = optBoolean("highAccuracy", defaults.highAccuracy),
        resolutionMultiplier = optFloat("resolutionMultiplier", defaults.resolutionMultiplier),
        disableSurfaceSync = optBoolean("disableSurfaceSync", defaults.disableSurfaceSync),
        screenFilter = optString("screenFilter", defaults.screenFilter),
        anisotropicFiltering = optInt("anisotropicFiltering", defaults.anisotropicFiltering),
        textureCache = optBoolean("textureCache", defaults.textureCache),
        asyncPipelineCompilation = optBoolean("asyncPipelineCompilation", defaults.asyncPipelineCompilation),
        accurateThreadScheduling = optBoolean("accurateThreadScheduling", defaults.accurateThreadScheduling),
        guestCores = optInt("guestCores", defaults.guestCores).coerceIn(1, 4),
        showCompileShaders = optBoolean("showCompileShaders", defaults.showCompileShaders),
        hashlessTextureCache = optBoolean("hashlessTextureCache", defaults.hashlessTextureCache),
        importTextures = optBoolean("importTextures", defaults.importTextures),
        exportTextures = optBoolean("exportTextures", defaults.exportTextures),
        exportAsPng = optBoolean("exportAsPng", defaults.exportAsPng),
        memoryMapping = optString("memoryMapping", defaults.memoryMapping),
        fullscreenHdResPixelPerfect = optBoolean("fullscreenHdResPixelPerfect", defaults.fullscreenHdResPixelPerfect),
        performanceOverlay = optBoolean("performanceOverlay", defaults.performanceOverlay),
        performanceOverlayDetail = optInt("performanceOverlayDetail", defaults.performanceOverlayDetail),
        performanceOverlayPosition = optInt("performanceOverlayPosition", defaults.performanceOverlayPosition),
        enableGamepadOverlay = optBoolean("enableGamepadOverlay", defaults.enableGamepadOverlay),
        overlayShowTouchSwitch = optBoolean("overlayShowTouchSwitch", defaults.overlayShowTouchSwitch),
        overlayScale = optFloat("overlayScale", defaults.overlayScale),
        overlayOpacity = optInt("overlayOpacity", defaults.overlayOpacity),
        disableMotion = optBoolean("disableMotion", defaults.disableMotion),
        touchHaptics = optBoolean("touchHaptics", defaults.touchHaptics),
        touchHapticsPreset = optInt("touchHapticsPreset", defaults.touchHapticsPreset)
            .coerceIn(VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT, VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG),
        touchHapticsStrength = optInt("touchHapticsStrength", defaults.touchHapticsStrength)
            .coerceIn(10, 100),
        gyroMode = optInt("gyroMode", defaults.gyroMode)
            .coerceIn(VitaCoreConfig.GYRO_MODE_OFF, VitaCoreConfig.GYRO_MODE_STEERING),
        gyroSensitivity = optInt("gyroSensitivity", defaults.gyroSensitivity)
            .coerceIn(25, 300),
        gyroSmoothing = optInt("gyroSmoothing", defaults.gyroSmoothing)
            .coerceIn(0, 90),
        gyroInvertX = optBoolean("gyroInvertX", defaults.gyroInvertX),
        gyroInvertY = optBoolean("gyroInvertY", defaults.gyroInvertY),
        analogMultiplier = optFloat("analogMultiplier", defaults.analogMultiplier),
        gamepadDeadzone = optFloat("gamepadDeadzone", defaults.gamepadDeadzone),
        gamepadTriggerThreshold = optFloat("gamepadTriggerThreshold", defaults.gamepadTriggerThreshold),
        gamepadButtonProfile = optString("gamepadButtonProfile", defaults.gamepadButtonProfile),
        gamepadVibration = optBoolean("gamepadVibration", defaults.gamepadVibration),
        gamepadVibrationStrength = optInt("gamepadVibrationStrength", defaults.gamepadVibrationStrength),
        deviceVibrationFallback = optBoolean("deviceVibrationFallback", defaults.deviceVibrationFallback),
        gamepadSwapSticks = optBoolean("gamepadSwapSticks", defaults.gamepadSwapSticks),
        gamepadInvertLeftX = optBoolean("gamepadInvertLeftX", defaults.gamepadInvertLeftX),
        gamepadInvertLeftY = optBoolean("gamepadInvertLeftY", defaults.gamepadInvertLeftY),
        gamepadInvertRightX = optBoolean("gamepadInvertRightX", defaults.gamepadInvertRightX),
        gamepadInvertRightY = optBoolean("gamepadInvertRightY", defaults.gamepadInvertRightY),
        stretchDisplayArea = optBoolean("stretchDisplayArea", defaults.stretchDisplayArea),
        fpsHack = optBoolean("fpsHack", defaults.fpsHack),
        frameLimit = FrameLimit.normalize(optInt("frameLimit", defaults.frameLimit)),
        vSync = optBoolean("vSync", defaults.vSync),
        bootAppsFullScreen = optBoolean("bootAppsFullScreen", defaults.bootAppsFullScreen),
        audioBackend = optString("audioBackend", defaults.audioBackend),
        audioVolume = optInt("audioVolume", defaults.audioVolume),
        bgmVolume = optInt("bgmVolume", defaults.bgmVolume),
        ngsEnable = optBoolean("ngsEnable", defaults.ngsEnable),
        showTouchpadCursor = optBoolean("showTouchpadCursor", defaults.showTouchpadCursor),
        sysButton = optInt("sysButton", defaults.sysButton),
        sysLang = optInt("sysLang", defaults.sysLang),
        cpuPoolSize = optInt("cpuPoolSize", defaults.cpuPoolSize),
        modulesMode = optInt("modulesMode", defaults.modulesMode),
        archiveLog = optBoolean("archiveLog", defaults.archiveLog),
        logLevel = optInt("logLevel", defaults.logLevel),
        discordRichPresence = optBoolean("discordRichPresence", defaults.discordRichPresence),
        checkForUpdates = optBoolean("checkForUpdates", defaults.checkForUpdates),
        fileLoadingDelay = optInt("fileLoadingDelay", defaults.fileLoadingDelay),
        shaderCache = optBoolean("shaderCache", defaults.shaderCache),
        spirvShader = optBoolean("spirvShader", defaults.spirvShader),
        psnSignedIn = optBoolean("psnSignedIn", defaults.psnSignedIn),
        httpEnable = optBoolean("httpEnable", defaults.httpEnable),
        colorSurfaceDebug = optBoolean("colorSurfaceDebug", defaults.colorSurfaceDebug),
        showShaderCacheWarn = optBoolean("showShaderCacheWarn", defaults.showShaderCacheWarn),
        frontCameraType = optInt("frontCameraType", defaults.frontCameraType),
        frontCameraId = optString("frontCameraId", defaults.frontCameraId),
        frontCameraImage = optString("frontCameraImage", defaults.frontCameraImage),
        frontCameraColor = optLongValue("frontCameraColor", defaults.frontCameraColor),
        backCameraType = optInt("backCameraType", defaults.backCameraType),
        backCameraId = optString("backCameraId", defaults.backCameraId),
        backCameraImage = optString("backCameraImage", defaults.backCameraImage),
        backCameraColor = optLongValue("backCameraColor", defaults.backCameraColor),
        screenshotFormat = optInt("screenshotFormat", defaults.screenshotFormat),
        showWelcome = optBoolean("showWelcome", defaults.showWelcome),
        warnMissingFirmware = optBoolean("warnMissingFirmware", defaults.warnMissingFirmware)
    )

    private fun JSONObject.optFloat(name: String, fallback: Float): Float {
        return if (has(name)) optDouble(name, fallback.toDouble()).toFloat() else fallback
    }

    private fun JSONObject.optLongValue(name: String, fallback: Long): Long {
        return if (has(name)) optLong(name, fallback) else fallback
    }

    private fun JSONObject.optNullableString(name: String): String? {
        return if (!has(name) || isNull(name)) null else optString(name).takeIf(String::isNotBlank)
    }

    private inline fun <reified T : Enum<T>> JSONObject.optEnum(name: String, fallback: T): T {
        val value = optString(name, fallback.name)
        return enumValues<T>().firstOrNull { it.name == value } ?: fallback
    }

    private fun JSONObject.putNullable(name: String, value: String?): JSONObject {
        return put(name, value ?: JSONObject.NULL)
    }

    private companion object {
        private const val BACKUP_FORMAT_VERSION = 1
    }
}
