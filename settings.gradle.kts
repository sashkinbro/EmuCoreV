pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "EmuCoreV"
include(":app")

// Native PS Vita shell UI. It ships no firmware-derived data: its resource pack is
// built on the device from the firmware the user installed (see FirmwarePack), and
// the app keeps the Compose carousel library mode until that firmware is present.
include(":native-ui-shell")
project(":native-ui-shell").projectDir = file("native-ui/shell")

// Explicitly requested development preview; absent from normal/release builds.
if (providers.gradleProperty("nativeUiPreview").orNull == "true") {
    include(":native-ui-preview")
    project(":native-ui-preview").projectDir = file("native-ui/preview")
}
