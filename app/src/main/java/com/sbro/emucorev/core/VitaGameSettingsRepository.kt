package com.sbro.emucorev.core

import android.content.Context
import android.util.Log
import org.w3c.dom.Element
import java.io.File
import java.io.StringWriter
import javax.xml.parsers.DocumentBuilderFactory
import javax.xml.transform.OutputKeys
import javax.xml.transform.TransformerFactory
import javax.xml.transform.dom.DOMSource
import javax.xml.transform.stream.StreamResult

data class VitaGameSettingsProfile(
    val config: VitaCoreConfig,
    val customDriverOverride: String?
)

class VitaGameSettingsRepository(private val context: Context) {
    private val globalRepository = VitaCoreConfigRepository(context)
    private val gameDatabase by lazy {
        runCatching { context.assets.open(GameCompatibilityDatabase.ASSET_PATH).use(GameCompatibilityDatabase::parse) }
            .onFailure { Log.e("GameCompatibilityDB", "Could not load bundled recommendations", it) }
            .getOrDefault(GameCompatibilityDatabase.EMPTY)
    }

    fun recommendationFor(titleId: String, title: String = ""): GameGpuRecommendation? =
        gameDatabase.recommendationFor(titleId, title)

    private val configDirectory: File
        get() {
            val base = EmulatorStorage.runtimeRoot(context)
            return File(base, "config")
        }

    fun customConfigFile(titleId: String): File = File(configDirectory, "config_${titleId.trim()}.xml")

    fun hasCustomConfig(titleId: String): Boolean {
        return titleId.isNotBlank() && customConfigFile(titleId).exists()
    }

    fun loadEffective(titleId: String): VitaCoreConfig {
        return loadProfile(titleId).config
    }

    fun loadProfile(titleId: String): VitaGameSettingsProfile {
        val global = globalRepository.ensureDefaultsPersisted()
        if (titleId.isBlank()) return VitaGameSettingsProfile(global, null)
        val title = runCatching { VitaSfoParser.parse(EmulatorStorage.paramSfoPath(context, titleId)).title.orEmpty() }
            .getOrDefault("")
        val base = gameDatabase.applyDefaults(global, titleId, title)
        val file = customConfigFile(titleId)
        val profile = if (file.exists()) {
            runCatching { readCustomConfig(file, base) }.getOrDefault(VitaGameSettingsProfile(base, null))
        } else {
            VitaGameSettingsProfile(base, null)
        }
        return profile
    }

    fun save(titleId: String, config: VitaCoreConfig) {
        savePreservingDriverOverride(titleId, config)
    }

    fun savePreservingDriverOverride(titleId: String, config: VitaCoreConfig) {
        if (titleId.isBlank()) return
        val override = loadProfile(titleId).customDriverOverride
        saveProfile(titleId, config, override)
    }

    fun saveProfile(titleId: String, config: VitaCoreConfig, customDriverOverride: String?) {
        if (titleId.isBlank()) return
        synchronized(CONFIG_IO_LOCK) {
            val base = globalRepository.ensureDefaultsPersisted()
            val effectiveConfig = config.copy(customDriverName = customDriverOverride ?: base.customDriverName)
            configDirectory.mkdirs()
            writeCustomConfig(customConfigFile(titleId), effectiveConfig, customDriverOverride.driverMode())
        }
    }

    fun syncEffectiveDriverForLaunch(titleId: String): VitaCoreConfig {
        val profile = loadProfile(titleId)
        if (titleId.isNotBlank() && customConfigFile(titleId).exists()) {
            saveProfile(titleId, profile.config, profile.customDriverOverride)
        }
        return profile.config
    }

    fun update(titleId: String, transform: (VitaCoreConfig) -> VitaCoreConfig): VitaCoreConfig {
        val profile = loadProfile(titleId)
        val updated = transform(profile.config)
        saveProfile(titleId, updated, profile.customDriverOverride)
        return updated
    }

    fun reset(titleId: String) {
        if (titleId.isBlank()) return
        synchronized(CONFIG_IO_LOCK) {
            customConfigFile(titleId).delete()
        }
    }

