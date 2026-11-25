@file:Suppress("UnstableApiUsage", "DEPRECATION")

import java.io.File
import java.util.Properties

val androidOpenSslRoot = rootProject.layout.projectDirectory.dir("tools/openssl-test/out").asFile
val emuCoreVNativeHook = project.layout.projectDirectory.file("src/main/cpp/emucorev/cmake/ECVProjectHook.cmake").asFile
val ecvNewAssetsDir = project.layout.projectDirectory.dir("src/main/cpp/emucorev/ECV/android/app/assets").asFile
val ecvLegacyAssetsDir = project.layout.projectDirectory.dir("src/main/cpp/emucorev/ECV/android/assets").asFile
val ecvAssetsDir = if (ecvNewAssetsDir.exists()) ecvNewAssetsDir else ecvLegacyAssetsDir
val localBuildProperties = Properties().apply {
    rootProject.layout.projectDirectory.file("local.properties").asFile
        .takeIf(File::exists)
        ?.inputStream()
        ?.use { input -> load(input) }
}
val releaseSigningKeys = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
val releaseSigningValues = releaseSigningKeys.associateWith {
    localBuildProperties.getProperty("emucorev.release.$it")?.takeIf(String::isNotBlank)
}
val hasReleaseSigning = releaseSigningValues.values.any { it != null }
require(!hasReleaseSigning || releaseSigningValues.values.all { it != null }) {
    "Configure all emucorev.release signing properties in local.properties"
}
val discordApplicationId = "1536775623287115786"
val discordSdkDirectory = localBuildProperties.getProperty("emucorev.discord.sdkDir")
    ?.let(::File)
    ?.takeIf { sdkDir ->
        sdkDir.resolve("include/discordpp.h").isFile &&
            sdkDir.resolve("arm64-v8a/libdiscord_partner_sdk.so").isFile &&
            sdkDir.resolve("discord_partner_sdk.aar").isFile
    }
val python3Executable = sequenceOf(
    providers.gradleProperty("python3.path").orNull,
    localBuildProperties.getProperty("python3.path"),
    System.getenv("PYTHON3"),
    System.getenv("PYTHON")
).filterNotNull()
    .map(::File)
    .firstOrNull(File::isFile)

// vita3k's cmake/vcpkg_android.cmake hard-requires ANDROID_NDK_HOME and VCPKG_ROOT env vars.
// The EmuCoreV layer must not patch vita3k core, so inject them here for the CMake subprocess.
run {
    val ndkVersion = "29.0.14206865"
    val sdkDir = localBuildProperties.getProperty("sdk.dir")
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

// The native PS Vita shell module exists only when its local resource pack does
// (see settings.gradle.kts); otherwise library mode keeps the Compose carousel.
val nativeShellAvailable = rootProject.findProject(":native-ui-shell") != null

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.google.services)
    alias(libs.plugins.ksp)
}

fun buildConfigString(value: String): String = "\"" + value
    .replace("\\", "\\\\")
    .replace("\"", "\\\"") + "\""

val feedbackEndpoint = localBuildProperties.getProperty("emucorev.feedback.endpoint").orEmpty()
val feedbackApiKey = localBuildProperties.getProperty("emucorev.feedback.apiKey").orEmpty()

// AGP gives source-set JNI libraries precedence over CMake outputs. A locally
// copied core would silently replace the core compiled for this variant,
// including debug diagnostics and fixes. Only CMake supplies the current ECV core.
val bundledJniLibraries = tasks.register<Sync>("prepareBundledJniLibraries") {
    from("src/main/jniLibs") {
        exclude("**/libECV.so")
        exclude("**/libVita3K.so")
    }
    into(layout.buildDirectory.dir("generated/bundled-jni-libs"))
}
val bundledDebugNativeLibraries = tasks.register<Sync>("prepareBundledDebugNativeLibraries") {
    from("src/main/cpp/emucorev/ECV/android/prebuilt") {
        exclude("**/libECV.so")
        exclude("**/libVita3K.so")
    }
    into(layout.buildDirectory.dir("generated/bundled-debug-native-libs"))
}
val bundledDebugNativeLibrariesDirectory = layout.buildDirectory.dir("generated/bundled-debug-native-libs").get().asFile

