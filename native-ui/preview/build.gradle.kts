import java.security.MessageDigest

plugins {
    alias(libs.plugins.android.application)
}

val verifyLocalNativeUiAssets by tasks.registering {
    doLast {
        val root = file("../resources")
        val required = listOf("images.json", "fonts.json", "fonts/ltn4.otf", "fonts/ltn0.otf", "audio/sounds.json",
            "effects/lookups/lookups.json", "effects/lookups/exp-minus.png",
            "effects/lookups/corner-distance.png",
            "images/paf-common-resource/ba195a37.png",
            "images/indicator_plugin/ca76be07.png", "images/indicator_plugin/56e72f16.png",
            "preview/bubble/stream-0.bin", "preview/bubble/indices.bin",
            "preview/bubble/vertex-ubo-0.bin", "preview/bubble/fragment-ubo-0.bin",
            "preview/bubble/fragment-texture-0.bin", "preview/bubble/fragment-texture-0-palette.bin",
            "preview/bubble/environment.rgba")
        check(required.all { root.resolve(it).isFile }) {
            "Local native UI resource pack is missing or incomplete. " +
                "Prepare native-ui/resources using the offline tools documented in native-ui/README.md."
        }
    }
}
val packedAssets = layout.buildDirectory.dir("generated/nativeUiAssets")
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
tasks.named("preBuild") { dependsOn(indexNativeUiAssets) }

android {
    namespace = "com.sbro.emucorev.nativeuipreview"
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    androidResources { noCompress += "zip" }
    sourceSets.getByName("main").kotlin.srcDir("../android/src/main/java")
    sourceSets.getByName("main").assets.srcDir(packedAssets.get().asFile)
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
