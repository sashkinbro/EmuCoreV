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

// Native PS Vita shell UI. Its ready resource pack is prepared offline into the
// git-ignored native-ui/resources directory; without it the app keeps the
// Compose carousel library mode.
if (file("native-ui/resources/images.json").isFile) {
    include(":native-ui-shell")
    project(":native-ui-shell").projectDir = file("native-ui/shell")
}

// Explicitly requested development preview; absent from normal/release builds.
if (providers.gradleProperty("nativeUiPreview").orNull == "true") {
    include(":native-ui-preview")
    project(":native-ui-preview").projectDir = file("native-ui/preview")
}
