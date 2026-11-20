import java.security.MessageDigest
import javax.xml.parsers.DocumentBuilderFactory

plugins {
    alias(libs.plugins.android.application)
}

val verifyLocalNativeUiAssets by tasks.registering {
    doLast {
        val root = file("../resources")
        val required = listOf("images.json", "fonts.json", "fonts/ltn4.otf", "fonts/ltn0.otf", "audio/sounds.json",
            "effects/lookups/lookups.json", "effects/lookups/exp-minus.png",
            "effects/lookups/corner-distance.png",
            "images/topmenu_plugin/754463d1.png",
            "images/livearea_plugin/ca6a6db1.png",
            "images/livearea_plugin/1e6a33de.png",
            "images/indicator_plugin/ca76be07.png", "images/indicator_plugin/56e72f16.png",
            "preview/home/catalog.json",
            "preview/bubble/stream-0.bin", "preview/bubble/indices.bin",
            "preview/bubble/vertex-ubo-0.bin", "preview/bubble/fragment-ubo-0.bin",
            "preview/bubble/fragment-texture-0.bin", "preview/bubble/fragment-texture-0-palette.bin",
            "preview/bubble/environment.rgba")
        check(required.all { root.resolve(it).isFile }) {
            "Local native UI resource pack is missing or incomplete. " +
                "Prepare native-ui/resources using the offline tools documented in native-ui/README.md."
        }
        val homeRoot = root.resolve("preview/home").canonicalFile
        val homePrefix = homeRoot.path + File.separator
        val catalog = groovy.json.JsonSlurper().parse(homeRoot.resolve("catalog.json")) as? Map<*, *>
            ?: error("Invalid Home preview catalog")
        check((catalog["version"] as? Number)?.toInt() == 1) { "Unsupported Home preview catalog version" }
        val games = catalog["games"] as? List<*> ?: error("Home preview catalog has no games list")
        check(games.size <= 512) { "Home preview catalog contains too many games" }
        games.forEach { entry ->
            val game = entry as? Map<*, *> ?: error("Invalid Home preview catalog entry")
            val iconRelativePath = game["icon"] as? String ?: error("Home preview game has no icon path")
            val icon = homeRoot.resolve(iconRelativePath).canonicalFile
            check(icon.path.startsWith(homePrefix) && icon.isFile) {
                "Invalid Home preview catalog icon path: $iconRelativePath"
            }
            val liveArea = game["liveArea"] as? Map<*, *> ?: return@forEach
            val templateRelativePath = liveArea["template"] as? String
                ?: error("Home preview LiveArea has no template path")
            val template = homeRoot.resolve(templateRelativePath).canonicalFile
            check(template.path.startsWith(homePrefix) && template.isFile) {
                "Invalid Home preview LiveArea template path: $templateRelativePath"
            }
            val images = liveArea["images"] as? List<*>
                ?: error("Home preview LiveArea has no images list")
            images.forEach { imageValue ->
                val imageRelativePath = imageValue as? String
                    ?: error("Home preview LiveArea image path must be a string")
                val image = homeRoot.resolve(imageRelativePath).canonicalFile
                check(image.path.startsWith(homePrefix) && image.isFile) {
                    "Invalid Home preview LiveArea image path: $imageRelativePath"
                }
            }
        }
    }
}
val packedAssets = layout.buildDirectory.dir("generated/nativeUiAssets")
val startupAssets = layout.buildDirectory.dir("generated/nativeUiStartupAssets")
val copyStartupBusyImage by tasks.registering(Copy::class) {
    from(file("../resources/images/topmenu_plugin/754463d1.png")) {
        rename { "startup-busy.png" }
    }
    into(startupAssets)
}
val generatedPreviewStrings = layout.buildDirectory.dir("generated/nativeUiPreviewStrings")
val sourceAppResources = layout.projectDirectory.dir("../../app/src/main/res").asFile
val generatePreviewStrings by tasks.registering {
    inputs.dir(sourceAppResources)
    outputs.dir(generatedPreviewStrings)
    doLast {
        val outputRoot = generatedPreviewStrings.get().asFile
        outputRoot.deleteRecursively()
        val localeDirs = sourceAppResources.listFiles().orEmpty()
            .filter { it.isDirectory && (it.name == "values" || it.name.startsWith("values-")) }
            .filter { it.resolve("strings.xml").isFile }
        check(localeDirs.any { it.name == "values" }) { "App default strings.xml is missing" }
        localeDirs.forEach { localeDir ->
            val document = DocumentBuilderFactory.newInstance().newDocumentBuilder()
                .parse(localeDir.resolve("strings.xml"))
            fun translatedText(name: String): String {
                val strings = document.getElementsByTagName("string")
                for (index in 0 until strings.length) {
                    val element = strings.item(index) as org.w3c.dom.Element
                    if (element.getAttribute("name") == name) return element.textContent
                }
                error("Missing $name translation in ${localeDir.name}/strings.xml")
            }
            fun escapeXml(value: String): String = value
                .replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                .replace("\"", "&quot;").replace("'", "&apos;")
            val localeOutput = outputRoot.resolve(localeDir.name).apply { mkdirs() }
            localeOutput.resolve("strings.xml").writeText(
                """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="native_ui_start">${escapeXml(translatedText("native_ui_start"))}</string>
    <string name="native_ui_continue">${escapeXml(translatedText("native_ui_continue"))}</string>
</resources>
""".trimIndent(),
            )
        }
    }
}
val packNativeUiAssets by tasks.registering(Zip::class) {
    dependsOn(verifyLocalNativeUiAssets)
    from(file("../resources"))
    destinationDirectory.set(packedAssets)
    archiveFileName.set("native-ui.zip")
    isPreserveFileTimestamps = false
    isReproducibleFileOrder = true
}
val indexNativeUiAssets by tasks.registering {
    dependsOn(packNativeUiAssets)
    val archive = packNativeUiAssets.flatMap { it.archiveFile }
    inputs.file(archive)
    outputs.file(packedAssets.map { it.file("native-ui.sha256") })
    doLast {
        val hash = MessageDigest.getInstance("SHA-256")
        archive.get().asFile.inputStream().use { input ->
            val buffer = ByteArray(65536)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                hash.update(buffer, 0, count)
            }
        }
        packedAssets.get().file("native-ui.sha256").asFile.writeText(
            hash.digest().joinToString("") { "%02x".format(it) })
    }
}
tasks.named("preBuild") { dependsOn(indexNativeUiAssets, copyStartupBusyImage) }
tasks.named("preBuild") { dependsOn(generatePreviewStrings) }

android {
    namespace = "com.sbro.emucorev.nativeuipreview"
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    androidResources { noCompress += "zip" }
    sourceSets.getByName("main").kotlin.srcDir("../android/src/main/java")
    sourceSets.getByName("main").assets.srcDir(packedAssets.get().asFile)
    sourceSets.getByName("main").assets.srcDir(startupAssets.get().asFile)
    sourceSets.getByName("main").res.srcDir(generatedPreviewStrings.get().asFile)
    defaultConfig {
        applicationId = "com.sbro.emucorev.nativeuipreview"
        minSdk = 29
        targetSdk = 37
        versionCode = 1
        versionName = "native-preview"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DBUILD_TESTING=OFF")
                targets += "paf_preview"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

// This package is a local development surface, never a release deliverable.
androidComponents {
    beforeVariants(selector().withBuildType("release")) { it.enable = false }
}