    private fun readCustomConfig(file: File, base: VitaCoreConfig): VitaGameSettingsProfile {
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val root = doc.documentElement ?: return VitaGameSettingsProfile(base, null)
        var config = base
        var rawDriverName: String? = null
        var driverMode: String? = null

        root.childElement("core")?.let { core ->
            config = config.copy(modulesMode = core.intAttr("modules-mode", config.modulesMode))
        }
        root.childElement("cpu")?.let { cpu ->
            config = config.copy(cpuPoolSize = cpu.intAttr("cpu-pool-size", config.cpuPoolSize))
        }
        root.childElement("gpu")?.let { gpu ->
            rawDriverName = gpu.stringAttrOrNull("custom-driver-name")
            config = config.copy(
                backendRenderer = gpu.stringAttr("backend-renderer", config.backendRenderer),
                useAngle = gpu.boolAttr("use-angle", config.useAngle),
                customDriverName = gpu.stringAttr("custom-driver-name", config.customDriverName),
                highAccuracy = gpu.boolAttr("high-accuracy", config.highAccuracy),
                resolutionMultiplier = gpu.floatAttr("resolution-multiplier", config.resolutionMultiplier),
                disableSurfaceSync = gpu.boolAttr("disable-surface-sync", config.disableSurfaceSync),
                screenFilter = gpu.stringAttr("screen-filter", config.screenFilter),
                memoryMapping = gpu.stringAttr("memory-mapping", config.memoryMapping),
                vSync = gpu.boolAttr("v-sync", config.vSync),
                anisotropicFiltering = gpu.intAttr("anisotropic-filtering", config.anisotropicFiltering),
                asyncPipelineCompilation = gpu.boolAttr("async-pipeline-compilation", config.asyncPipelineCompilation),
                accurateThreadScheduling = gpu.boolAttr("accurate-thread-scheduling", config.accurateThreadScheduling),
                guestCores = gpu.intAttr("guest-cores", config.guestCores).coerceIn(1, 4),
                importTextures = gpu.boolAttr("import-textures", config.importTextures),
                exportTextures = gpu.boolAttr("export-textures", config.exportTextures),
                exportAsPng = gpu.boolAttr("export-as-png", config.exportAsPng),
                fpsHack = gpu.boolAttr("fps-hack", config.fpsHack),
                frameLimit = FrameLimit.normalize(gpu.intAttr("frame-limit", config.frameLimit)),
                shaderCache = gpu.boolAttr("shader-cache", config.shaderCache),
                spirvShader = gpu.boolAttr("spirv-shader", config.spirvShader),
                textureCache = gpu.boolAttr("texture-cache", config.textureCache),
                showCompileShaders = gpu.boolAttr("show-compile-shaders", config.showCompileShaders),
                showShaderCacheWarn = gpu.boolAttr("show-shader-cache-warn", config.showShaderCacheWarn)
            )
        }
        root.childElement("audio")?.let { audio ->
            config = config.copy(
                audioBackend = audio.stringAttr("audio-backend", config.audioBackend),
                audioVolume = audio.intAttr("audio-volume", config.audioVolume),
                ngsEnable = audio.boolAttr("enable-ngs", config.ngsEnable)
            )
        }
        root.childElement("system")?.let { system ->
            config = config.copy(
                pstvMode = system.boolAttr("pstv-mode", config.pstvMode),
                sysButton = system.intAttr("sys-button", config.sysButton),
                sysLang = system.intAttr("sys-lang", config.sysLang)
            )
        }
        root.childElement("emulator")?.let { emulator ->
            config = config.copy(
                fileLoadingDelay = emulator.intAttr("file-loading-delay", config.fileLoadingDelay),
                stretchDisplayArea = emulator.boolAttr("stretch-the-display-area", config.stretchDisplayArea),
                fullscreenHdResPixelPerfect = emulator.boolAttr("fullscreen-hd-res-pixel-perfect", config.fullscreenHdResPixelPerfect)
            )
        }
        root.childElement("debug")?.let { debug ->
            config = config.copy(
                logActiveShaders = debug.boolAttr("log-active-shaders", config.logActiveShaders),
                logUniforms = debug.boolAttr("log-uniforms", config.logUniforms),
                colorSurfaceDebug = debug.boolAttr("color-surface-debug", config.colorSurfaceDebug),
                validationLayer = debug.boolAttr("validation-layer", config.validationLayer)
            )
        }
        root.childElement("network")?.let { network ->
            config = config.copy(psnSignedIn = network.boolAttr("psn-signed-in", config.psnSignedIn))
        }
        root.childElement("emucorev")?.let { own ->
            driverMode = own.stringAttrOrNull("gpu-driver-mode")
            config = config.copy(
                performanceOverlay = own.boolAttr("performance-overlay", config.performanceOverlay),
                performanceOverlayDetail = own.intAttr("performance-overlay-detail", config.performanceOverlayDetail),
                performanceOverlayPosition = own.intAttr("performance-overlay-position", config.performanceOverlayPosition),
                enableGamepadOverlay = own.boolAttr("enable-gamepad-overlay", config.enableGamepadOverlay),
                overlayShowTouchSwitch = own.boolAttr("overlay-show-touch-switch", config.overlayShowTouchSwitch),
                overlayScale = own.floatAttr("overlay-scale", config.overlayScale),
                overlayOpacity = own.intAttr("overlay-opacity", config.overlayOpacity),
                disableMotion = own.boolAttr("disable-motion", config.disableMotion),
                touchHaptics = own.boolAttr("touch-haptics", config.touchHaptics),
                touchHapticsPreset = own.intAttr("touch-haptics-preset", config.touchHapticsPreset)
                    .coerceIn(VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT, VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG),
                touchHapticsStrength = own.intAttr("touch-haptics-strength", config.touchHapticsStrength)
                    .coerceIn(10, 100),
                gyroMode = own.intAttr("gyro-mode", config.gyroMode)
                    .coerceIn(VitaCoreConfig.GYRO_MODE_OFF, VitaCoreConfig.GYRO_MODE_STEERING),
                gyroSensitivity = own.intAttr("gyro-sensitivity", config.gyroSensitivity)
                    .coerceIn(25, 300),
                gyroSmoothing = own.intAttr("gyro-smoothing", config.gyroSmoothing)
                    .coerceIn(0, 90),
                gyroInvertX = own.boolAttr("gyro-invert-x", config.gyroInvertX),
                gyroInvertY = own.boolAttr("gyro-invert-y", config.gyroInvertY),
                analogMultiplier = own.floatAttr("controller-analog-multiplier", config.analogMultiplier),
                gamepadDeadzone = own.floatAttr("gamepad-deadzone", config.gamepadDeadzone),
                gamepadTriggerThreshold = own.floatAttr("gamepad-trigger-threshold", config.gamepadTriggerThreshold),
                gamepadButtonProfile = own.stringAttr("gamepad-button-profile", config.gamepadButtonProfile),
                gamepadVibration = own.boolAttr("gamepad-vibration", config.gamepadVibration),
                gamepadVibrationStrength = own.intAttr("gamepad-vibration-strength", config.gamepadVibrationStrength),
                deviceVibrationFallback = own.boolAttr("device-vibration-fallback", config.deviceVibrationFallback),
                gamepadSwapSticks = own.boolAttr("gamepad-swap-sticks", config.gamepadSwapSticks),
                gamepadInvertLeftX = own.boolAttr("gamepad-invert-left-x", config.gamepadInvertLeftX),
                gamepadInvertLeftY = own.boolAttr("gamepad-invert-left-y", config.gamepadInvertLeftY),
                gamepadInvertRightX = own.boolAttr("gamepad-invert-right-x", config.gamepadInvertRightX),
                gamepadInvertRightY = own.boolAttr("gamepad-invert-right-y", config.gamepadInvertRightY),
                showTouchpadCursor = own.boolAttr("show-touchpad-cursor", config.showTouchpadCursor),
                turboMode = own.boolAttr("turbo-mode", config.turboMode),
                showInfoBar = own.boolAttr("show-info-bar", config.showInfoBar),
                bgmVolume = own.intAttr("bgm-volume", config.bgmVolume)
            )
        }

        val customDriverOverride = resolveDriverOverride(driverMode, rawDriverName, base)
        return VitaGameSettingsProfile(
            config = config.copy(customDriverName = customDriverOverride ?: base.customDriverName),
            customDriverOverride = customDriverOverride
        )
    }

