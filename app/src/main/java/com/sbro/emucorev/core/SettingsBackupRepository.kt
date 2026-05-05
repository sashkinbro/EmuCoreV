package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import com.sbro.emucorev.data.AppPreferences
import org.json.JSONObject

class SettingsBackupRepository(
    private val context: Context,
    private val preferences: AppPreferences,
    private val coreConfigRepository: VitaCoreConfigRepository
) {
    fun exportTo(uri: Uri) {
        val config = coreConfigRepository.ensureDefaultsPersisted()
        val root = JSONObject()
            .put("format", BACKUP_FORMAT_VERSION)
            .put(
                "app",
                JSONObject()
                    .putNullable("packagesFolderUri", preferences.packagesFolderUri)
                    .put("onboardingCompleted", preferences.onboardingCompleted)
                    .put("themeMode", preferences.themeMode.name)
                    .put("appLanguage", preferences.appLanguage.name)
                    .putNullable("skippedUpdateTag", preferences.skippedUpdateTag)
            )
            .put("core", config.toJson())

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
            preferences.onboardingCompleted = app.optBoolean(
                "onboardingCompleted",
                preferences.onboardingCompleted
            )
            preferences.themeMode = app.optEnum("themeMode", preferences.themeMode)
            preferences.appLanguage = app.optEnum("appLanguage", preferences.appLanguage)
            preferences.skippedUpdateTag = app.optNullableString("skippedUpdateTag")
            preferences.applyAppLanguage()
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
        .put("analogMultiplier", analogMultiplier.toDouble())
        .put("stretchDisplayArea", stretchDisplayArea)
        .put("fpsHack", fpsHack)
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

    private fun JSONObject.toVitaCoreConfig(defaults: VitaCoreConfig): VitaCoreConfig = defaults.copy(
        validationLayer = optBoolean("validationLayer", defaults.validationLayer),
        logActiveShaders = optBoolean("logActiveShaders", defaults.logActiveShaders),
        logUniforms = optBoolean("logUniforms", defaults.logUniforms),
        logCompatWarn = optBoolean("logCompatWarn", defaults.logCompatWarn),
        pstvMode = optBoolean("pstvMode", defaults.pstvMode),
        showInfoBar = optBoolean("showInfoBar", defaults.showInfoBar),
        showLiveAreaScreen = optBoolean("showLiveAreaScreen", defaults.showLiveAreaScreen),
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
        analogMultiplier = optFloat("analogMultiplier", defaults.analogMultiplier),
        stretchDisplayArea = optBoolean("stretchDisplayArea", defaults.stretchDisplayArea),
        fpsHack = optBoolean("fpsHack", defaults.fpsHack),
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
        showShaderCacheWarn = optBoolean("showShaderCacheWarn", defaults.showShaderCacheWarn)
    )

    private fun JSONObject.optFloat(name: String, fallback: Float): Float {
        return if (has(name)) optDouble(name, fallback.toDouble()).toFloat() else fallback
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
