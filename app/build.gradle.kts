@file:Suppress("UnstableApiUsage", "DEPRECATION")

import java.io.File

val androidOpenSslRoot = rootProject.layout.projectDirectory.dir("tools/openssl-test/out").asFile!!
val emuCoreVNativeHook = project.layout.projectDirectory.file("src/main/cpp/emucorev/cmake/Vita3KProjectHook.cmake").asFile!!
val vita3kNewAssetsDir = project.layout.projectDirectory.dir("src/main/cpp/vita3k/android/app/assets").asFile!!
val vita3kLegacyAssetsDir = project.layout.projectDirectory.dir("src/main/cpp/vita3k/android/assets").asFile!!
val vita3kAssetsDir = if (vita3kNewAssetsDir.exists()) vita3kNewAssetsDir else vita3kLegacyAssetsDir

// vita3k's cmake/vcpkg_android.cmake hard-requires ANDROID_NDK_HOME and VCPKG_ROOT env vars.
// The EmuCoreV layer must not patch vita3k core, so inject them here for the CMake subprocess.
run {
    val ndkVersion = "29.0.14206865"
    val sdkDir = rootProject.layout.projectDirectory.file("local.properties").asFile
        .takeIf { it.exists() }
        ?.readLines()
        ?.firstOrNull { it.startsWith("sdk.dir=") }
        ?.substringAfter("sdk.dir=")
        ?.replace("\\:", ":")
        ?.replace("\\\\", "\\")
        ?: System.getenv("ANDROID_HOME")
        ?: System.getenv("ANDROID_SDK_ROOT")
    val ndkHome: File? = sdkDir?.let { File(it, "ndk/$ndkVersion") }
    val vcpkgRoot: File = rootProject.layout.projectDirectory.dir("third_party/vcpkg").asFile

    fun setEnv(name: String, value: String) {
        if (System.getenv(name) != null) return
        try {
            val pe = Class.forName("java.lang.ProcessEnvironment")
            val theEnvField = pe.getDeclaredField("theEnvironment").apply { isAccessible = true }
            @Suppress("UNCHECKED_CAST")
            (theEnvField.get(null) as MutableMap<String, String>)[name] = value
            val ciField = pe.getDeclaredField("theCaseInsensitiveEnvironment").apply { isAccessible = true }
            @Suppress("UNCHECKED_CAST")
            (ciField.get(null) as MutableMap<String, String>)[name] = value
        } catch (_: Throwable) {
            try {
                val cl = System.getenv().javaClass
                val m = cl.getDeclaredField("m").apply { isAccessible = true }
                @Suppress("UNCHECKED_CAST")
                (m.get(System.getenv()) as MutableMap<String, String>)[name] = value
            } catch (_: Throwable) { /* best-effort */ }
        }
    }

    if (ndkHome != null && ndkHome.exists()) setEnv("ANDROID_NDK_HOME", ndkHome.absolutePath.replace('\\', '/'))
    if (vcpkgRoot.exists()) setEnv("VCPKG_ROOT", vcpkgRoot.absolutePath.replace('\\', '/'))
}

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.sbro.emucorev"
    ndkVersion = "29.0.14206865"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "com.sbro.emucorev"
        minSdk = 28
        targetSdk = 36
        versionCode = 35
        versionName = "0.0.8"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            //noinspection ChromeOsAbiSupport
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                arguments += "-DOPENSSL_ROOT_DIR=${androidOpenSslRoot.invariantSeparatorsPath}"
                arguments += "-DOPENSSL_USE_STATIC_LIBS=TRUE"
                arguments += "-DCMAKE_PROJECT_Vita3K_INCLUDE=${emuCoreVNativeHook.invariantSeparatorsPath}"
            }
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            isJniDebuggable = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
                "src/main/cpp/vita3k/android/proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }

    sourceSets {
        getByName("main") {
            java.setSrcDirs(listOf("src/main/java"))
            res.setSrcDirs(listOf("src/main/res"))
            assets.setSrcDirs(
                listOf(
                    "src/main/assets",
                    vita3kAssetsDir
                )
            )
            jniLibs.setSrcDirs(listOf("src/main/cpp/vita3k/android/prebuilt"))
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/vita3k/CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.documentfile)
    implementation(libs.androidx.core.google.shortcuts)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.google.material)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.process.phoenix)
    implementation(libs.relinker)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.android.youtube.player)

    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}