android {
    namespace = "com.sbro.emucorev"
    ndkVersion = "29.0.14206865"
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }

    defaultConfig {
        applicationId = "com.sbro.emucorev"
        minSdk = 29
        targetSdk = 37
        versionCode = 96
        versionName = "0.2.9"

        buildConfigField("String", "FEEDBACK_ENDPOINT", buildConfigString(feedbackEndpoint))
        buildConfigField("String", "FEEDBACK_API_KEY", buildConfigString(feedbackApiKey))
        buildConfigField("long", "DISCORD_APPLICATION_ID", "${discordApplicationId}L")
        buildConfigField("boolean", "DISCORD_SDK_AVAILABLE", (discordSdkDirectory != null).toString())
        buildConfigField("boolean", "NATIVE_SHELL", nativeShellAvailable.toString())
        manifestPlaceholders["discordSdkAvailable"] = (discordSdkDirectory != null).toString()

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
                arguments += "-DCMAKE_PROJECT_ECV_INCLUDE=${emuCoreVNativeHook.invariantSeparatorsPath}"
                // Android dependencies are vendored by the Vita3K tree. Avoid vcpkg trying
                // to build host tools with MSVC during ordinary Gradle/Android Studio builds.
                arguments += "-DVCPKG_MANIFEST_INSTALL=OFF"
                discordSdkDirectory?.let { sdkDir ->
                    arguments += "-DDISCORD_SDK_DIR=${sdkDir.invariantSeparatorsPath}"
                }
                python3Executable?.let { python ->
                    arguments += "-DPython3_EXECUTABLE=${python.invariantSeparatorsPath}"
                }
            }
        }
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = rootProject.file(releaseSigningValues.getValue("storeFile")!!)
                storePassword = releaseSigningValues.getValue("storePassword")
                keyAlias = releaseSigningValues.getValue("keyAlias")
                keyPassword = releaseSigningValues.getValue("keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
            // Opt-in (-PdebugWithReleaseSigning=true): sign the debug build with the release
            // key so it installs over an existing release install (install -r) and keeps
            // the device's games, saves and settings while testing with core logs.
            if (hasReleaseSigning && providers.gradleProperty("debugWithReleaseSigning").orNull == "true") {
                signingConfig = signingConfigs.getByName("release")
            }
            externalNativeBuild {
                cmake {
                    // CMake Debug compiles the whole emulator at -O0 and instruments
                    // every core component with Tracy, which makes on-device testing
                    // painfully slow (multi-minute game startup). Keep symbols and
                    // native debugging, but optimize and drop the profiler.
                    arguments += "-DCMAKE_CXX_FLAGS_DEBUG=-O2 -g"
                    arguments += "-DCMAKE_C_FLAGS_DEBUG=-O2 -g"
                    arguments += "-DTRACY_ENABLE_ON_CORE_COMPONENTS=OFF"
                    // Private research oracles (tools/oracles, if present) exist only in the debug variant.
                    arguments += "-DECV_ORACLE=ON"
                }
            }
        }
        release {
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
            isDebuggable = false
            isMinifyEnabled = true
            isShrinkResources = true
            isJniDebuggable = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
                "src/main/cpp/emucorev/ECV/android/proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    // Release native ships as RelWithDebInfo (symbols for Play),
                    // which enables Tracy profiler threads + verbose spdlog
                    // (flush_on trace) upstream. Both starve the UI thread on
                    // low-end GPUs and trip the Vitals ANR grouped under
                    // condition_variable::wait. Keep symbols, drop the profiler.
                    // No-op with a warning when upstream builds as Release.
                    arguments += "-DTRACY_ENABLE_ON_CORE_COMPONENTS=OFF"
                }
            }
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
            val shellSources = if (nativeShellAvailable) "src/nativeShell/java" else "src/nativeShellStub/java"
            java.setSrcDirs(listOf("src/main/java", shellSources))
            kotlin.srcDir(shellSources)
            res.setSrcDirs(listOf("src/main/res"))
            assets.setSrcDirs(
                listOf(
                    "src/main/assets",
                    ecvAssetsDir
                )
            )
            jniLibs.setSrcDirs(listOf(bundledJniLibraries))
            discordSdkDirectory?.let(jniLibs::srcDir)
        }
        // The vendored Khronos validation layer (downloaded into
        // src/main/cpp/emucorev/ECV/android/prebuilt for non-Release CMake builds)
        // is a debug-only tool: the Vulkan loader auto-discovers
        // libVkLayer_*.so from the app library dir, so shipping it in release
        // routes every vkCreateInstance through stale validation code (Vitals
        // SIGSEGV in VKState::create during layer/extension enumeration).
        // Keep it for debug, drop it (~24 MB) from release; the in-app
        // validation setting disables itself gracefully when absent.
        getByName("debug") {
            jniLibs.srcDir(bundledDebugNativeLibrariesDirectory)
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/emucorev/ECV/CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            pickFirsts += "**/libdiscord_partner_sdk.so"
        }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
        // Robolectric recreates the Application per test: run its startup work inline so tests
        // never race the startup worker over config files (production keeps the worker thread).
        unitTests.all { it.systemProperty("emucorev.startup.sync", "true") }
    }
}

