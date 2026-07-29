package com.sbro.emucorev.ui.settings

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsTabsUiContractTest {
    @Test
    fun updatesProAndAboutAreTheFinalTabsInThatOrder() {
        val source = sourceRoot()
            .resolve("ui/settings/SettingsScreen.kt")
            .readText()
        val updates = source.indexOf("Updates(R.string.settings_tab_updates")
        val pro = source.indexOf("Pro(R.string.settings_pro_tab")
        val about = source.indexOf("About(R.string.settings_tab_about")

        assertTrue("Updates tab declaration is missing", updates >= 0)
        assertTrue("Pro tab must follow Updates", pro > updates)
        assertTrue("About tab must follow Pro", about > pro)
        assertTrue(
            "Pro must be directly before About",
            Regex(
                """Updates\(R\.string\.settings_tab_updates[\s\S]*?Pro\(R\.string\.settings_pro_tab[\s\S]*?About\(R\.string\.settings_tab_about"""
            ).containsMatchIn(source)
        )
    }

    @Test
    fun selectedTabIsMadeVisibleAndSmoothlyCentered() {
        val source = sourceRoot()
            .resolve("ui/settings/SettingsScreen.kt")
            .readText()

        assertTrue("Tab row needs persistent lazy-list state", "rememberLazyListState()" in source)
        assertTrue("Tab row must react to selection", "LaunchedEffect(selectedTab)" in source)
        assertTrue("Off-screen tabs must first become visible", "listState.scrollToItem(selectedIndex)" in source)
        assertTrue("Visible selected tabs must animate to center", "listState.animateScrollBy(delta)" in source)
        assertTrue("LazyRow must use the centering state", "state = listState" in source)
    }

    @Test
    fun onboardingUsesSeparateBenefitCardsAndACompactPurchaseCard() {
        val sourceRoot = sourceRoot()
        val onboarding = sourceRoot.resolve("ui/onboarding/OnboardingScreen.kt").readText()
        val proUi = sourceRoot.resolve("ui/pro/ProPurchaseUi.kt").readText()

        assertTrue("Onboarding must render dedicated benefit cards", "ProBenefitCards(" in onboarding)
        assertTrue("Purchase card must not duplicate benefit rows", "showFeatures = false" in onboarding)
        assertTrue("Crimson needs its own card", "settings_pro_feature_crimson_title" in proUi)
        assertTrue("Gold icon needs its own card", "settings_pro_feature_icon_title" in proUi)
        assertTrue("Profile badge needs its own card", "settings_pro_feature_profile_title" in proUi)
        assertTrue("Benefit cards need independent surfaces", "private fun ProBenefitCard(" in proUi)
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(
            workingDirectory,
            workingDirectory.resolve("app")
        ).firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
