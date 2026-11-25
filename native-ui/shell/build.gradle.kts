import javax.xml.parsers.DocumentBuilderFactory

plugins {
    alias(libs.plugins.android.library)
}

// Standalone native Vita shell UI. It bundles no firmware-derived resources: the
// resource pack (fonts, images, sounds, effects, system applications) is extracted
// on the device from the firmware the user installed (FirmwarePack + the native
// emucorev_extract library). Only the application's own art ships as assets.
val generatedStrings = layout.buildDirectory.dir("generated/nativeShellStrings")

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
    dependsOn(generateNativeShellStrings)
}

android {
    namespace = "com.sbro.emucorev.nativeui.shell"
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }
    ndkVersion = "29.0.14206865"
    sourceSets.getByName("main").kotlin.srcDir("../android/src/main/java")
    sourceSets.getByName("main").res.srcDir(generatedStrings.get().asFile)
    defaultConfig {
        minSdk = 29
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DBUILD_TESTING=OFF")
                targets += listOf("emucorev_shell", "emucorev_packjni")
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
