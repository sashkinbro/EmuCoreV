plugins {
    alias(libs.plugins.android.application)
}

val verifyLocalNativeUiAssets by tasks.registering {
    doLast {
        val root = file("../resources")
        val required = listOf("images.json", "fonts.json", "fonts/ltn4.otf",
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
tasks.named("preBuild") { dependsOn(verifyLocalNativeUiAssets) }

android {
    namespace = "com.sbro.emucorev.nativeuipreview"
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    sourceSets.getByName("main").kotlin.srcDir("../android/src/main/java")
    sourceSets.getByName("main").assets.srcDir("../resources")
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
