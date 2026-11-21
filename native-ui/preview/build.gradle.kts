plugins {
    alias(libs.plugins.android.application)
}

// Development host for the native shell. It shows the local fixture catalog
// (copied game icons/LiveAreas in the git-ignored resource directory); the
// application itself supplies real installed games.
val fixtureAssets = layout.buildDirectory.dir("generated/nativeShellFixture")
val copyHomeFixture by tasks.registering(Copy::class) {
    from(file("../resources/preview/home"))
    into(fixtureAssets.map { it.dir("home-fixture") })
}
tasks.named("preBuild") { dependsOn(copyHomeFixture) }

android {
    namespace = "com.sbro.emucorev.nativeuipreview"
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    sourceSets.getByName("main").assets.srcDir(fixtureAssets.get().asFile)
    defaultConfig {
        applicationId = "com.sbro.emucorev.nativeuipreview"
        minSdk = 29
        targetSdk = 37
        versionCode = 1
        versionName = "native-preview"
        ndk { abiFilters += "arm64-v8a" }
    }
}

dependencies {
    implementation(project(":native-ui-shell"))
}

// This package is a local development surface, never a release deliverable.
androidComponents {
    beforeVariants(selector().withBuildType("release")) { it.enable = false }
}