// SourceSet accepts only static paths. Add an explicit dependency so the filtered
// generated debug JNI directory is ready before AGP merges native libraries.
tasks.configureEach {
    if (name == "mergeDebugJniLibFolders") {
        dependsOn(bundledDebugNativeLibraries)
    }
}

// Release must never contain research/oracle tooling. When the private checker
// (git-ignored tools/oracles) is present, every release APK is scanned after assembly.
val releaseCleanChecker = rootProject.file("tools/oracles/check_release_clean.py")
if (releaseCleanChecker.isFile) {
    // Scans the newest existing artifact among the candidates: an unsigned build produces
    // app-release-unsigned.apk instead of app-release.apk.
    fun registerReleaseCleanCheck(taskName: String, outputsDir: String, candidates: List<String>) =
        tasks.register<Exec>(taskName) {
            val directory = layout.buildDirectory.dir(outputsDir)
            val checker = releaseCleanChecker.absolutePath
            fun existing() = candidates.map { directory.get().file(it).asFile }.filter(File::isFile)
            onlyIf { existing().isNotEmpty() }
            doFirst {
                commandLine("python", "-I", checker, existing().maxByOrNull(File::lastModified)!!.absolutePath)
            }
        }
    val checkReleaseApkClean = registerReleaseCleanCheck(
        "checkReleaseClean", "outputs/apk/release", listOf("app-release.apk", "app-release-unsigned.apk"))
    val checkReleaseBundleClean = registerReleaseCleanCheck(
        "checkReleaseBundleClean", "outputs/bundle/release", listOf("app-release.aab"))
    tasks.configureEach {
        if (name == "assembleRelease") finalizedBy(checkReleaseApkClean)
        if (name == "bundleRelease") finalizedBy(checkReleaseBundleClean)
    }
} else {
    // The tools directory is private and absent in public checkouts: warn, never fail.
    tasks.configureEach {
        if (name == "assembleRelease" || name == "bundleRelease") {
            doLast {
                logger.warn("WARNING: tools/oracles/check_release_clean.py is missing; the release " +
                    "artifact was NOT scanned for research/oracle tooling.")
            }
        }
    }
}

// A release build without signing keys would silently produce an unsigned APK/AAB that cannot be
// installed or published. Fail the release artifact tasks with a clear message (debug builds and
// the opt-in debugWithReleaseSigning are unaffected).
if (!hasReleaseSigning) {
    val requireReleaseSigning = tasks.register("requireReleaseSigning") {
        doFirst {
            throw GradleException(
                "Release signing is not configured. Set emucorev.release.storeFile, storePassword, " +
                    "keyAlias and keyPassword in local.properties to build a release artifact."
            )
        }
    }
    tasks.configureEach {
        if (name in setOf("assembleRelease", "bundleRelease", "packageRelease", "packageReleaseBundle")) {
            dependsOn(requireReleaseSigning)
        }
    }
}

dependencies {
    if (nativeShellAvailable) implementation(project(":native-ui-shell"))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.documentfile)
    implementation(libs.androidx.core.google.shortcuts)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.androidx.browser)
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
    implementation(libs.commons.compress)
    implementation(platform(libs.firebase.bom))
    implementation(libs.firebase.auth)
    implementation(libs.firebase.firestore)
    implementation(libs.firebase.analytics)
    implementation(libs.google.play.billing)
    implementation(libs.google.play.review)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.work.runtime)
    implementation(libs.androidx.room.runtime)
    implementation(libs.androidx.room.ktx)
    implementation(libs.coil.compose)
    implementation(libs.coil.network.okhttp)
    ksp(libs.androidx.room.compiler)
    implementation(libs.google.play.review.ktx)
    implementation(libs.google.play.age.signals)
    implementation(libs.androidx.credentials)
    implementation(libs.androidx.credentials.play.services)
    implementation(libs.google.identity)
    implementation(libs.google.auth)
    discordSdkDirectory?.let { sdkDir ->
        implementation(files(sdkDir.resolve("discord_partner_sdk.aar")))
    }

    testImplementation(libs.junit)
    testImplementation(libs.robolectric)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}

ksp {
    arg("room.schemaLocation", "$projectDir/schemas")
}
