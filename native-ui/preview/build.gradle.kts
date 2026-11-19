plugins {
    alias(libs.plugins.android.application)
}

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