    private fun writeCustomConfig(file: File, config: VitaCoreConfig, driverMode: String) {
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        val root = doc.createElement("config")
        doc.appendChild(root)

        root.appendChild(doc.createElement("core").apply {
            setAttribute("modules-mode", config.modulesMode.toString())
            appendChild(doc.createElement("lle-modules"))
        })
        root.appendChild(doc.createElement("cpu").apply {
            setAttribute("cpu-opt", true.toString())
            setAttribute("cpu-pool-size", config.cpuPoolSize.toString())
        })
        root.appendChild(doc.createElement("gpu").apply {
            setAttribute("backend-renderer", config.backendRenderer)
            setAttribute("use-angle", config.useAngle.toString())
            setAttribute("gpu-idx", "0")
            setAttribute("custom-driver-name", config.customDriverName)
            setAttribute("high-accuracy", config.highAccuracy.toString())
            setAttribute("resolution-multiplier", config.resolutionMultiplier.toString())
            setAttribute("disable-surface-sync", config.disableSurfaceSync.toString())
            setAttribute("screen-filter", config.screenFilter)
            setAttribute("memory-mapping", config.memoryMapping)
            setAttribute("v-sync", config.vSync.toString())
            setAttribute("anisotropic-filtering", config.anisotropicFiltering.toString())
            setAttribute("async-pipeline-compilation", config.asyncPipelineCompilation.toString())
            setAttribute("accurate-thread-scheduling", config.accurateThreadScheduling.toString())
            setAttribute("guest-cores", config.guestCores.coerceIn(1, 4).toString())
            setAttribute("import-textures", config.importTextures.toString())
            setAttribute("export-textures", config.exportTextures.toString())
            setAttribute("export-as-png", config.exportAsPng.toString())
            setAttribute("fps-hack", config.fpsHack.toString())
            setAttribute("frame-limit", FrameLimit.normalize(config.frameLimit).toString())
            setAttribute("shader-cache", config.shaderCache.toString())
            setAttribute("spirv-shader", config.spirvShader.toString())
            setAttribute("texture-cache", config.textureCache.toString())
            setAttribute("show-compile-shaders", config.showCompileShaders.toString())
            setAttribute("show-shader-cache-warn", config.showShaderCacheWarn.toString())
        })
        root.appendChild(doc.createElement("audio").apply {
            setAttribute("audio-backend", config.audioBackend)
            setAttribute("audio-volume", config.audioVolume.toString())
            setAttribute("enable-ngs", config.ngsEnable.toString())
        })
        root.appendChild(doc.createElement("system").apply {
            setAttribute("pstv-mode", config.pstvMode.toString())
            setAttribute("sys-button", config.sysButton.toString())
            setAttribute("sys-lang", config.sysLang.toString())
            appendChild(doc.createElement("ime-langs").apply {
                appendChild(doc.createElement("lang").apply { textContent = "4" })
            })
        })
        root.appendChild(doc.createElement("emulator").apply {
            setAttribute("file-loading-delay", config.fileLoadingDelay.toString())
            setAttribute("stretch-the-display-area", config.stretchDisplayArea.toString())
            setAttribute("fullscreen-hd-res-pixel-perfect", config.fullscreenHdResPixelPerfect.toString())
        })
        root.appendChild(doc.createElement("debug").apply {
            setAttribute("log-active-shaders", config.logActiveShaders.toString())
            setAttribute("log-uniforms", config.logUniforms.toString())
            setAttribute("color-surface-debug", config.colorSurfaceDebug.toString())
            setAttribute("validation-layer", config.validationLayer.toString())
        })
        root.appendChild(doc.createElement("network").apply {
            setAttribute("psn-signed-in", config.psnSignedIn.toString())
        })
        root.appendChild(doc.createElement("emucorev").apply {
            setAttribute("gpu-driver-mode", driverMode)
            setAttribute("performance-overlay", config.performanceOverlay.toString())
            setAttribute("performance-overlay-detail", config.performanceOverlayDetail.toString())
            setAttribute("performance-overlay-position", config.performanceOverlayPosition.toString())
            setAttribute("enable-gamepad-overlay", config.enableGamepadOverlay.toString())
            setAttribute("overlay-show-touch-switch", config.overlayShowTouchSwitch.toString())
            setAttribute("overlay-scale", config.overlayScale.toString())
            setAttribute("overlay-opacity", config.overlayOpacity.toString())
            setAttribute("disable-motion", config.disableMotion.toString())
            setAttribute("touch-haptics", config.touchHaptics.toString())
            setAttribute("touch-haptics-preset", config.touchHapticsPreset.toString())
            setAttribute("touch-haptics-strength", config.touchHapticsStrength.toString())
            setAttribute("gyro-mode", config.gyroMode.toString())
            setAttribute("gyro-sensitivity", config.gyroSensitivity.toString())
            setAttribute("gyro-smoothing", config.gyroSmoothing.toString())
            setAttribute("gyro-invert-x", config.gyroInvertX.toString())
            setAttribute("gyro-invert-y", config.gyroInvertY.toString())
            setAttribute("controller-analog-multiplier", config.analogMultiplier.toString())
            setAttribute("gamepad-deadzone", config.gamepadDeadzone.toString())
            setAttribute("gamepad-trigger-threshold", config.gamepadTriggerThreshold.toString())
            setAttribute("gamepad-button-profile", config.gamepadButtonProfile)
            setAttribute("gamepad-vibration", config.gamepadVibration.toString())
            setAttribute("gamepad-vibration-strength", config.gamepadVibrationStrength.toString())
            setAttribute("device-vibration-fallback", config.deviceVibrationFallback.toString())
            setAttribute("gamepad-swap-sticks", config.gamepadSwapSticks.toString())
            setAttribute("gamepad-invert-left-x", config.gamepadInvertLeftX.toString())
            setAttribute("gamepad-invert-left-y", config.gamepadInvertLeftY.toString())
            setAttribute("gamepad-invert-right-x", config.gamepadInvertRightX.toString())
            setAttribute("gamepad-invert-right-y", config.gamepadInvertRightY.toString())
            setAttribute("show-touchpad-cursor", config.showTouchpadCursor.toString())
            setAttribute("turbo-mode", config.turboMode.toString())
            setAttribute("show-info-bar", config.showInfoBar.toString())
            setAttribute("bgm-volume", config.bgmVolume.toString())
        })

        val output = StringWriter()
        TransformerFactory.newInstance().newTransformer().apply {
            setOutputProperty(OutputKeys.INDENT, "yes")
            setOutputProperty(OutputKeys.ENCODING, "utf-8")
        }.transform(DOMSource(doc), StreamResult(output))
        AtomicTextFile.write(file, output.toString())
    }

