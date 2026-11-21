import java.security.MessageDigest
import javax.xml.parsers.DocumentBuilderFactory

plugins {
    alias(libs.plugins.android.library)
}

// Standalone native Vita shell UI. The ready resource pack lives in the
// git-ignored native-ui/resources directory (prepared offline, never extracted
// from firmware at runtime); settings.gradle only includes this module when it
// exists.
val resourceRoot = file("../resources")

val verifyNativeShellResources by tasks.registering {
    doLast {
        val required = listOf("images.json", "fonts.json", "fonts/ltn4.otf", "fonts/ltn0.otf", "audio/sounds.json",
            "effects/lookups/lookups.json", "effects/lookups/exp-minus.png",
            "effects/lookups/corner-distance.png",
            "images/topmenu_plugin/754463d1.png",
            "images/livearea_plugin/ca6a6db1.png",
            "images/livearea_plugin/1e6a33de.png",
            "images/indicator_plugin/ca76be07.png", "images/indicator_plugin/56e72f16.png",
            "preview/bubble/stream-0.bin", "preview/bubble/indices.bin",
            "preview/bubble/vertex-ubo-0.bin", "preview/bubble/fragment-ubo-0.bin",
            "preview/bubble/fragment-texture-0.bin", "preview/bubble/fragment-texture-0-palette.bin",
            "preview/bubble/environment.rgba")
        check(required.all { resourceRoot.resolve(it).isFile }) {
            "Local native UI resource pack is missing or incomplete. " +
                "Prepare native-ui/resources using the offline tools documented in native-ui/README.md."
        }
    }
}

val packedAssets = layout.buildDirectory.dir("generated/nativeShellAssets")
val startupAssets = layout.buildDirectory.dir("generated/nativeShellStartupAssets")
val generatedStrings = layout.buildDirectory.dir("generated/nativeShellStrings")

val copyStartupBusyImage by tasks.registering(Copy::class) {
    from(resourceRoot.resolve("images/topmenu_plugin/754463d1.png")) { rename { "startup-busy.png" } }
    into(startupAssets)
}

// Development fixtures (copied game icons/LiveAreas) never enter the shipped pack.
val packNativeShellAssets by tasks.registering(Zip::class) {
    dependsOn(verifyNativeShellResources)
    from(resourceRoot) { exclude("preview/home/**") }
    destinationDirectory.set(packedAssets)
    archiveFileName.set("native-ui.zip")
    isPreserveFileTimestamps = false
    isReproducibleFileOrder = true
}

val indexNativeShellAssets by tasks.registering {
    dependsOn(packNativeShellAssets)
    val archive = packNativeShellAssets.flatMap { it.archiveFile }
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

// Every native_ui_* string of the application (all of its locales) is mirrored
// into this module, so the shell speaks every language the app does.
val sourceAppResources = file("../../app/src/main/res")
val generateNativeShellStrings by tasks.registering {
    inputs.dir(sourceAppResources)
    outputs.dir(generatedStrings)
    doLast {
        val outputRoot = generatedStrings.get().asFile
        outputRoot.deleteRecursively()
        val localeDirs = sourceAppResources.listFiles().orEmpty()
            .filter { it.isDirectory && (it.name == "values" || it.name.startsWith("values-")) }
            .filter { it.resolve("strings.xml").isFile }
        check(localeDirs.any { it.name == "values" }) { "App default strings.xml is missing" }
        fun parse(dir: File) = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(dir.resolve("strings.xml"))
        fun nativeUiStrings(document: org.w3c.dom.Document): Map<String, String> {
            val strings = document.getElementsByTagName("string")
            val result = LinkedHashMap<String, String>()
            for (index in 0 until strings.length) {
                val element = strings.item(index) as org.w3c.dom.Element
                val name = element.getAttribute("name")
                if (name.startsWith("native_ui_")) result[name] = element.textContent
            }
            return result
        }
        val required = nativeUiStrings(parse(localeDirs.first { it.name == "values" })).keys
        fun escapeXml(value: String): String = value
            .replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace("\"", "&quot;")
        localeDirs.forEach { localeDir ->
            val strings = nativeUiStrings(parse(localeDir))
            val missing = required - strings.keys
            check(missing.isEmpty()) { "Missing $missing translation in ${localeDir.name}/strings.xml" }
            val body = required.joinToString(separator = System.lineSeparator()) { name ->
                "    <string name=\"$name\">${escapeXml(strings.getValue(name))}</string>"
            }
            outputRoot.resolve(localeDir.name).apply { mkdirs() }.resolve("strings.xml").writeText(
                listOf("<?xml version=\"1.0\" encoding=\"utf-8\"?>", "<resources>", body, "</resources>", "")
                    .joinToString(System.lineSeparator()),
            )
        }
    }
}

tasks.named("preBuild") {
    dependsOn(indexNativeShellAssets, copyStartupBusyImage, generateNativeShellStrings)
}

android {
    namespace = "com.sbro.emucorev.nativeui.shell"
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    androidResources { noCompress += "zip" }
    sourceSets.getByName("main").kotlin.srcDir("../android/src/main/java")
    sourceSets.getByName("main").assets.srcDir(packedAssets.get().asFile)
    sourceSets.getByName("main").assets.srcDir(startupAssets.get().asFile)
    sourceSets.getByName("main").res.srcDir(generatedStrings.get().asFile)
    defaultConfig {
        minSdk = 29
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DBUILD_TESTING=OFF")
                targets += "emucorev_shell"
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