    private fun resolveDriverOverride(mode: String?, rawDriverName: String?, base: VitaCoreConfig): String? {
        return when (mode) {
            DRIVER_MODE_GLOBAL -> null
            DRIVER_MODE_SYSTEM -> ""
            DRIVER_MODE_CUSTOM -> rawDriverName.orEmpty()
            else -> when (rawDriverName) {
                null -> null
                base.customDriverName -> null
                else -> rawDriverName
            }
        }
    }

    private fun String?.driverMode(): String {
        return when {
            this == null -> DRIVER_MODE_GLOBAL
            this.isBlank() -> DRIVER_MODE_SYSTEM
            else -> DRIVER_MODE_CUSTOM
        }
    }

    private fun Element.childElement(name: String): Element? {
        val nodes = getElementsByTagName(name)
        for (index in 0 until nodes.length) {
            val element = nodes.item(index) as? Element ?: continue
            if (element.parentNode == this) return element
        }
        return null
    }

    private fun Element.stringAttr(name: String, default: String): String =
        if (hasAttribute(name)) getAttribute(name) else default

    private fun Element.stringAttrOrNull(name: String): String? =
        if (hasAttribute(name)) getAttribute(name) else null

    private fun Element.boolAttr(name: String, default: Boolean): Boolean =
        if (hasAttribute(name)) getAttribute(name).toBooleanStrictOrNull() ?: default else default

    private fun Element.intAttr(name: String, default: Int): Int =
        if (hasAttribute(name)) getAttribute(name).toIntOrNull() ?: default else default

    private fun Element.floatAttr(name: String, default: Float): Float =
        if (hasAttribute(name)) getAttribute(name).toFloatOrNull() ?: default else default

    companion object {
        private val CONFIG_IO_LOCK = Any()
        private const val DRIVER_MODE_GLOBAL = "global"
        private const val DRIVER_MODE_SYSTEM = "system"
        private const val DRIVER_MODE_CUSTOM = "custom"
    }
}
